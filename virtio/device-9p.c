/*
Treat 9P filesystem as accessible by path only
(unfortunately wasting the find-by-CNID ability of HFS+/APFS)

Therefore we need this mapping:
(CNID) <-> (parent's CNID, name)
*/

#include <Disks.h>
#include <Files.h>
#include <FSM.h>
#include <Gestalt.h>
#include <DriverServices.h>
#include <Memory.h>
#include <MixedMode.h>

#include "hashtab.h"
#include "lprintf.h"
#include "paramblkprint.h"
#include "rpc9p.h"
#include "transport.h"
#include "unicode.h"
#include "virtqueue.h"

#include <stdbool.h> // leave till last, conflicts with Universal Interfaces
#include <stddef.h>
#include <string.h>

#define c2pstr(p, c) {uint8_t l=strlen(c); p[0]=l; memcpy(p+1, c, l);}
#define p2cstr(c, p) {uint8_t l=p[0]; memcpy(c, p+1, l); c[l]=0;}

enum {
	CREATOR = (0x0613<<16) | ('9'<<8) | 'p',
};

// of a File Manager call
struct handler {
	void *func; // Unsafe magic allows different pb types
	short err; // If func==NULL then return ret
};

// in the hash table
struct record {
	int32_t parent;
	char name[];
};

static OSStatus initialize(DriverInitInfo *info);
static OSStatus finalize(DriverFinalInfo *info);
static OSErr CommProc(short message, struct IOParam *pb, void *globals);
static OSErr HFSProc(struct VCB *vcb, unsigned short selector, void *pb, void *globals, short fsid);
static OSErr MyVolumeMount(struct VolumeParam *pb, struct VCB *vcb);
static OSErr MyGetVolInfo(struct HVolumeParam *pb, struct VCB *vcb);
static OSErr MyGetVolParms(struct HIOParam *pb, struct VCB *vcb);
static OSErr MyGetFileInfo(struct HFileInfo *pb, struct VCB *vcb);
static int32_t browse(uint32_t fid, int32_t cnid, const unsigned char *paspath);
static int32_t pbDirID(void *pb);
static struct WDCBRec *wdcb(short refnum);
static int walkToCNID(int32_t cnid, uint32_t fid);
static int32_t makeCNID(int32_t parent, char *name);
static void cnidPrint(int32_t cnid);
static struct handler handler(unsigned short selector);

char stack[32*1024];
short drvRefNum;
unsigned long callcnt;
struct Qid9 root;
int32_t cnidCtr = 100;
static Handle finderwin;

DriverDescription TheDriverDescription = {
	kTheDescriptionSignature,
	kInitialDriverDescriptor,
	{"\x0cpci1af4,1009", {0x00, 0x10, 0x80, 0x00}}, // v0.1
	{kDriverIsLoadedUponDiscovery |
		kDriverIsOpenedUponLoad,
		"\x09.virtio9p"},
	{1, // nServices
	{{kServiceCategoryNdrvDriver, kNdrvTypeIsGeneric, {0x00, 0x10, 0x80, 0x00}}}} //v0.1
};

OSStatus DoDriverIO(AddressSpaceID spaceID, IOCommandID cmdID,
	IOCommandContents pb, IOCommandCode code, IOCommandKind kind) {
	OSStatus err;

	switch (code) {
	case kInitializeCommand:
	case kReplaceCommand:
		err = initialize(pb.initialInfo);
		break;
	case kFinalizeCommand:
	case kSupersededCommand:
		err = finalize(pb.finalInfo);
		break;
	case kControlCommand:
		err = controlErr;
		break;
	case kStatusCommand:
		err = statusErr;
		break;
	case kOpenCommand:
	case kCloseCommand:
		err = noErr;
		break;
	default:
		err = paramErr;
		break;
	}

	// Return directly from every call
	if (kind & kImmediateIOCommandKind) {
		return err;
	} else {
		return IOCommandIsComplete(cmdID, err);
	}
}

