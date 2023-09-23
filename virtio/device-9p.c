/*
Treat 9P filesystem as accessible by path only
(unfortunately wasting the find-by-CNID ability of HFS+/APFS)

Therefore we need this mapping:
(CNID) <-> (parent's CNID, name)
*/

#include <Disks.h>
#include <DriverGestalt.h>
#include <DriverServices.h>
#include <Files.h>
#include <FSM.h>
#include <Gestalt.h>
#include <LowMem.h>
#include <Memory.h>
#include <MixedMode.h>
#include <Start.h>
#include <Traps.h>

#include "hashtab.h"
#include "lprintf.h"
#include "panic.h"
#include "paramblkprint.h"
#include "patch68k.h"
#include "9p.h"
#include "timing.h"
#include "transport.h"
#include "unicode.h"
#include "universalfcb.h"
#include "virtqueue.h"

#include <stdbool.h> // leave till last, conflicts with Universal Interfaces
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define c2pstr(p, c) {uint8_t l=strlen(c); p[0]=l; memcpy(p+1, c, l);}
#define p2cstr(c, p) {uint8_t l=p[0]; memcpy(c, p+1, l); c[l]=0;}
#define pstrcpy(d, s) memcpy(d, s, 1+(unsigned char)s[0])

#define unaligned32(ptr) (((uint32_t)*(uint16_t *)(ptr) << 16) | *((uint16_t *)(ptr) + 1));

enum {
	FSID = ('9'<<8) | 'p',
	ROOTFID = 2,
	WDLO = -32767,
	WDHI = -4096,
	STACKSIZE = 32 * 1024,
};

// of a File Manager call
struct handler {
	void *func; // Unsafe magic allows different pb types
	short err; // If func==NULL then return ret
};

struct flagdqe {
	uint32_t flags; // 4 bytes of flags at neg offset
	DrvQEl dqe; // AddDrive points here
};

struct bbnames {
	unsigned char sys[16]; // "System"
	unsigned char fnd[16]; // "Finder"
	unsigned char dbg[16]; // "MacsBug"
	unsigned char dis[16]; // "Disassembler"
	unsigned char scr[16]; // "StartUpScreen"
	unsigned char app[16]; // "Finder"
	unsigned char clp[16]; // "Clipboard"
};

static OSStatus finalize(DriverFinalInfo *info);
static OSStatus initialize(DriverInitInfo *info);
static void installAndMountAndNotify(void);
static char *mkbb(OSErr (*booter)(void), struct bbnames names);
static OSErr boot(void);
static int32_t browse(uint32_t fid, int32_t cnid, const unsigned char *paspath);
static bool setPath(int32_t cnid);
static bool appendPath(const unsigned char *path);
static bool isAbs(const unsigned char *path);
static int32_t pbDirID(void *_pb);
static struct WDCBRec *findWD(short refnum);
static int walkToCNID(int32_t cnid, uint32_t fid);
static int32_t qid2cnid(struct Qid9 qid);
static void cnidPrint(int32_t cnid);
static struct DrvQEl *findDrive(short num);
static struct VCB *findVol(short num);
static char determineNumStr(void *_pb);
static char determineNum(void *_pb);
static bool visName(const char *name);
static void setDB(int32_t cnid, int32_t pcnid, const char *name);
static const char *getDBName(int32_t cnid);
static int32_t getDBParent(int32_t cnid);
static long fsCall(void *pb, long selector);
static struct handler fsHandler(unsigned short selector);
static OSErr controlStatusCall(struct CntrlParam *pb);
static struct handler controlStatusHandler(long selector);

// Single statically allocated array of path components
// UTF-8, null-terminated
// (Final component can be edited safely)
static char *pathComps[100];
static int32_t expectCNID[100];
static int pathCompCnt;
static char pathBlob[512];
static int pathBlobSize;

static unsigned long hfsTimer, browseTimer, relistTimer;
static char *stack;
static short drvrRefNum;
static unsigned long callcnt;
static struct Qid9 root;
static Handle finderwin;
static bool mounted;
static char *bootBlock;
static struct flagdqe dqe = {
	.flags = 0x00080000, // fixed disk
	.dqe = {.dQFSID = FSID},
};
static struct VCB vcb = {
	.vcbAtrb = 0x8080, // hw and sw locked
	.vcbSigWord = kHFSSigWord,
	.vcbNmFls = 1234,
	.vcbNmRtDirs = 6, // "number of directories in root" -- why?
	.vcbNmAlBlks = 0xf000,
	.vcbAlBlkSiz = 512,
	.vcbClpSiz = 512,
	.vcbNxtCNID = 16, // the first "user" cnid... we will never use this field
	.vcbFreeBks = 0xe000,
	.vcbFSID = FSID,
	.vcbFilCnt = 1,
	.vcbDirCnt = 1,
};

// Work around a ROM bug:
// If kDriverIsLoadedUponDiscovery is set, the ROM calls GetDriverDescription
// for a pointer to the global below, then frees it with DisposePtr. Padding
// the global to a positive offset within our global area defeats DisposePtr.
char BugWorkaroundExport1[] = "TheDriverDescription must not come first";

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

char BugWorkaroundExport2[] = "TheDriverDescription must not come first";

OSStatus DoDriverIO(AddressSpaceID spaceID, IOCommandID cmdID,
	IOCommandContents pb, IOCommandCode code, IOCommandKind kind) {
	OSStatus err;

	if (code <= 6 && lprintf_enable)
		lprintf("Drvr_%s", PBPrint(pb.pb, (*pb.pb).ioParam.ioTrap | 0xa000, 1));

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
	case kStatusCommand:
		err = controlStatusCall(&(*pb.pb).cntrlParam);
		break;
	case kOpenCommand:
	case kCloseCommand:
		err = noErr;
		break;
	case kReadCommand: {
		struct IOParam *param = &pb.pb->ioParam;
		param->ioActCount = param->ioReqCount;
		for (long i=0; i<param->ioReqCount; i++) {
			if (param->ioPosOffset+i < 512 && bootBlock != NULL) {
				param->ioBuffer[i] = bootBlock[i];
			} else {
				param->ioBuffer[i] = 0;
			}
		}
		err = noErr;
		break;
		}
	default:
		err = paramErr;
		break;
	}

	if (code <= 6 && lprintf_enable)
		lprintf("%s", PBPrint(pb.pb, (*pb.pb).ioParam.ioTrap | 0xa000, err));

	// Return directly from every call
	if (kind & kImmediateIOCommandKind) {
		return err;
	} else {
		return IOCommandIsComplete(cmdID, err);
	}
}

static OSStatus finalize(DriverFinalInfo *info) {
	return noErr;
}