static OSStatus initialize(DriverInitInfo *info) {
	lprintf_enable = true;

	drvRefNum = info->refNum;

	OSStatus err;

	lprintf(".virtio9p: starting\n");

	// No need to signal FAILED if cannot communicate with device
	if (!VInit(&info->deviceEntry)) {
		lprintf(".virtio9p: failed VInit()\n");
		return paramErr;
	};

	if (!VFeaturesOK()) {
		lprintf(".virtio9p: failed VFeaturesOK()\n");
		return paramErr;
	}

	// Cannot go any further without touching virtqueues, which requires DRIVER_OK
	VDriverOK();

	// More buffers allow us to tolerate more physical mem fragmentation
	uint16_t viobufs = QInit(0, 256);
	if (viobufs < 2) {
		lprintf(".virtio9p: failed QInit()\n");
		return paramErr;
	}
	QInterest(0, 1);

	if (Init9(0, viobufs)) {
		return paramErr;
	}

	if (Attach9(2 /*special root CNID*/, (uint32_t)~0 /*auth=NOFID*/, "", "", &root)) {
		return paramErr;
	}

	lprintf("attached to root with path %08x%08x\n", (uint32_t)(root.path>>32), (uint32_t)root.path);

	long fsmver = 0;
	Gestalt(gestaltFSMVersion, &fsmver);
	lprintf("File System Manager version %04x\n", fsmver);

	// Bit strange... DQE starts mid-structure, 4 bytes of flags at neg offset
	struct AdornedDQE {
		uint32_t flags;
		DrvQEl dqe;
	};

	static struct AdornedDQE dqe = {
		.flags = 0x00080000, // fixed disk
		.dqe = {
			.dQFSID = CREATOR & 0xffff,
		}
	};

	AddDrive(drvRefNum, 22 /*drive number todo*/, &dqe.dqe);
	lprintf("DQE qLink %#08x\n", dqe.dqe.qLink);

	static RoutineDescriptor commDesc = BUILD_ROUTINE_DESCRIPTOR(uppFSDCommProcInfo, CommProc);
	static RoutineDescriptor hfsDesc = BUILD_ROUTINE_DESCRIPTOR(uppHFSCIProcInfo, HFSProc);

	static struct FSDRec fsdesc = {
		.fsdLength = sizeof (struct FSDRec),
		.fsdVersion = 1,
		.fileSystemFSID = CREATOR & 0xffff,
		.fileSystemName = "\x09Virtio-9P",
		.fileSystemCommProc = &commDesc,
		.fsdHFSCI = {
			.compInterfProc = &hfsDesc,
			.stackTop = stack + sizeof (stack),
			.stackSize = sizeof (stack),
			.idSector = -1, // networked volume
		},
	};

	int fserr = InstallFS(&fsdesc);
	lprintf("InstallFS returns %d\n", fserr);

	// Enable HFS component (whatever that means)
	fsdesc.fsdHFSCI.compInterfMask |= fsmComponentEnableMask | hfsCIResourceLoadedMask | hfsCIDoesHFSMask;
	fserr = SetFSInfo(CREATOR & 0xffff, sizeof fsdesc, &fsdesc);
	lprintf("SetFSInfo returns %d\n", fserr);

	static struct VolumeMountInfoHeader vmi = {
		.length = 8,
		.media = CREATOR,
		.flags = 0,
	};

	static struct IOParam pb = {
		.ioBuffer = (void *)&vmi,
	};
	fserr = PBVolumeMount((void *)&pb);

	char elmo[9] = {0,0,0,1,'E','l','m','o',0};
	HTinstall("\x00\x00\x00\x02", 4, elmo, sizeof(elmo));

	finderwin = NewHandleSysClear(2);

	return noErr;
}

static OSStatus finalize(DriverFinalInfo *info) {
	return noErr;
}

static OSErr CommProc(short message, struct IOParam *pb, void *globals) {
	lprintf("## CommProc message=%#02x paramBlock=%#08x\n", message, pb);

	switch (message) {
	case ffsNopMessage:
		return noErr;
	case ffsGetIconMessage:
		return afpItemNotFound;
	case ffsIDDiskMessage:
		return (pb->ioVRefNum == 22) ? noErr : extFSErr;
	case ffsLoadMessage:
		return noErr; // The HFS interface thingy is always loaded
	case ffsUnloadMessage:
		return noErr; // Haha no, because we aren't disk based
	case ffsIDVolMountMessage:
		return (((struct VolumeMountInfoHeader *)pb->ioBuffer)->media == CREATOR) ? noErr : extFSErr;
	case ffsInformMessage:
		return noErr;
	}

	return extFSErr;
}

static OSErr HFSProc(struct VCB *vcb, unsigned short selector, void *pb, void *globals, short fsid) {
	lprintf("%s", PBPrint(pb, selector, 1));

	callcnt++;

	OSErr result;
	struct handler h = handler(selector);

	if (h.func == NULL) {
		result = h.err;
	} else {
		// Unsafe calling convention magic
		typedef OSErr (*handlerFunc)(void *pb, struct VCB *vcb);
		result = ((handlerFunc)h.func)(pb, vcb);
	}

	// pb.ioResult for the PBPrint
	*(short *)((char *)pb + 16) = result;

	lprintf("%s", PBPrint(pb, selector, 0));

	return result;
}

static OSErr MyVolumeMount(struct VolumeParam *pb, struct VCB *vcb) {
	OSErr err;

	short sysVCBLength;
	err = UTAllocateVCB(&sysVCBLength, &vcb, 0 /*addSize*/);
	if (err) return err;

	// Values copied from SheepShaver
	vcb->vcbSigWord = 0x4244; // same as HFS
	vcb->vcbNmFls = 1;
	vcb->vcbNmRtDirs = 1;
	vcb->vcbNmAlBlks = 0xffff;
	vcb->vcbAlBlkSiz = 512;
	vcb->vcbClpSiz = 512;
	vcb->vcbNxtCNID = 100;
	vcb->vcbFreeBks = 0xffff;
	c2pstr(vcb->vcbVN, "Elmo");
	vcb->vcbFSID = CREATOR & 0xffff;
	vcb->vcbFilCnt = 1;
	vcb->vcbDirCnt = 1;

	err = UTAddNewVCB(22, &pb->ioVRefNum, vcb);
	if (err) return err;

	PostEvent(diskEvt, 22);

	return noErr;
}

static OSErr MyGetVolInfo(struct HVolumeParam *pb, struct VCB *vcb) {
	if (pb->ioNamePtr!=NULL && pb->ioVolIndex==0) {
		c2pstr(pb->ioNamePtr, "Elmo");
	}

	pb->ioVRefNum = -2;

	pb->ioVCrDate = 0;
	pb->ioVLsMod = 0;
	pb->ioVAtrb = 0;
	pb->ioVNmFls = 0;
	pb->ioVBitMap = 0;
	pb->ioAllocPtr = 0;
	pb->ioVNmAlBlks = 31744;
	pb->ioVAlBlkSiz = 512;
	pb->ioVClpSiz = 512;
	pb->ioAlBlSt = 0;
	pb->ioVNxtCNID = 100;
	pb->ioVFrBlk = 31744;

	if (pb->ioTrap & 0x200) {
		pb->ioVSigWord = 0x4244; // same as HFS
		pb->ioVDrvInfo = 22;
		pb->ioVDRefNum = drvRefNum;
		pb->ioVFSID = CREATOR & 0xffff;
		pb->ioVBkUp = 0;
		pb->ioVSeqNum = 0;
		pb->ioVWrCnt = 0;
		pb->ioVFilCnt = 1;
		pb->ioVDirCnt = 1;
		memset(pb->ioVFndrInfo, 0, sizeof (pb->ioVFndrInfo));
	}

	return noErr;
}

// -->    12    ioCompletion  long    optional completion routine ptr
// <--    16    ioResult      word    error result code
// -->    18    ioFileName    long    volume name specifier
// -->    22    ioVRefNum     word    volume refNum
// <--    32    ioBuffer      long    ptr to vol parms data
// -->    36    ioReqCount    long    size of buffer area
// <--    40    ioActCount    long    length of vol parms data

static OSErr MyGetVolParms(struct HIOParam *pb, struct VCB *vcb) {
	struct GetVolParmsInfoBuffer buf = {
		.vMVersion = 1, // goes up to version 4
		.vMAttrib = 0
			| (1<<bNoMiniFndr)
			| (1<<bNoLclSync)
			| (1<<bTrshOffLine)
			| (1<<bNoBootBlks)
			| (1<<bHasExtFSVol)
			| (1<<bHasFileIDs)
			| (1<<bLocalWList)
			,
		.vMLocalHand = finderwin,
		.vMServerAdr = 0, // might be used for uniqueness checking -- ?set uniq
	};

	short s = pb->ioReqCount;
	if (s > 14) s = 14; // not the whole struct, just the v1 part
	memcpy(pb->ioBuffer, &buf, s);
	pb->ioActCount = s;
	return noErr;
}