static OSStatus initialize(DriverInitInfo *info) {
	drvrRefNum = info->refNum;
	sprintf(lprintf_prefix, ".virtio9p(%d) ", drvrRefNum);
	if (0 == RegistryPropertyGet(&info->deviceEntry, "debug", NULL, 0)) {
		lprintf_enable = 1;
	}

	lprintf("Primary init\n");

	// No need to signal FAILED if cannot communicate with device
	if (!VInit(&info->deviceEntry)) {
		lprintf("...failed VInit()\n");
		return paramErr;
	};

	// Request mount_tag in the config area
	VSetFeature(0, 1);
	if (!VFeaturesOK()) {
		lprintf("...failed VFeaturesOK()\n");
		return paramErr;
	}

	// Cannot go any further without touching virtqueues, which requires DRIVER_OK
	VDriverOK();

	HTallocate();

	// More buffers allow us to tolerate more physical mem fragmentation
	uint16_t viobufs = QInit(0, 256);
	if (viobufs < 2) {
		lprintf("...failed QInit()\n");
		return paramErr;
	}
	QInterest(0, 1);

	int err9;
	if ((err9 = Init9(viobufs)) != 0) {
		return paramErr;
	}

	if ((err9 = Attach9(ROOTFID, (uint32_t)~0 /*auth=NOFID*/, "", "", 0, &root)) != 0) {
		return paramErr;
	}

	dqe.dqe.dQDrive=4; // lower numbers reserved
	while (findDrive(dqe.dqe.dQDrive) != NULL) dqe.dqe.dQDrive++;
	AddDrive(drvrRefNum, dqe.dqe.dQDrive, &dqe.dqe);

	// Is the File Manager actually up yet?
	if ((long)GetVCBQHdr()->qHead != -1 && (long)GetVCBQHdr()->qHead != 0) {
		lprintf("FM up: mounting now\n");
		installAndMountAndNotify();
	} else {
		// Bootable filesystem?
		// TODO: this needs to check for a ZSYS and FNDR file
		int32_t systemFolder = browse(3 /*fid*/, 2 /*cnid*/, "\pSystem Folder");
		if (systemFolder > 0) {
			lprintf("FM down, bootable System Folder: I am the boot drive\n");
			vcb.vcbFndrInfo[0] = systemFolder;

			struct bbnames bbn = {};
			memcpy(bbn.sys, "\pSystem", 7);
			memcpy(bbn.fnd, "\pFinder", 7);
			memcpy(bbn.dbg, "\pMacsBug", 8);
			memcpy(bbn.dis, "\pDisassembler", 13);
			memcpy(bbn.clp, "\pClipboard", 10);

			bootBlock = mkbb(boot, bbn);

			SetTimeout(1); // give up on the default disk quickly
		} else {
			lprintf("FM down, not bootable\n");
		}
	}

	lprintf("...primary init succeeded\n");
	return noErr;
}

// Run this only when the File Manager is up
static void installAndMountAndNotify(void) {
	static bool alreadyUp;
	if (alreadyUp) return;
	alreadyUp = true;

	lprintf("Hooking ToExtFS\n");

	// External filesystems need a big stack, and they can't
	// share the FileMgr stack because of reentrancy problems
	stack = NewPtrSysClear(STACKSIZE);
	if (stack == NULL) panic("failed extfs stack allocation");

	Patch68k(
		0x3f2,      // ToExtFS:
		"0cb8 %l 03ee" //    cmp.l   #vcb,ReqstVol
		"6706"      //       beq.s   yes
		"0c28 000f 0007" //  cmp.b   #_MountVol&255,ioTrap(a0)
		"6642"      //       bne.s   no
		"2f38 0110" // yes:  move.l  StkLowPt,-(sp)
		"42b8 0110" //       clr.l   StkLowPt
		"23cf %l"   //       move.l  sp,stack-4
		"2e7c %l"   //       move.l  #stack-4,sp
		"2f00"      //       move.l  d0,-(sp)          ; restore d0 only if hook fails
		"48e7 60e0" //       movem.l d1-d2/a0-a2,-(sp)
		"2f00"      //       move.l  d0,-(sp)
		"2f08"      //       move.l  a0,-(sp)
		"4eb9 %l"   //       jsr     fsCall
		"504f"      //       addq    #8,sp
		"4cdf 0706" //       movem.l (sp)+,d1-d2/a0-a2
		"0c40 ffc6" //       cmp.w   #extFSErr,d0
		"670a"      //       beq.s   punt
		"584f"      //       addq    #4,sp             ; hook success: don't restore d0
		"2e57"      //       move.l  (sp),sp           ; unswitch stack
		"21df 0110" //       move.l  (sp)+,StkLowPt
		"4e75"      //       rts
		"201f"      // punt: move.l  (sp)+,d0          ; hook failure: restore d0 for next FS
		"2e57"      //       move.l  (sp),sp           ; unswitch stack
		"21df 0110" //       move.l  (sp)+,StkLowPt
		"4ef9 %o",  // no:   jmp     originalToExtFS

		&vcb,
		stack + STACKSIZE - 100,
		stack + STACKSIZE - 100,
		NewRoutineDescriptor((ProcPtr)fsCall,
			kCStackBased
				| STACK_ROUTINE_PARAMETER(1, kFourByteCode)
				| STACK_ROUTINE_PARAMETER(2, kFourByteCode)
				| RESULT_SIZE(kFourByteCode),
			GetCurrentISA())
	);

	struct IOParam pb = {.ioVRefNum = dqe.dqe.dQDrive};
	PBMountVol((void *)&pb);

	PostEvent(diskEvt, dqe.dqe.dQDrive);
}

// Make (in the heap) a single 512-byte block that the ROM will boot from
static char *mkbb(OSErr (*booter)(void), struct bbnames names) {
	char *bb = NewPtrSysClear(512);
	if (!bb) return NULL;

	// Larry Kenyon's magic number, BRA routine descriptor, executable flavour
	memcpy(bb, (const short []){0x4c4b, 0x6000, 0x0086, 0x4418}, 8);

	// List of 16-byte names: "System", "Finder" etc
	memcpy(bb + 0x0a, &names, sizeof names);

	RoutineDescriptor booter68 = BUILD_ROUTINE_DESCRIPTOR(
		kCStackBased
			| STACK_ROUTINE_PARAMETER(1, kFourByteCode)
			| RESULT_SIZE(kTwoByteCode),
		booter);
	memcpy(bb + 0x8a, &booter68, sizeof booter68);

	return bb;
}