// Files:                                   Directories:
// -->    12    ioCompletion   pointer      -->    12    ioCompletion  pointer
// <--    16    ioResult       word         <--    16    ioResult      word
// <->    18    ioNamePtr      pointer      <->    18    ioNamePtr     pointer
// -->    22    ioVRefNum      word         -->    22    ioVRefNum     word
// <--    24    ioFRefNum      word         <--    24    ioFRefNum     word
// -->    28    ioFDirIndex    word         -->    28    ioFDirIndex   word
// <--    30    ioFlAttrib     byte         <--    30    ioFlAttrib    byte
// <--    31    ioACUser       byte         access rights for directory only
// <--    32    ioFlFndrInfo   16 bytes     <--    32    ioDrUsrWds    16 bytes
// <->    48    ioDirID        long word    <->    48    ioDrDirID     long word
// <--    52    ioFlStBlk      word         <--    52    ioDrNmFls     word
// <--    54    ioFlLgLen      long word
// <--    58    ioFlPyLen      long word
// <--    62    ioFlRStBlk     word
// <--    64    ioFlRLgLen     long word
// <--    68    ioFlRPyLen     long word
// <--    72    ioFlCrDat      long word    <--    72    ioDrCrDat    long word
// <--    76    ioFlMdDat      long word    <--    76    ioDrMdDat    long word
// <--    80    ioFlBkDat      long word    <--    80    ioDrBkDat    long word
// <--    84    ioFlXFndrInfo  16 bytes     <--    84    ioDrFndrInfo 16 bytes
// <--    100   ioFlParID      long word    <--    100    ioDrParID   long word
// <--    104   ioFlClpSiz     long word

static OSErr MyGetFileInfo(struct HFileInfo *pb, struct VCB *vcb) {
	enum {MYFID=3, LISTFID=4};

	bool flat = (pb->ioTrap&0xf2ff) == 0xa00c; // GetFileInfo without "H"
	bool longform = (pb->ioTrap&0x00ff) == 0x0060; // GetCatInfo

	int idx = pb->ioFDirIndex;
	if (idx<0 && !longform) idx=0; // make named GetFInfo calls behave right

	int32_t cnid = pbDirID(pb);

	if (idx > 0) {
		lprintf("   GCI index mode\n");

		if (walkToCNID(cnid, MYFID) < 0) return fnfErr;

		// Read the directory
		Walk9(MYFID, LISTFID, 0, NULL, NULL, NULL); // duplicate
		Lopen9(LISTFID, O_RDONLY, NULL, NULL); // iterate

		char utf8[512];
		int err;
		int n=0;
		while ((err=Readdir9(LISTFID, NULL, NULL, utf8)) == 0) {
			if (!strcmp(utf8, ".") || !strcmp(utf8, ".."))
				continue;
			if (pb->ioFDirIndex==++n) break;
		}

		if (err!=0) return fnfErr;

		cnid = makeCNID(cnid, utf8);
		Walk9(MYFID, MYFID, 1, (const char *[]){utf8}, NULL, NULL);
	} else if (idx == 0) {
		lprintf("   GCI name mode\n");
		cnid = browse(MYFID, cnid, pb->ioNamePtr);
		if (cnid < 0) return cnid;
	} else {
		lprintf("   GCI ID mode\n");
		cnid = browse(MYFID, cnid, "\p");
		if (cnid < 0) return cnid;
	}

	// MYFID and cnid now both valid

	// Here's some tricky debugging
	if (lprintf_enable) {
		lprintf("   GCI found "); cnidPrint(cnid); lprintf("\n");
	}

	// MYFID and cnid point to the correct file
	struct record *detail = HTlookup(&cnid, sizeof(cnid));
	if (detail == NULL) lprintf("BAD LOOKUP\n");

	// Return the filename
	if (idx!=0 && pb->ioNamePtr!=NULL) {
		mr31name(pb->ioNamePtr, detail->name);
	}

	uint64_t size, time;
	struct Qid9 qid;
	if (Getattr9(MYFID, &qid, &size, &time)) return permErr;

	pb->ioDirID = cnid; // alias ioDrDirID
	pb->ioFlParID = detail->parent; // alias ioDrDirID
	pb->ioFRefNum = 0;

	if (qid.type & 0x80) { // directory
		int n=-2; // to get rid of . and ..
		Lopen9(MYFID, O_RDONLY, NULL, NULL); // iterate
		while (Readdir9(MYFID, NULL, NULL, NULL) == 0) {
			n++;
		}

		pb->ioFlAttrib = 0x10;
		pb->ioFlFndrInfo = (struct FInfo){}; // alias ioDrUsrWds
		pb->ioFlStBlk = n; // alias ioDrNmFls
		pb->ioFlCrDat = 0; // alias ioDrCrDat
		pb->ioFlMdDat = 0; // alias ioDrMdDat
		if (longform) {
			pb->ioFlBkDat = 0; // alias ioDrBkDat
			pb->ioFlXFndrInfo = (struct FXInfo){0}; // alias ioDrFndrInfo
		}
	} else { // file
		pb->ioFlAttrib = 0;
		pb->ioACUser = 0;
		pb->ioFlFndrInfo = (struct FInfo){0};
		pb->ioFlStBlk = 0;
		pb->ioFlLgLen = 0;
		pb->ioFlPyLen = 0;
		pb->ioFlRStBlk = 0;
		pb->ioFlRLgLen = 0;
		pb->ioFlRPyLen = 0;
		pb->ioFlCrDat = 0;
		pb->ioFlMdDat = 0;
		if (longform) {
			pb->ioFlBkDat = 0;
			pb->ioFlXFndrInfo = (struct FXInfo){0};
			pb->ioFlClpSiz = 0;
		}
	}

	return noErr;
}

// As a utility routine, be careful to clean up our FIDs
static int32_t browse(uint32_t fid, int32_t cnid, const unsigned char *paspath) {
	enum {LISTFID=5};

	// Disallow nulls before we convert to C string
	for (int i=0; i<paspath[0]; i++) {
		if (paspath[i+1]==0) return bdNamErr;
	}

	// Null termination makes tokenisation easier (just convert : to null)
	char cpath[256];
	p2cstr(cpath, paspath);
	int pathlen=strlen(cpath);

	bool pathAbsolute = ((cpath[0] != ':') && (strstr(cpath, ":") != NULL))
		|| (cnid == 1);

	// Tokenize
	for (int i=0; i<sizeof cpath; i++) {
		if (cpath[i] == ':') cpath[i] = 0;
	}

	char *comp=cpath;
	if (pathAbsolute) {
		// Reenable this volname check once we support multiple volumes
		if (false && strcmp(comp, "Elmo")) {
			return bdNamErr;
		}

		// Cut the disk name off, leaving the leading colon
		comp += strlen(comp);

		cnid = 2;
	} else if (cnid <= 2) {
		cnid = 2;
	}

	// Walk to that CNID
	if (walkToCNID(cnid, fid) < 0) return fnfErr;

	// Trim empty component from the start
	// so that ":subdir" doesn't become ".." but "::subdir" does
	if (comp[0] == 0) comp++;

	// Compare each path component with the filesystem
	for (; comp<cpath+pathlen; comp+=strlen(comp)+1) {
		if (comp[0] == 0) {
			// Consecutive colons are like ".."
			struct record *rec = HTlookup(&cnid, sizeof(cnid));
			if (rec == NULL) {
				lprintf("DANGLING CNID!");
				return fnfErr;
			}

			if (Walk9(fid, fid, 1, (const char *[]){".."}, NULL, NULL))
				return fnfErr;
		} else {
			unsigned char want[32], got[32];
			char gotutf8[512];
			if (strlen(comp) > 31) return bdNamErr;
			c2pstr(want, comp);

			// Read the directory
			Walk9(fid, LISTFID, 0, NULL, NULL, NULL); // duplicate
			Lopen9(LISTFID, O_RDONLY, NULL, NULL);

			// Need to list the directory and see what matches!
			// (A shortcut might be to query the CNID table)
			int err;
			while ((err=Readdir9(LISTFID, NULL, NULL, gotutf8)) == 0) {
				mr31name(got, gotutf8);
				if (RelString(want, got, 0, 1) == 0) break;
			}
			Clunk9(LISTFID);
			if (err) return fnfErr; // Or dirNFErr?

			// We were promised that this exists
			Walk9(fid, fid, 1, (const char *[]){gotutf8}, NULL, NULL);

			cnid = makeCNID(cnid, gotutf8);
		}
	}

	return cnid;
}