// Emulate a System 7-style boot block ('boot' 1 resource):
// Load and run a 'boot' 2 resource in the System file
// Not worth checking return values: if boot fails then the reason is clear enough
static OSErr boot(void) {
	lprintf("Emulating boot block\n");

	// Populate low memory from the declarative part of the boot block.
	// (We use the copy from our own globals. There is a copy A5+0x270.)
	memcpy((void *)0xad8, bootBlock + 10, 16); // SysResName
	memcpy((void *)0x2e0, bootBlock + 26, 16); // FinderName
	memcpy((void *)0x970, bootBlock + 106, 16); // ScrapTag
	*(void **)0x96c = (void *)0x970; // ScrapName pointer --> ScrapTag string

	CallOSTrapUniversalProc(GetOSTrapAddress(_InitEvents), 0x33802, _InitEvents, 20);

	CallOSTrapUniversalProc(GetOSTrapAddress(_InitFS), 0x33802, _InitFS, 10);

	installAndMountAndNotify();

	// Is the System Folder not the root of the disk? (It usually isn't.)
	// Make it a working directory and call that the default (fake) volume.
	if (vcb.vcbFndrInfo[0] > 2) {
		struct WDPBRec pb = {
			.ioVRefNum = vcb.vcbVRefNum,
			.ioWDDirID = vcb.vcbFndrInfo[0],
			.ioWDProcID = 'ERIK',
		};
		PBOpenWDSync((void *)&pb);
		PBSetVolSync((void *)&pb);
	}

	// Next startup stage
	InitResources();
	Handle boot2hdl = GetResource('boot', 2);

	// boot 2 resource requires a3=handle and a4=startup app dirID
	// movem.l (sp),a0/a3/a4; move.l (a3),-(sp); rts
	static short thunk[6] = {0x4CD7, 0x1900, 0x2F13, 0x4E75};
	BlockMove(thunk, thunk, sizeof thunk);

	// Call boot 2, never to return
	lprintf("...starting System file\n");
	CallUniversalProc(
		(void *)thunk,
		kCStackBased
			| STACK_ROUTINE_PARAMETER(1, kFourByteCode)
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode),
		boot2hdl,
		vcb.vcbFndrInfo[0] // a4 has in the past been the startup app dirID??
	);
}

static OSErr fsMountVol(struct IOParam *pb) {
	// This is the only call that needs to check that our volume is the right one
	if (pb->ioVRefNum != dqe.dqe.dQDrive) return extFSErr;

	if (mounted) return volOnLinErr;

	// Read mount_tag from config space into a C string
	char name[128] = {};
	long nameLen = *(unsigned char *)VConfig + 0x100 * *(unsigned char *)(VConfig+1);
	if (nameLen > 127) nameLen = 127;
	memcpy(name, VConfig + 2, nameLen);
	mr27name(vcb.vcbVN, name); // and convert to short Mac Roman pascal string

	setDB(2, 1, name);

	finderwin = NewHandleSysClear(2);

	vcb.vcbDrvNum = dqe.dqe.dQDrive;
	vcb.vcbDRefNum = drvrRefNum;
	vcb.vcbVRefNum = -1;

	while (findVol(vcb.vcbVRefNum) != NULL) vcb.vcbVRefNum--;

	lprintf("refnums are driver=%d drive=%d volume=%d\n",
		drvrRefNum, dqe.dqe.dQDrive, vcb.vcbVRefNum);

	if (GetVCBQHdr()->qHead == NULL) {
		LMSetDefVCBPtr((Ptr)&vcb);
		*(short *)0x384 = vcb.vcbVRefNum; // DefVRefNum

		memcpy(findWD(0),
			&(struct WDCBRec){.wdVCBPtr=&vcb, .wdDirID = 2},
			16);
	}

	Enqueue((QElemPtr)&vcb, GetVCBQHdr());

	mounted = true;
// 	if (vcb.vcbVRefNum & 1)
// 		strcpy(lprintf_prefix, "                              ");
	return noErr;
}