// Handles one volume only: will need revision
static int32_t pbDirID(void *pb) {
	struct HFileParam *pbcast = pb;

	// HFSDispatch or another hierarchical call: use dirID if nonzero
	if ((pbcast->ioTrap & 0xff) == 0x60 || (pbcast->ioTrap & 0x200) != 0) {
		if (pbcast->ioDirID != 0) {
			return pbcast->ioDirID;
		}
	}

	// Otherwise fall back on vRefNum
	if (pbcast->ioVRefNum == -2) {
		return 2; // root
	} else if (pbcast->ioVRefNum == 0) {
		return wdcb(-32765)->wdDirID; // default
	} else {
		return wdcb(pbcast->ioVRefNum)->wdDirID;
	}
}

// Conservatively check for this WDCB
static struct WDCBRec *wdcb(short refnum) {
	char *table = *(char **)0x372; // unaligned access?
	int16_t tblSize = *(int16_t *)table;
	int16_t offset = refnum + 0x7fff;
	if (offset<2 || offset>tblSize || (offset%16)!=2) {
		lprintf("bad wdcb refnum %d, will probably crash now\n", refnum);
		return (void *)0x68f168f1;
	}
	return (struct WDCBRec *)(table + offset);
}

// If positive, return the "type" field from the qid
// If negative, an error
static int walkToCNID(int32_t cnid, uint32_t fid) {
	char *components[256];
	char **compptr = &components[256]; // fully descending
	int compcnt = 0;

	static char blob[2048];
	char *blobptr = blob; // empty ascending

	static struct Qid9 qids[256];

	while (cnid != 2) {
		struct record *rec = HTlookup(&cnid, sizeof(cnid));
		if (rec == NULL) return -1;

		*--compptr = blobptr;
		compcnt++;
		strcpy(blobptr, rec->name);
		blobptr += strlen(blobptr)+1;

		cnid = rec->parent;
	}

	bool bad = Walk9(2, fid, compcnt, (const char **)compptr, NULL/*numok*/, qids);
	if (bad) return -1;

	return (int)(unsigned char)qids[compcnt-1].type;
}

// Need a perfect UTF-8 filename match, which must therefore come from readdir
static int32_t makeCNID(int32_t parent, char *name) {
	// Big, so keep outside stack. Hack to make space for flexible array member.
	static struct record lookup = {.name={[511]=0}};

	// Already exists in our db?
	lookup.parent = parent;
	strcpy(lookup.name, name);
	int32_t *existing = HTlookup(&lookup, sizeof(struct record)+strlen(lookup.name)+1);
	if (existing != NULL) return *existing;

	// No, we must create the entry
	int32_t cnid = ++cnidCtr;
	HTinstall(&cnid, sizeof(cnid), &lookup, 4+strlen(lookup.name)+1);
	HTinstall(&lookup, 4+strlen(lookup.name)+1, &cnid, sizeof(cnid));
	return cnid;
}

static void cnidPrint(int32_t cnid) {
	char **path; uint16_t pathcnt;

	while (cnid != 2) {
		struct record *rec = HTlookup(&cnid, sizeof(cnid));
		if (rec == NULL) {
			lprintf("(DANGLING)");
			return;
		}

		lprintf("%s<-", rec->name);

		cnid = rec->parent;
	}
	lprintf("(root)");
}

// This makes it easy to have a selector return noErr without a function
static struct handler handler(unsigned short selector) {
	switch (selector & 0xf0ff) {
	case kFSMOpen: return (struct handler){NULL, extFSErr};
	case kFSMClose: return (struct handler){NULL, extFSErr};
	case kFSMRead: return (struct handler){NULL, extFSErr};
	case kFSMWrite: return (struct handler){NULL, extFSErr};
	case kFSMGetVolInfo: return (struct handler){MyGetVolInfo};
	case kFSMCreate: return (struct handler){NULL, extFSErr};
	case kFSMDelete: return (struct handler){NULL, extFSErr};
	case kFSMOpenRF: return (struct handler){NULL, extFSErr};
	case kFSMRename: return (struct handler){NULL, extFSErr};
	case kFSMGetFileInfo: return (struct handler){MyGetFileInfo};
	case kFSMSetFileInfo: return (struct handler){NULL, extFSErr};
	case kFSMUnmountVol: return (struct handler){NULL, extFSErr};
	case kFSMMountVol: return (struct handler){NULL, volOnLinErr};
	case kFSMAllocate: return (struct handler){NULL, extFSErr};
	case kFSMGetEOF: return (struct handler){NULL, extFSErr};
	case kFSMSetEOF: return (struct handler){NULL, extFSErr};
	case kFSMFlushVol: return (struct handler){NULL, noErr};
	case kFSMGetVol: return (struct handler){NULL, extFSErr};
	case kFSMSetVol: return (struct handler){NULL, extFSErr};
	case kFSMEject: return (struct handler){NULL, extFSErr};
	case kFSMGetFPos: return (struct handler){NULL, extFSErr};
	case kFSMOffline: return (struct handler){NULL, extFSErr};
	case kFSMSetFilLock: return (struct handler){NULL, extFSErr};
	case kFSMRstFilLock: return (struct handler){NULL, extFSErr};
	case kFSMSetFilType: return (struct handler){NULL, extFSErr};
	case kFSMSetFPos: return (struct handler){NULL, extFSErr};
	case kFSMFlushFile: return (struct handler){NULL, extFSErr};
	case kFSMOpenWD: return (struct handler){NULL, extFSErr};
	case kFSMCloseWD: return (struct handler){NULL, extFSErr};
	case kFSMCatMove: return (struct handler){NULL, extFSErr};
	case kFSMDirCreate: return (struct handler){NULL, extFSErr};
	case kFSMGetWDInfo: return (struct handler){NULL, extFSErr};
	case kFSMGetFCBInfo: return (struct handler){NULL, extFSErr};
	case kFSMGetCatInfo: return (struct handler){MyGetFileInfo};
	case kFSMSetCatInfo: return (struct handler){NULL, extFSErr};
	case kFSMSetVolInfo: return (struct handler){NULL, extFSErr};
	case kFSMLockRng: return (struct handler){NULL, extFSErr};
	case kFSMUnlockRng: return (struct handler){NULL, extFSErr};
	case kFSMXGetVolInfo: return (struct handler){NULL, extFSErr};
	case kFSMCreateFileIDRef: return (struct handler){NULL, extFSErr};
	case kFSMDeleteFileIDRef: return (struct handler){NULL, extFSErr};
	case kFSMResolveFileIDRef: return (struct handler){NULL, extFSErr};
	case kFSMExchangeFiles: return (struct handler){NULL, extFSErr};
	case kFSMCatSearch: return (struct handler){NULL, extFSErr};
	case kFSMOpenDF: return (struct handler){NULL, extFSErr};
	case kFSMMakeFSSpec: return (struct handler){NULL, extFSErr};
	case kFSMDTGetPath: return (struct handler){NULL, extFSErr};
	case kFSMDTCloseDown: return (struct handler){NULL, extFSErr};
	case kFSMDTAddIcon: return (struct handler){NULL, extFSErr};
	case kFSMDTGetIcon: return (struct handler){NULL, extFSErr};
	case kFSMDTGetIconInfo: return (struct handler){NULL, extFSErr};
	case kFSMDTAddAPPL: return (struct handler){NULL, extFSErr};
	case kFSMDTRemoveAPPL: return (struct handler){NULL, extFSErr};
	case kFSMDTGetAPPL: return (struct handler){NULL, extFSErr};
	case kFSMDTSetComment: return (struct handler){NULL, extFSErr};
	case kFSMDTRemoveComment: return (struct handler){NULL, extFSErr};
	case kFSMDTGetComment: return (struct handler){NULL, extFSErr};
	case kFSMDTFlush: return (struct handler){NULL, extFSErr};
	case kFSMDTReset: return (struct handler){NULL, extFSErr};
	case kFSMDTGetInfo: return (struct handler){NULL, extFSErr};
	case kFSMDTOpenInform: return (struct handler){NULL, extFSErr};
	case kFSMDTDelete: return (struct handler){NULL, extFSErr};
	case kFSMGetVolParms: return (struct handler){MyGetVolParms};
	case kFSMGetLogInInfo: return (struct handler){NULL, extFSErr};
	case kFSMGetDirAccess: return (struct handler){NULL, extFSErr};
	case kFSMSetDirAccess: return (struct handler){NULL, extFSErr};
	case kFSMMapID: return (struct handler){NULL, extFSErr};
	case kFSMMapName: return (struct handler){NULL, extFSErr};
	case kFSMCopyFile: return (struct handler){NULL, extFSErr};
	case kFSMMoveRename: return (struct handler){NULL, extFSErr};
	case kFSMOpenDeny: return (struct handler){NULL, extFSErr};
	case kFSMOpenRFDeny: return (struct handler){NULL, extFSErr};
	case kFSMGetXCatInfo: return (struct handler){NULL, extFSErr};
	case kFSMGetVolMountInfoSize: return (struct handler){NULL, extFSErr};
	case kFSMGetVolMountInfo: return (struct handler){NULL, extFSErr};
	case kFSMVolumeMount: return (struct handler){MyVolumeMount};
	case kFSMShare: return (struct handler){NULL, extFSErr};
	case kFSMUnShare: return (struct handler){NULL, extFSErr};
	case kFSMGetUGEntry: return (struct handler){NULL, extFSErr};
	case kFSMGetForeignPrivs: return (struct handler){NULL, extFSErr};
	case kFSMSetForeignPrivs: return (struct handler){NULL, extFSErr};
	case kFSMGetVolumeInfo: return (struct handler){NULL, extFSErr};
	case kFSMSetVolumeInfo: return (struct handler){NULL, extFSErr};
	case kFSMReadFork: return (struct handler){NULL, extFSErr};
	case kFSMWriteFork: return (struct handler){NULL, extFSErr};
	case kFSMGetForkPosition: return (struct handler){NULL, extFSErr};
	case kFSMSetForkPosition: return (struct handler){NULL, extFSErr};
	case kFSMGetForkSize: return (struct handler){NULL, extFSErr};
	case kFSMSetForkSize: return (struct handler){NULL, extFSErr};
	case kFSMAllocateFork: return (struct handler){NULL, extFSErr};
	case kFSMFlushFork: return (struct handler){NULL, extFSErr};
	case kFSMCloseFork: return (struct handler){NULL, extFSErr};
	case kFSMGetForkCBInfo: return (struct handler){NULL, extFSErr};
	case kFSMCloseIterator: return (struct handler){NULL, extFSErr};
	case kFSMGetCatalogInfoBulk: return (struct handler){NULL, extFSErr};
	case kFSMCatalogSearch: return (struct handler){NULL, extFSErr};
	case kFSMMakeFSRef: return (struct handler){NULL, extFSErr};
	case kFSMCreateFileUnicode: return (struct handler){NULL, extFSErr};
	case kFSMCreateDirUnicode: return (struct handler){NULL, extFSErr};
	case kFSMDeleteObject: return (struct handler){NULL, extFSErr};
	case kFSMMoveObject: return (struct handler){NULL, extFSErr};
	case kFSMRenameUnicode: return (struct handler){NULL, extFSErr};
	case kFSMExchangeObjects: return (struct handler){NULL, extFSErr};
	case kFSMGetCatalogInfo: return (struct handler){NULL, extFSErr};
	case kFSMSetCatalogInfo: return (struct handler){NULL, extFSErr};
	case kFSMOpenIterator: return (struct handler){NULL, extFSErr};
	case kFSMOpenFork: return (struct handler){NULL, extFSErr};
	case kFSMMakeFSRefUnicode: return (struct handler){NULL, extFSErr};
	case kFSMCompareFSRefs: return (struct handler){NULL, extFSErr};
	case kFSMCreateFork: return (struct handler){NULL, extFSErr};
	case kFSMDeleteFork: return (struct handler){NULL, extFSErr};
	case kFSMIterateForks: return (struct handler){NULL, extFSErr};
	default: return (struct handler){NULL, extFSErr};
	}
}