// TODO: search the FCB array for open files on this volume, set bit 6
// TODO: when given a WD refnum, return directory valence as the file count
// TODO: fake used/free alloc blocks (there are limits depending on H bit)
static OSErr fsGetVolInfo(struct HVolumeParam *pb) {
	if (pb->ioVRefNum < -256) {
		panic("GetVolInfo on a WD: not implemented");
	}

	pb->ioVCrDate = vcb.vcbCrDate;
	pb->ioVLsMod = vcb.vcbLsMod; // old field ioVLsBkUp is DIFFERENT
	pb->ioVAtrb = vcb.vcbAtrb;
	pb->ioVNmFls = vcb.vcbNmFls;
	pb->ioVBitMap = vcb.vcbVBMSt; // old field ioVDirSt is DIFFERENT
	pb->ioAllocPtr = vcb.vcbAllocPtr; // old field ioVBlLn is DIFFERENT
	pb->ioVNmAlBlks = vcb.vcbNmAlBlks;
	pb->ioVAlBlkSiz = vcb.vcbAlBlkSiz;
	pb->ioVClpSiz = vcb.vcbClpSiz;
	pb->ioAlBlSt = vcb.vcbAlBlSt;
	pb->ioVNxtCNID = vcb.vcbNxtCNID; // old ioVNxtFNum field is equivalent
	pb->ioVFrBlk = vcb.vcbFreeBks;

	if ((char *)LMGetDefVCBPtr() == (char *)&vcb)
		pb->ioVAtrb |= 1<<5;

	if (pb->ioVRefNum == 0) {
		pb->ioVRefNum = vcb.vcbVRefNum;
	}

	if (pb->ioNamePtr != NULL) {
		pstrcpy(pb->ioNamePtr, vcb.vcbVN);
	}

	if (pb->ioTrap & 0x200) { // ***H***GetVolInfo
		pb->ioVRefNum = vcb.vcbVRefNum; // return irrespective of original val
		pb->ioVSigWord = vcb.vcbSigWord;
		pb->ioVDrvInfo = vcb.vcbDrvNum;
		pb->ioVDRefNum = vcb.vcbDRefNum;
		pb->ioVFSID = vcb.vcbFSID;
		pb->ioVBkUp = vcb.vcbVolBkUp;
		pb->ioVSeqNum = vcb.vcbVSeqNum;
		pb->ioVWrCnt = vcb.vcbWrCnt;
		pb->ioVFilCnt = vcb.vcbFilCnt;
		pb->ioVDirCnt = vcb.vcbDirCnt;
		memcpy(&pb->ioVFndrInfo, &vcb.vcbFndrInfo, 32);
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

static OSErr fsGetVolParms(struct HIOParam *pb) {
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

static OSErr fsGetFileInfo(struct HFileInfo *pb) {
	enum {MYFID=3};

	bool flat = (pb->ioTrap&0xf2ff) == 0xa00c; // GetFileInfo without "H"
	bool longform = (pb->ioTrap&0x00ff) == 0x0060; // GetCatInfo

	int idx = pb->ioFDirIndex;
	if (idx<0 && !longform) idx=0; // make named GetFInfo calls behave right

	int32_t cnid = pbDirID(pb);

	if (idx > 0) {
		lprintf("GCI index mode\n");

		// Software commonly calls with index 1, 2, 3 etc
		// Cache Readdir9 to avoid quadratically relisting the directory per-call
		// An improvement might be to have multiple caches
		static char scratch[2048];
		static long lastCNID;
		static int lastIdx;
		enum {LISTFID=22};

		// Invalidate the cache (by setting lastCNID to 0)
		if (cnid != lastCNID || idx <= lastIdx) {
			lastCNID = 0;
			lastIdx = 0;
			if (walkToCNID(cnid, LISTFID) < 0) return fnfErr;
			if (Lopen9(LISTFID, O_RDONLY|O_DIRECTORY, NULL, NULL)) return permErr;
			InitReaddir9(LISTFID, scratch, sizeof scratch);
			lastCNID = cnid;
		}

		char name[512];
		struct Qid9 qid;

		// Fast-forward
		while (lastIdx < idx) {
			char type;
			int err = Readdir9(scratch, &qid, &type, name);

			if (err) {
				lastCNID = 0;
				Clunk9(LISTFID);
				return fnfErr;
			}

			// GetFileInfo/HGetFileInfo ignores child directories
			// Note that Rreaddir does return a qid, but the type field of that
			// qid is unpopulated. So we use the Linux-style type byte instead.
			if ((!longform && type == 4) || !visName(name)) {
				continue;
			}

			lastIdx++;
		}

		int32_t childcnid = qid2cnid(qid);
		setDB(childcnid, cnid, name);
		cnid = childcnid;

		walkToCNID(cnid, MYFID);

	} else if (idx == 0) {
		lprintf("GCI name mode\n");
		cnid = browse(MYFID, cnid, pb->ioNamePtr);
		if (cnid < 0) return cnid;
	} else {
		lprintf("GCI ID mode\n");
		cnid = browse(MYFID, cnid, "\p");
		if (cnid < 0) return cnid;
	}

	// MYFID and cnid now both valid

	// Here's some tricky debugging
	if (lprintf_enable) {
		lprintf("GCI found "); cnidPrint(cnid); lprintf("\n");
	}

	// Return the filename
	if (idx!=0 && pb->ioNamePtr!=NULL) {
		mr31name(pb->ioNamePtr, getDBName(cnid));
	}

	struct Stat9 stat;
	if (Getattr9(MYFID, STAT_ALL, &stat)) return permErr;

	pb->ioDirID = cnid; // alias ioDrDirID
	pb->ioFRefNum = 0;

	// This is outside the GetFileInfo block: anticipate catastrophe
	if (longform) {
		pb->ioFlParID = getDBParent(cnid); // alias ioDrDirID
	}

	if (stat.qid.type & 0x80) { // directory
		if (!longform) return fnfErr; // GetFileInfo doesn't do hierarchical

		int n=0;
		char childname[512];
		Lopen9(MYFID, O_RDONLY, NULL, NULL); // iterate
		char scratch[4096];
		InitReaddir9(MYFID, scratch, sizeof scratch);
		while (Readdir9(scratch, NULL, NULL, childname) == 0) {
			if (!visName(childname)) continue;
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
		struct Stat9 rstat = {};
		char rname[512];
		sprintf(rname, "%s.rsrc", getDBName(cnid));
		uint16_t oksteps = 0;

		Walk9(MYFID, 19, 2, (const char *[]){"..", rname}, &oksteps, NULL);
		if (oksteps == 2) {
			Getattr9(19, STAT_SIZE, &rstat);
		}

		pb->ioFlAttrib = 0;
		pb->ioACUser = 0;
		pb->ioFlFndrInfo = (struct FInfo){0};
		pb->ioFlStBlk = 0;
		pb->ioFlLgLen = stat.size;
		pb->ioFlPyLen = (stat.size + 511) & -512;
		pb->ioFlRStBlk = 0;
		pb->ioFlRLgLen = rstat.size;
		pb->ioFlRPyLen = (rstat.size + 511) & -512;
		pb->ioFlCrDat = 0;
		pb->ioFlMdDat = 0;
		if (longform) {
			pb->ioFlBkDat = 0;
			pb->ioFlXFndrInfo = (struct FXInfo){0};
			pb->ioFlClpSiz = 0;
		}

		char iname[512];
		sprintf(iname, "%s.idump", getDBName(cnid));
		oksteps = 0;

		Walk9(MYFID, 19, 2, (const char *[]){"..", iname}, &oksteps, NULL);
		if (oksteps == 2) {
			if (!Lopen9(19, O_RDONLY, NULL, NULL)) {
				Read9(19, &pb->ioFlFndrInfo, 0, 8, NULL); // don't care about actual count
			}
		}
	}

	return noErr;
}

static OSErr fsSetVol(struct HFileParam *pb) {
	struct VCB *setDefVCBPtr;
	short setDefVRefNum;
	struct WDCBRec setDefWDCB;

	if (pb->ioTrap & 0x200) {
		// HSetVol: any directory is fair game,
		// so check that the path exists and is really a directory
		int32_t cnid = browse(11, pbDirID(pb), pb->ioNamePtr);
		if (cnid < 0) return cnid;
		struct Stat9 stat;
		if (Getattr9(11, 0, &stat)) return permErr;
		if ((stat.qid.type & 0x80) == 0) return dirNFErr;
		Clunk9(11);

		setDefVCBPtr = &vcb;
		setDefVRefNum = vcb.vcbVRefNum;
		setDefWDCB = (struct WDCBRec){
			.wdVCBPtr=&vcb,
			.wdDirID=cnid
		};
	} else {
		// SetVol: only the root or a Working Directory is allowed,
		// and in either case the directory is known already to exist
		if (determineNumStr(pb) == 'w') { // Working Directory
			setDefVCBPtr = &vcb;
			setDefVRefNum = pb->ioVRefNum;
			setDefWDCB = (struct WDCBRec){
				.wdVCBPtr=&vcb,
				.wdDirID=findWD(pb->ioVRefNum)->wdDirID
			};
		} else { // Root (via path, volume number or drive number)
			setDefVCBPtr = &vcb;
			setDefVRefNum = vcb.vcbVRefNum;
			setDefWDCB = (struct WDCBRec){
				.wdVCBPtr=&vcb,
				.wdDirID=2
			};
		}
	}

	// Set super secret globals
	*(short *)0x352 = (long)setDefVCBPtr >> 16;
	*(short *)0x354 = (long)setDefVCBPtr;
	*(short *)0x384 = setDefVRefNum;
	memcpy(findWD(0), &setDefWDCB, sizeof setDefWDCB);
	return noErr;
}

static OSErr fsMakeFSSpec(struct HIOParam *pb) {
	struct FSSpec *spec = (struct FSSpec *)pb->ioMisc;

	int32_t cnid = pbDirID(pb);
	cnid = browse(10, cnid, pb->ioNamePtr);
	if (cnid > 0) {
		// The target exists
		if (cnid == 2) {
			spec->vRefNum = vcb.vcbVRefNum;
			spec->parID = 2;
			spec->name[0] = 0;
		} else {
			spec->vRefNum = vcb.vcbVRefNum;
			spec->parID = getDBParent(cnid);
			mr31name(spec->name, getDBName(cnid));
		}

		return noErr;
	}

	if (!pb->ioNamePtr) return dirNFErr;

	// The target doesn't (yet) exist
	// TODO this is messy
	int leafstart = pb->ioNamePtr[0];
	int leaflen = 0;
	if (leafstart && pb->ioNamePtr[leafstart] == ':') leafstart--;
	while (leafstart && pb->ioNamePtr[leafstart] != ':') {
		leafstart--;
		leaflen++;
	}

	if (leaflen == 0) return dirNFErr;

	unsigned char path[256], leaf[256];
	path[0] = leafstart;
	memcpy(path+1, pb->ioNamePtr+1, leafstart);
	leaf[0] = leaflen;
	memcpy(leaf+1, pb->ioNamePtr+1+leafstart, leaflen);

	cnid = pbDirID(pb);
	cnid = browse(10, cnid, path);
	if (cnid < 0) return dirNFErr; // return cnid;

	spec->vRefNum = vcb.vcbVRefNum;
	spec->parID = cnid;
	pstrcpy(spec->name, leaf);

	return fnfErr;
}

static OSErr fsOpen(struct HFileParam *pb) {
// 	memcpy(LMGetCurrentA5() + 0x278, "CB", 2); // Force early MacsBug, TODO absolutely will crash

	// OpenSync is allowed to move memory
 	if ((pb->ioTrap & 0x200) == 0) HTallocate();

	pb->ioFRefNum = 0;

	bool rfork = (pb->ioTrap & 0xff) == 0x0a;

	short refn;
	struct FCBRec *fcb;
	if (UnivAllocateFCB(&refn, &fcb) != noErr) return tmfoErr;

	uint32_t fid = 32 + refn;
	Clunk9(fid); // preemptive

	int32_t cnid = pbDirID(pb);
	cnid = browse(fid, cnid, pb->ioNamePtr);
	if (cnid < 0) return cnid;

	if (rfork) {
		char rname[512];
		sprintf(rname, "%s.rsrc", getDBName(cnid));

		uint16_t oksteps = 0;
		Walk9(fid, fid, 2, (const char *[]){"..", rname}, &oksteps, NULL);
		if (oksteps != 2) return fnfErr;
	}

	struct Stat9 stat;
	if (Getattr9(fid, 0, &stat)) return permErr;
	if (stat.qid.type & 0x80) return fnfErr; // better not be a folder!

	if (Lopen9(fid, O_RDONLY, NULL, NULL))
		return permErr;

	*fcb = (struct FCBRec){
		.fcbFlNm = cnid,
		.fcbFlags = (rfork ? 2 : 0) | 0x20, // locked ?resource
		.fcbTypByt = 0, // MFS only
		.fcbSBlk = 99, // free for our use
		.fcbEOF = stat.size,
		.fcbPLen = (stat.size + 511) & -512,
		.fcbCrPs = 0,
		.fcbVPtr = &vcb,
		.fcbBfAdr = NULL, // reserved
		.fcbFlPos = 0, // free for own use
		.fcbClmpSize = 512,
		.fcbBTCBPtr = NULL, // reserved
		.fcbExtRec = {}, // own use
		.fcbFType = 'APPL',
		.fcbCatPos = 0, // own use
		.fcbDirID = getDBParent(cnid),
	};

	mr31name(fcb->fcbCName, getDBName(cnid));

	pb->ioFRefNum = refn;

	return noErr;
}

static OSErr fsGetEOF(struct IOParam *pb) {
	struct FCBRec *fcb;
	if (UnivResolveFCB(pb->ioRefNum, &fcb))
		return paramErr;

	pb->ioMisc = (Ptr)fcb->fcbEOF;

	return noErr;
}

// File Manager has populated the PB for us
static OSErr fsGetFCBInfo(struct FCBPBRec *pb) {
	if (pb->ioFCBVRefNum != vcb.vcbVRefNum) return extFSErr;
	return noErr;
}

static OSErr fsClose(struct IOParam *pb) {
	struct FCBRec *fcb;
	if (UnivResolveFCB(pb->ioRefNum, &fcb))
		return paramErr;

	Clunk9(32 + pb->ioRefNum);
	fcb->fcbFlNm = 0;

	return noErr;
}

static OSErr fsRead(struct IOParam *pb) {
	if ((pb->ioTrap & 0xa8ff) == (_SetFPos & 0xa8ff))
		pb->ioReqCount = 0;

	if (pb->ioReqCount < 0) return paramErr;

	struct FCBRec *fcb;
	if (UnivResolveFCB(pb->ioRefNum, &fcb))
		return paramErr;

	char seek = pb->ioPosMode & 3;

	if (seek == fsAtMark) {
		// leave fcb->fcbCrPs alone
	} else if (seek == fsFromStart) {
		fcb->fcbCrPs = pb->ioPosOffset;
	} else if (seek == fsFromLEOF) {
		fcb->fcbCrPs = fcb->fcbEOF + pb->ioPosOffset;
	} else if (seek == fsFromMark) {
		fcb->fcbCrPs = fcb->fcbCrPs + pb->ioPosOffset;
	}

	pb->ioActCount = 0;

	if (fcb->fcbCrPs < 0) {
		fcb->fcbCrPs = 0;
		return posErr;
	}

	// Request the host
	while (pb->ioActCount < pb->ioReqCount) {
		uint32_t want = pb->ioReqCount - pb->ioActCount;
		if (want > Max9) want = Max9;

		uint32_t got = 0;
		int err = Read9(32 + pb->ioRefNum, pb->ioBuffer + pb->ioActCount, fcb->fcbCrPs, want, &got);

		if (err) return ioErr;

		pb->ioActCount += got;
		fcb->fcbCrPs += got;
		pb->ioPosOffset = fcb->fcbCrPs;

		if (got < want) break;
	}

	return noErr;
}

// "Working directories" are a compatibility shim for apps expecting flat disks:
// a table of fake volume reference numbers that actually refer to directories.
static OSErr fsOpenWD(struct WDParam *pb) {
	int32_t cnid = pbDirID(pb);
	cnid = browse(12, cnid, pb->ioNamePtr);
	if (cnid < 0) return cnid;

	// The root: no need to create a WD, just return the volume's refnum
	if (cnid == 2) {
		pb->ioVRefNum = vcb.vcbVRefNum;
		return noErr;
	}

	// Ensure it's actually a directory
	struct Stat9 stat;
	if (Getattr9(12, STAT_ALL, &stat)) return permErr;
	if ((stat.qid.type & 0x80) == 0) return dirNFErr;

	// A copy of the desired WDCB (a straight comparison is okay)
	struct WDCBRec wdcb = {
		.wdVCBPtr = &vcb,
		.wdDirID = cnid,
		.wdProcID = pb->ioWDProcID,
	};

	short tablesize = *(short *)unaligned32(0x372); // int at start of table
	enum {SKIPWD = 2 + 2 * sizeof (struct WDCBRec)}; // never use 1st/2nd WDCB

	// Search for already-open WDCB
	for (short ref=WDLO+SKIPWD; ref<WDLO+tablesize; ref+=16) {
		if (!memcmp(findWD(ref), &wdcb, sizeof wdcb)) {
			pb->ioVRefNum = ref;
			return noErr;
		}
	}

	// Search for free WDCB
	for (short ref=WDLO+SKIPWD; ref<WDLO+tablesize; ref+=16) {
		if (findWD(ref)->wdVCBPtr == NULL) {
			memcpy(findWD(ref), &wdcb, sizeof wdcb);
			pb->ioVRefNum = ref;
			return noErr;
		}
	}

	return tmwdoErr;
}

static int32_t browse(uint32_t fid, int32_t cnid, const unsigned char *paspath) {
	TIMEFUNC(browseTimer);

	if (paspath == NULL) paspath = "";

	if (isAbs(paspath)) {
		pathCompCnt = 0;
		pathBlobSize = 0;
	} else {
		if (setPath(cnid)) return dirNFErr;
	}

	if (appendPath(paspath)) return bdNamErr;

	lprintf("Browsing for /");
	const char *suffix = "/";
	for (int i=0; i<pathCompCnt; i++) {
		lprintf(i<pathCompCnt-1 ? "%s/" : "%s", pathComps[i]);
	}
	lprintf("\n");

	// Fast case: root only
	if (pathCompCnt == 0) {
		Walk9(ROOTFID, fid, 0, NULL, NULL, NULL); // dupe shouldn't fail
		return 2;
	}

	struct Qid9 qidarray[100] = {root};
	struct Qid9 *qids = qidarray + 1; // so that root is index -1
	int progress = 0;
	uint32_t tip = ROOTFID; // as soon as a Walk9 succeeds, this equals fid

	while (progress < pathCompCnt) {
		// The aim of a loop iteration is to advance "progress"
		// (an index into pathComps) by up to 16 steps.
		// The complexity is mainly in the error handling (Tolstoyan).

		uint16_t curDepth = progress;
		uint16_t tryDepth = pathCompCnt;
		if (tryDepth > curDepth+16) tryDepth = curDepth+16; // a 9P protocol limitation

		uint16_t numOK = 0;
		Walk9(tip, fid, tryDepth-curDepth, (const char **)pathComps+curDepth, &numOK, qids+curDepth);
		// cast is unfortunate... values won't change while Walk9 is running

		// The call fully succeeded, so fid does indeed point where requested
		// (if only a lesser number of steps succeeded, fid didn't move)
		if (curDepth+numOK == tryDepth) {
			curDepth = tryDepth;
			tip = fid;
		}

		// Some of the inodes might be wrong though: discard these
		int16_t keepDepth = curDepth;

		// Discard components that have the "wrong" CNID
		for (int i=progress; i<keepDepth; i++) {
			if (expectCNID[i] != 0) {
				if (expectCNID[i] != qid2cnid(qids[i])) {
					keepDepth = i;
					break;
				}
			}
		}

		// Point tip to the final correct path member
		if (curDepth > keepDepth) {
			const char *const dotDot[] = {
				"..", "..", "..", "..", "..", "..", "..", "..",
				"..", "..", "..", "..", "..", "..", "..", ".."};

			Walk9(tip, fid, curDepth-keepDepth, dotDot, NULL, NULL);
			tip = fid;
			curDepth = keepDepth;
		} else if (curDepth > keepDepth) {
			Walk9(tip, fid, keepDepth-curDepth, (const char **)pathComps+progress, NULL, NULL);
			// again an unfortunate cast
			// assume it succeeded...
			tip = fid;
			curDepth = keepDepth;
		}

		// Put confirmed path components in the database
		for (int i=progress; i<curDepth; i++) {
			// If this was a CNID component, then we know its details well
			if (expectCNID[i] == 0) {
				setDB(qid2cnid(qids[i]), qid2cnid(qids[i-1]), pathComps[i]);
			}
		}

		// There has been a lookup failure...
		// Do an exhaustive directory search to resolve it
		if (curDepth < tryDepth) {
			// Are we looking for a name match, or a number match?
			int32_t wantCNID = expectCNID[curDepth];
			const char *wantName = pathComps[curDepth];

			// If there is no chance of the name matching then we might fail here:
			// (lookup is by name and not CNID)
			// AND
			// (fs case insensitive OR no letters in name)
			// AND
			// (fs norm insensitive OR no accents in name)
			// AND
			// (name is not mangled for length)

			// Exhaustive directory listing
			char scratch[4096];
			Walk9(tip, 27, 0, NULL, NULL, NULL); // dupe shouldn't fail
			if (Lopen9(27, O_RDONLY|O_DIRECTORY, NULL, NULL)) return fnfErr;
			InitReaddir9(27, scratch, sizeof scratch);

			int err;
			struct Qid9 qid;
			char type;
			char filename[512];
			while ((err=Readdir9(scratch, &qid, &type, filename)) == 0) {
				if (wantCNID) {
					// Check for a number match
					if (qid2cnid(qid) == wantCNID) {
						break;
					}
				} else {
					// Check for a name match
					// TODO: fuzzy filename comparison
				}
			}
			Clunk9(27);

			if (err != 0) return fnfErr;

			if (Walk9(tip, fid, 1, (const char *[]){filename}, NULL, NULL))
				return fnfErr;
			tip = fid;

			qids[curDepth] = qid;
			setDB(qid2cnid(qid), qid2cnid(qids[curDepth-1]), filename);

			curDepth++;
		}

		progress = curDepth;
	}

	return qid2cnid(qids[pathCompCnt-1]);
}

// Panics if the CNID is invalid... bad interface
static bool setPath(int32_t cnid) {
	int nbytes = 0;
	int npath = 0;

	// Preflight: number of components and their total length
	for (int32_t i=cnid; i!=2; i=getDBParent(i)) {
		if (i == 0) return true; // bad cnid
		nbytes += strlen(getDBName(i)) + 1;
		npath++;
	}

	pathBlobSize = nbytes;
	pathCompCnt = npath;

	for (int32_t i=cnid; i!=2; i=getDBParent(i)) {
		npath--;
		expectCNID[npath] = i;
		const char *name = getDBName(i);
		nbytes -= strlen(name) + 1;
		pathComps[npath] = strcpy(pathBlob + nbytes, name);
	}

	return false;
}

// Make a host-friendly array of path components (which can be dot-dot)
// Returns data in own static storage, invalidated by next call
// It is okay to append things to the final path component
static bool appendPath(const unsigned char *path) {
	// Divide path components so each is either:
	// [^:]*:
	// [^:]*$
	// So an empty component conveniently corresponds with dot-dot

	const unsigned char *component = path + 1;
	const unsigned char *limit = path + 1 + path[0];

	// Preprocess path: remove disk name (we know it implicitly)
	if (isAbs(path)) {
		while (component[0] != ':') {
			component++;
		}

		// Also erase setPath (because this is not relative to a know CNID)
		pathCompCnt = 0;
		pathBlobSize = 0;
	}

	// Preprocess path: remove leading colon (means "relative path" not dot-dot)
	if (component != limit && component[0] == ':') {
		component++;
	}

	// Component conversion loop
	int len = -1;
	while ((component += len + 1) < limit) {
		len = 0;
		while (component + len < limit && component[len] != ':') len++;

		if (pathCompCnt >= sizeof pathComps/sizeof *pathComps) return true; // oom

		expectCNID[pathCompCnt] = 0;
		pathComps[pathCompCnt] = pathBlob + pathBlobSize;
		pathCompCnt++;

		if (len == 0) {
			strcpy(pathBlob + pathBlobSize, "..");
			pathBlobSize += 3;
		} else {
			for (int i=0; i<len; i++) {
				long bytes = utf8char(component[i]);
				if (bytes == '/') bytes = ':';
				do {
					if (pathBlobSize >= sizeof pathBlob) return true; // oom
					pathBlob[pathBlobSize++] = bytes;
					bytes >>= 8;
				} while (bytes);
			}
			if (pathBlobSize >= sizeof pathBlob) return true; // oom
			pathBlob[pathBlobSize++] = 0;
		}
	}

	return false;
}

static bool isAbs(const unsigned char *path) {
	unsigned char *firstColon = memchr(path+1, ':', path[0]);
	return (firstColon != NULL && firstColon != path+1);
}

static int32_t pbDirID(void *_pb) {
	struct HFileParam *pb = _pb;

	// HFSDispatch or another hierarchical call: use dirID if nonzero
	if ((pb->ioTrap & 0xff) == 0x60 || (pb->ioTrap & 0x200) != 0) {
		if (pb->ioDirID != 0) {
			return pb->ioDirID;
		}
	}

	// Is it a WDCB?
	if (pb->ioVRefNum <= WDHI || pb->ioVRefNum == 0) {
		struct WDCBRec *wdcb = findWD(pb->ioVRefNum);
		if (wdcb) return wdcb->wdDirID;
	}

	// It's just the root
	return 2;
}

// Validate WDCB refnum and return the structure.
// A refnum of zero refers to the "current directory" WDCB
// Sadly the blocks are *always* non-4-byte-aligned
static struct WDCBRec *findWD(short refnum) {
	void *table = (void *)unaligned32(0x372);

	int16_t tblSize = *(int16_t *)table;
	int16_t offset = refnum ? refnum-WDLO : 2;

	if (offset>=2 && offset<tblSize && (offset%16)==2) {
		return table+offset;
	} else {
		return NULL;
	}
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
		*--compptr = blobptr;
		compcnt++;
		strcpy(blobptr, getDBName(cnid));
		blobptr += strlen(blobptr)+1;

		cnid = getDBParent(cnid);
	}

	bool bad = Walk9(ROOTFID, fid, compcnt, (const char **)compptr, NULL/*numok*/, qids);
	if (bad) return -1;

	return (int)(unsigned char)qids[compcnt-1].type;
}

// Remember that Qemu lets the qid path change between boots
// TODO: make folder and file CNIDs different
// (problem is, the qid type field from Readdir9 is nonsense)
static int32_t qid2cnid(struct Qid9 qid) {
	if (qid.path == root.path) return 2;
	int32_t cnid = 0; // needs to be positive (I reserve negative for error codes)
	cnid ^= (0x7fffffffULL & qid.path);
	cnid ^= ((0x3fffffff80000000ULL & qid.path) >> 31);
	cnid ^= ((0xc000000000000000ULL & qid.path) >> 40); // don't forget the upper two bits
	if (cnid < 16) cnid += 0x12342454; // low numbers reserved for system
	return cnid;
}

static void cnidPrint(int32_t cnid) {
	if (!lprintf_enable) return;

	char big[512];
	int remain = sizeof big;

	while (cnid != 2) {
		const char *name = getDBName(cnid);
		int nsize = strlen(name);
		if (remain < nsize+1) break;

		remain -= nsize;
		memcpy(big + remain, name, nsize);
		big[--remain] = '/';

		cnid = getDBParent(cnid);
	}

	lprintf("%.*s", sizeof big - remain, big + remain);
}

static struct DrvQEl *findDrive(short num) {
	for (struct DrvQEl *i=(struct DrvQEl *)GetDrvQHdr()->qHead;
		i!=NULL;
		i=(struct DrvQEl *)i->qLink
	) {
		if (i->dQDrive == num) return i;
	}
	return NULL;
}

static struct VCB *findVol(short num) {
	for (struct VCB *i=(struct VCB *)GetVCBQHdr()->qHead;
		i!=NULL;
		i=(struct VCB *)i->qLink
	) {
		if (i->vcbVRefNum == num) return i;
	}
	return NULL;
}

// The "standard way":
// 1. ioNamePtr: absolute path with correct volume name? Return 'p'.
// 2. ioVRefNum: correct volume/drive/WD? See determineNum for return values.
// 3. Else return 0.
// Note that MPW does relative paths from CNID 1 but these have the right vRefNum
static char determineNumStr(void *_pb) {
	struct VolumeParam *pb = _pb;

	// First element of absolute path
	if (pb->ioNamePtr) {
		char name[256];
		p2cstr(name, pb->ioNamePtr);

		// Is absolute path?
		if (name[0]!=0 && name[0]!=':' && strstr(name, ":")!=NULL) {
			strstr(name, ":")[0] = 0; // terminate at the colon
			unsigned char pas[256];
			c2pstr(pas, name);
			if (RelString(pas, vcb.vcbVN, 0, 1) == 0) {
				return 'p';
			} else {
				return 0;
			}
		}
	}

	return determineNum(pb);
}

// 0 for shrug, 'w' for wdcbRefNum, 'v' for vRefNum, 'd' for drvNum
static char determineNum(void *_pb) {
	struct VolumeParam *pb = _pb;

	if (pb->ioVRefNum <= WDHI) {
		// Working directory refnum
		struct WDCBRec *wdcb = findWD(pb->ioVRefNum);
		if (wdcb && wdcb->wdVCBPtr == &vcb) {
			return 'w';
		} else {
			return 0;
		}
	} else if (pb->ioVRefNum < 0) {
		// Volume refnum
		if (pb->ioVRefNum == vcb.vcbVRefNum) {
			return 'v';
		} else {
			return 0;
		}
	} else if (pb->ioVRefNum == 0) {
		// Default volume (arguably also a WD)
		if ((struct VCB *)LMGetDefVCBPtr() == &vcb) {
			return 'v';
		} else {
			return 0;
		}
	} else {
		// Drive number
		if (pb->ioVRefNum == dqe.dqe.dQDrive) {
			return 'd';
		} else {
			return 0;
		}
	}
}

static bool visName(const char *name) {
	if (name[0] == '.') return false;

	int len = strlen(name);
	if (len >= 5 && !strcmp(name+len-5, ".rsrc")) return false;
	if (len >= 6 && !strcmp(name+len-6, ".rdump")) return false;
	if (len >= 6 && !strcmp(name+len-6, ".idump")) return false;

	return true;
}

// Hash table accessors, tuned to minimise slot usage

struct rec {
	int32_t parent;
	char name[512];
};

// NULL on failure (bad CNID)
static void setDB(int32_t cnid, int32_t pcnid, const char *name) {
	struct rec rec;
	rec.parent = pcnid;
	strcpy(rec.name, name);

	// Cast away the const -- but the name should not be modified by us
	HTinstall('$', &cnid, sizeof cnid, &rec, 4+strlen(name)+1); // dodgy size calc
}

static const char *getDBName(int32_t cnid) {
	struct rec *rec = HTlookup('$', &cnid, sizeof cnid);
	if (!rec) return NULL;
	return rec->name;
}

// Zero on failure (bad CNID)
static int32_t getDBParent(int32_t cnid) {
	struct rec *rec = HTlookup('$', &cnid, sizeof cnid);
	if (!rec) return 0;
	return rec->parent;
}

static long fsCall(void *pb, long selector) {
	static unsigned char hdr;
	if (hdr++ == 0) {
		lprintf("%lu%% (browse/total) %d%% (relist/total)\n", browseTimer*100/hfsTimer, relistTimer*100/hfsTimer);
	}

	TIMEFUNC(hfsTimer);

	HTallocatelater(); // schedule some system task time if needed

	unsigned short trap = ((struct IOParam *)pb)->ioTrap;

	// Use the selector format of the File System Manager
	if ((trap & 0xff) == 0x60) { // HFSDispatch
		selector = (selector & 0xff) | (trap & 0xf00);
	} else {
		selector = trap;
	}

	if (lprintf_enable) {
		lprintf("FS_%s", PBPrint(pb, selector, 1));
		strcat(lprintf_prefix, "     ");
	}

	callcnt++;

	OSErr result;
	struct handler h = fsHandler(selector);

	if (h.func == NULL) {
		result = h.err;
	} else {
		// Unsafe calling convention magic
		typedef OSErr (*handlerFunc)(void *pb);
		result = ((handlerFunc)h.func)(pb);
	}

	if (lprintf_enable) {
		lprintf_prefix[strlen(lprintf_prefix) - 5] = 0;
		lprintf("%s", PBPrint(pb, selector, result));
	}

	for (int i=0; i<512; i++) {
		if (stack[i]) panic("blown stack");
	}

	return result;
}

// This makes it easy to have a selector return noErr without a function
static struct handler fsHandler(unsigned short selector) {
	switch (selector & 0xf0ff) {
	case kFSMOpen: return (struct handler){fsOpen};
	case kFSMClose: return (struct handler){fsClose};
	case kFSMRead: return (struct handler){fsRead};
	case kFSMWrite: return (struct handler){NULL, wPrErr};
	case kFSMGetVolInfo: return (struct handler){fsGetVolInfo};
	case kFSMCreate: return (struct handler){NULL, wPrErr};
	case kFSMDelete: return (struct handler){NULL, wPrErr};
	case kFSMOpenRF: return (struct handler){fsOpen};
	case kFSMRename: return (struct handler){NULL, wPrErr};
	case kFSMGetFileInfo: return (struct handler){fsGetFileInfo};
	case kFSMSetFileInfo: return (struct handler){NULL, wPrErr};
	case kFSMUnmountVol: return (struct handler){NULL, extFSErr};
	case kFSMMountVol: return (struct handler){fsMountVol};
	case kFSMAllocate: return (struct handler){NULL, wPrErr};
	case kFSMGetEOF: return (struct handler){fsGetEOF};
	case kFSMSetEOF: return (struct handler){NULL, wPrErr};
	case kFSMFlushVol: return (struct handler){NULL, noErr};
	case kFSMGetVol: return (struct handler){NULL, extFSErr};
	case kFSMSetVol: return (struct handler){fsSetVol};
	case kFSMEject: return (struct handler){NULL, extFSErr};
	case kFSMGetFPos: return (struct handler){NULL, extFSErr};
	case kFSMOffline: return (struct handler){NULL, extFSErr};
	case kFSMSetFilLock: return (struct handler){NULL, extFSErr};
	case kFSMRstFilLock: return (struct handler){NULL, extFSErr};
	case kFSMSetFilType: return (struct handler){NULL, extFSErr};
	case kFSMSetFPos: return (struct handler){fsRead};
	case kFSMFlushFile: return (struct handler){NULL, wPrErr};
	case kFSMOpenWD: return (struct handler){fsOpenWD};
	case kFSMCloseWD: return (struct handler){NULL, extFSErr};
	case kFSMCatMove: return (struct handler){NULL, wPrErr};
	case kFSMDirCreate: return (struct handler){NULL, wPrErr};
	case kFSMGetWDInfo: return (struct handler){NULL, noErr};
	case kFSMGetFCBInfo: return (struct handler){fsGetFCBInfo};
	case kFSMGetCatInfo: return (struct handler){fsGetFileInfo};
	case kFSMSetCatInfo: return (struct handler){NULL, wPrErr};
	case kFSMSetVolInfo: return (struct handler){NULL, wPrErr};
	case kFSMLockRng: return (struct handler){NULL, extFSErr};
	case kFSMUnlockRng: return (struct handler){NULL, extFSErr};
	case kFSMXGetVolInfo: return (struct handler){NULL, extFSErr};
	case kFSMCreateFileIDRef: return (struct handler){NULL, extFSErr};
	case kFSMDeleteFileIDRef: return (struct handler){NULL, extFSErr};
	case kFSMResolveFileIDRef: return (struct handler){NULL, extFSErr};
	case kFSMExchangeFiles: return (struct handler){NULL, extFSErr};
	case kFSMCatSearch: return (struct handler){NULL, extFSErr};
	case kFSMOpenDF: return (struct handler){fsOpen};
	case kFSMMakeFSSpec: return (struct handler){fsMakeFSSpec};
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
	case kFSMGetVolParms: return (struct handler){fsGetVolParms};
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
	case kFSMVolumeMount: return (struct handler){NULL, extFSErr};
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

static OSErr controlStatusCall(struct CntrlParam *pb) {
	OSErr err = 100;

	// Coerce csCode or driverGestaltSelector into one long
	// Negative is Status/DriverGestalt, positive is Control/DriverConfigure
	long selector = pb->csCode;

	if (selector == kDriverGestaltCode)
		selector = ((struct DriverGestaltParam *)pb)->driverGestaltSelector;

	if (selector > 0) {
		if ((pb->ioTrap & 0xa8ff) == _Status)
			selector = -selector;

		struct handler h = controlStatusHandler(selector);

		if (h.func == NULL) {
			err = h.err;
		} else {
			// Unsafe calling convention magic
			typedef OSErr (*handlerFunc)(void *pb);
			err = ((handlerFunc)h.func)(pb);
		}
	}

	if (err == 100) {
		if ((pb->ioTrap & 0xa8ff) == _Status)
			err = statusErr;
		else
			err = controlErr;
	}

	return err;
}

static struct handler controlStatusHandler(long selector) {
	switch (selector & 0xf0ff) {
// 	case 'boot': return (struct handler){MyDCBoot};
	default: return (struct handler){NULL, selector>0 ? statusErr : controlErr};
	}
}
