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
#include "paramblkprint.h"
#include "patch68k.h"
#include "rpc9p.h"
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

struct flagdqe {
	uint32_t flags; // 4 bytes of flags at neg offset
	DrvQEl dqe; // AddDrive points here
};

static OSStatus finalize(DriverFinalInfo *info);
static OSStatus initialize(DriverInitInfo *info);
static long disposePtrShield(Ptr ptr, void *caller);
static void secondaryInitialize(void);
static int32_t browse(uint32_t fid, int32_t cnid, const unsigned char *paspath);
static int32_t pbDirID(void *_pb);
static struct WDCBRec *findWD(short refnum);
static int walkToCNID(int32_t cnid, uint32_t fid);
static int32_t makeCNID(int32_t parent, char *name);
static void cnidPrint(int32_t cnid);
static struct DrvQEl *findDrive(short num);
static struct VCB *findVol(short num);
static char determineNumStr(void *_pb);
static char determineNum(void *_pb);
static void autoVolName(unsigned char *pas);
static bool visName(const char *name);
static long fsCall(void *pb, long selector);
static struct handler fsHandler(unsigned short selector);
static OSErr controlStatusCall(struct CntrlParam *pb);
static struct handler controlStatusHandler(long selector);

void *disposePtrPatch;
static short drvrRefNum;
static unsigned long callcnt;
static struct Qid9 root;
static int32_t cnidCtr = 100;
static Handle finderwin;
static bool mounted;
static char preferName[28];
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
	.vcbNxtCNID = 100,
	.vcbFreeBks = 0xe000,
	.vcbFSID = FSID,
	.vcbFilCnt = 1,
	.vcbDirCnt = 1,
};
static const short bootBlocks[] = {
	0x4c4b, 0x6000, 0x0086, 0x4418, 0x0000, 0x0653, 0x7973, 0x7465, // LK`...D....Syste
	0x6d00, 0x0000, 0x0000, 0x0000, 0x0000, 0x0646, 0x696e, 0x6465, // m..........Finde
	0x7200, 0x0000, 0x0000, 0x0000, 0x0000, 0x074d, 0x6163, 0x7342, // r..........MacsB
	0x7567, 0x0000, 0x0000, 0x0000, 0x0000, 0x0c44, 0x6973, 0x6173, // ug.........Disas
	0x7365, 0x6d62, 0x6c65, 0x7200, 0x0000, 0x0d53, 0x7461, 0x7274, // sembler....Start
	0x5570, 0x5363, 0x7265, 0x656e, 0x0000, 0x0646, 0x696e, 0x6465, // UpScreen...Finde
	0x7200, 0x0000, 0x0000, 0x0000, 0x0000, 0x0943, 0x6c69, 0x7062, // r..........Clipb
	0x6f61, 0x7264, 0x0000, 0x0000, 0x0000, 0x000a, 0x0014, 0x0000, // oard............
	0x4300, 0x0000, 0x8000, 0x0002, 0x0000, 0x4a78, 0x028e, 0x6b46, // C.........Jx..kF
	0x2078, 0x02ae, 0x3228, 0x0008, 0x7cfe, 0x5446, 0x303b, 0x603c, //  x..2(..|.TF0;`<
	0x6758, 0xb240, 0x66f4, 0x0c01, 0x0076, 0x6210, 0x2078, 0x02a6, // gX.@f....vb. x..
	0xd1fa, 0xffd4, 0xa057, 0x21f8, 0x02a6, 0x0118, 0x584f, 0x2e0f, // .....W!.....XO..
	0x6138, 0x323b, 0x6022, 0x4a40, 0x6704, 0x323b, 0x6024, 0x2078, // a82;`"J@g.2;`$ x
	0x02ae, 0x4ef0, 0x1000, 0x7062, 0xa9c9, 0x0075, 0x0276, 0x0178, // ..N...pb...u.v.x
	0x037a, 0x067c, 0x0000, 0x0a44, 0x090e, 0x0f1c, 0x30e6, 0x1d96, // .z.|...D....0...
	0x0b82, 0x0a52, 0x11ae, 0x336e, 0x203e, 0x41fa, 0xff0e, 0x43f8, // ...R..3n >A...C.
	0x0ad8, 0x7010, 0xa02e, 0x41fa, 0xff12, 0x43f8, 0x02e0, 0x7010, // ..p...A...C...p.
	0xa02e, 0x41fa, 0xff56, 0x43f8, 0x0970, 0x21c9, 0x096c, 0x7010, // ..A..VC..p!..lp.
	0xa02e, 0x303a, 0xff58, 0xa06d, 0x303a, 0xff50, 0xa06c, 0x2047, // ..0:.X.m0:.P.l G
	0x3178, 0x0210, 0x0016, 0xa00f, 0x6654, 0x42a8, 0x0012, 0x4268, // 1x......fTB...Bh
	0x001c, 0xa207, 0x6640, 0x2868, 0x005e, 0x2168, 0x005a, 0x0030, // ....f@(h.^!h.Z.0
	0x6710, 0x217c, 0x4552, 0x494b, 0x001c, 0x7001, 0xa260, 0x6626, // g.!|ERIK..p..`f&
	0xa015, 0x554f, 0xa995, 0x4a5f, 0x6b1a, 0x594f, 0x2f3c, 0x626f, // ..UO..J_k.YO/<bo
	0x6f74, 0x3f3c, 0x0002, 0xa9a0, 0x201f, 0x6712, 0x584f, 0x2640, // ot?<.... .g.XO&@
	0x2053, 0x4ed0, 0x702b, 0x3f00, 0x2047, 0xa00e, 0x301f, 0x4e75, //  SN.p+?. G..0.Nu
};

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

	if (code <= 6)
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
			if (param->ioPosOffset+i < sizeof(bootBlocks)) {
				param->ioBuffer[i] = ((char *)&bootBlocks)[param->ioPosOffset+i];
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

	if (code <= 6)
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
	lprintf_enable = true;

	drvrRefNum = info->refNum;

	OSStatus err;

	lprintf(".virtio9p: primary init\n");

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

	if (Attach9(ROOTFID, (uint32_t)~0 /*auth=NOFID*/, "", "", &root)) {
		return paramErr;
	}

	dqe.dqe.dQDrive=4; // lower numbers reserved
	while (findDrive(dqe.dqe.dQDrive) != NULL) dqe.dqe.dQDrive++;
	AddDrive(drvrRefNum, dqe.dqe.dQDrive, &dqe.dqe);

	RegPropertyValueSize size = sizeof(preferName);
	RegistryPropertyGet(&info->deviceEntry, "mount", preferName, &size);

	// Find the system folder
	vcb.vcbFndrInfo[0] = browse(3 /*fid*/, 2 /*cnid*/, "\pSystem Folder");
	lprintf("System folder CNID %d\n", vcb.vcbFndrInfo[0]);

	// Is the File Manager actually up yet?
	if ((long)GetVCBQHdr()->qHead != -1 && (long)GetVCBQHdr()->qHead != 0) {
		secondaryInitialize();

		struct IOParam pb = {.ioVRefNum = dqe.dqe.dQDrive};
		PBMountVol((void *)&pb);
	} else {
		lprintf(".virtio9p: early boot -- attempting boot device hacks\n");

		// This is very early boot, and my globals will be nuked by a ?ROM bug.
		disposePtrPatch = Patch68k(
			_DisposePtr,
			"48e7 e0e0" //     movem.l d0-d2/a0-a2,-(sp)
			"2f2f 0034" //     move.l  $34(sp),-(sp)
			"2f08"      //     move.l  a0,-(sp)
			"4eb9 %l"   //     jsr     disposePtrShield
			"504f"      //     addq    #8,sp
			"4a80"      //     tst.l   d0
			"4cdf 0707" //     movem.l (sp)+,d0-d2/a0-a2
			"6702"      //     beq.s   ok
			"4e75"      //     rts
			"4ef9 %o",  // ok: jmp     originalDisposePtr
			NewRoutineDescriptor((ProcPtr)disposePtrShield,
				kCStackBased
					| STACK_ROUTINE_PARAMETER(1, kFourByteCode)
					| STACK_ROUTINE_PARAMETER(2, kFourByteCode)
					| RESULT_SIZE(kFourByteCode),
				GetCurrentISA())
		);

		Patch68k(
			_InitFS,
			"48e7 e0e0" //     movem.l d0-d2/a0-a2,-(sp)
			"4eb9 %o"   //     jsr     originalInitFS
			"4eb9 %l"   //     jsr     secondaryInitialize
			"4cdf 0707" //     movem.l (sp)+,d0-d2/a0-a2
			"4e75",     //     rts
			NewRoutineDescriptor((ProcPtr)secondaryInitialize,
				kCStackBased,
				GetCurrentISA())
		);

		SetTimeout(1); // give up on the default disk quickly
	}

	return noErr;
}

// Prevent some errant ROM code from nuking my driver!
static long disposePtrShield(Ptr ptr, void *caller) {
	static char myglobals; // a memory address inside our global area

	if (ptr <= &myglobals && &myglobals < ptr+GetPtrSize(ptr)) {
		lprintf("Foiled the ROM's attempt to DisposePtr my globals! Caller was %p.\n", caller);
		Unpatch68k(disposePtrPatch);
		return 1; // do not call the real DisposePtr
	} else {
		return 0; // call the real DisposePtr
	}
}

// Run this only when the File Manager is up
static void secondaryInitialize(void) {
	static bool alreadyUp;
	if (alreadyUp) return;
	alreadyUp = true;

	lprintf(".virtio9p: secondary init\n");

	// External filesystems need a big stack, and they can't
	// share the FileMgr stack because of reentrancy problems
	enum {STACKSIZE = 12 * 1024};
	char *stack = NewPtrSys(STACKSIZE);
	if (stack == NULL) stack = (char *)0x68f168f1;

	Patch68k(
		0x3f2,      // ToExtFS:
		"2f38 0110" //       move.l  StkLowPt,-(sp)
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
		"4ef9 %o",  //       jmp     originalToExtFS

		stack + STACKSIZE - 100,
		stack + STACKSIZE - 100,
		NewRoutineDescriptor((ProcPtr)fsCall,
			kCStackBased
				| STACK_ROUTINE_PARAMETER(1, kFourByteCode)
				| STACK_ROUTINE_PARAMETER(2, kFourByteCode)
				| RESULT_SIZE(kFourByteCode),
			GetCurrentISA())
	);
}

static OSErr fsMountVol(struct IOParam *pb) {
	if (pb->ioVRefNum != dqe.dqe.dQDrive) return extFSErr;
	if (mounted) return volOnLinErr;

	if (preferName[0] != 0) {
		c2pstr(vcb.vcbVN, preferName);
	} else {
		autoVolName(vcb.vcbVN);
	}

	int32_t rootcnid = 2;
	static struct record rootrec = {.parent=1, .name={[28]=0}};
	p2cstr(rootrec.name, vcb.vcbVN);
	HTinstall(&rootcnid, sizeof(rootcnid), &rootrec, sizeof(rootrec)+strlen(rootrec.name)+1);

	finderwin = NewHandleSysClear(2);

	vcb.vcbDrvNum = dqe.dqe.dQDrive;
	vcb.vcbDRefNum = drvrRefNum;
	vcb.vcbVRefNum = -1;

	while (findVol(vcb.vcbVRefNum) != NULL) vcb.vcbVRefNum--;

	lprintf(".virtio9p: refnums are driver=%d drive=%d volume=%d\n",
		drvrRefNum, dqe.dqe.dQDrive, vcb.vcbVRefNum);

	if (GetVCBQHdr()->qHead == NULL) {
		LMSetDefVCBPtr((Ptr)&vcb);
		*(short *)0x384 = vcb.vcbVRefNum; // DefVRefNum

		memcpy(findWD(0),
			&(struct WDCBRec){.wdVCBPtr=&vcb, .wdDirID = 2},
			16);
	}

	Enqueue((QElemPtr)&vcb, GetVCBQHdr());

	PostEvent(diskEvt, dqe.dqe.dQDrive);

	mounted = true;
// 	if (vcb.vcbVRefNum & 1)
// 		strcpy(lprintf_prefix, "                              ");
	return noErr;
}

// TODO: search the FCB array for open files on this volume, set bit 6
// TODO: when given a WD refnum, return directory valence as the file count
// TODO: fake used/free alloc blocks (there are limits depending on H bit)
static OSErr fsGetVolInfo(struct HVolumeParam *pb) {
	if (pb->ioVolIndex > 0) {
		// Index into volume queue
		struct VCB *v = (struct VCB *)GetVCBQHdr()->qHead;
		for (int i=1; i<pb->ioVolIndex && v!=NULL; i++)
			v = (struct VCB *)v->qLink;
		if (v != &vcb)
			return extFSErr;
	} else if (pb->ioVolIndex == 0) {
		// Simple volume/drive refnum
		if (!determineNum(pb)) return extFSErr;
	} else if (pb->ioVolIndex < 0) {
		// "Standard way"
		if (!determineNumStr(pb)) return extFSErr;
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
	if (!determineNumStr(pb)) return extFSErr;

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
	enum {MYFID=3, LISTFID=4};

	bool flat = (pb->ioTrap&0xf2ff) == 0xa00c; // GetFileInfo without "H"
	bool longform = (pb->ioTrap&0x00ff) == 0x0060; // GetCatInfo

	int idx = pb->ioFDirIndex;
	if (idx<0 && !longform) idx=0; // make named GetFInfo calls behave right

	int32_t cnid = pbDirID(pb);

	if (idx > 0) {
		if (!determineNum(pb)) return extFSErr;
		lprintf("GCI index mode\n");

		if (walkToCNID(cnid, MYFID) < 0) return fnfErr;

		// Read the directory
		Walk9(MYFID, LISTFID, 0, NULL, NULL, NULL); // duplicate
		Lopen9(LISTFID, O_RDONLY, NULL, NULL); // iterate

		char utf8[512];
		int err;
		int n=0;
		while ((err=Readdir9(LISTFID, NULL, NULL, utf8)) == 0) {
			if (!visName(utf8)) continue;
			if (pb->ioFDirIndex==++n) break;
		}

		if (err!=0) return fnfErr;

		cnid = makeCNID(cnid, utf8);
		Walk9(MYFID, MYFID, 1, (const char *[]){utf8}, NULL, NULL);
	} else if (idx == 0) {
		if (!determineNumStr(pb)) return extFSErr;
		lprintf("GCI name mode\n");
		cnid = browse(MYFID, cnid, pb->ioNamePtr);
		if (cnid < 0) return cnid;
	} else {
		if (!determineNum(pb)) return extFSErr;
		lprintf("GCI ID mode\n");
		cnid = browse(MYFID, cnid, "\p");
		if (cnid < 0) return cnid;
	}

	// MYFID and cnid now both valid

	// Here's some tricky debugging
	if (lprintf_enable) {
		lprintf("GCI found "); cnidPrint(cnid); lprintf("\n");
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
		int n=0;
		char childname[512];
		Lopen9(MYFID, O_RDONLY, NULL, NULL); // iterate
		while (Readdir9(MYFID, NULL, NULL, childname) == 0) {
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
		uint64_t rsize = 0;
		char rname[512];
		sprintf(rname, "%s.rsrc", detail->name);
		if (!Walk9(MYFID, MYFID, 2, (const char *[]){"..", rname}, NULL, NULL)) {
			Getattr9(MYFID, NULL, &rsize, NULL);
		}

		pb->ioFlAttrib = 0;
		pb->ioACUser = 0;
		pb->ioFlFndrInfo = (struct FInfo){0};
		pb->ioFlStBlk = 0;
		pb->ioFlLgLen = size;
		pb->ioFlPyLen = (size + 511) & -512;
		pb->ioFlRStBlk = 0;
		pb->ioFlRLgLen = rsize;
		pb->ioFlRPyLen = (rsize + 511) & -512;
		pb->ioFlCrDat = 0;
		pb->ioFlMdDat = 0;
		if (longform) {
			pb->ioFlBkDat = 0;
			pb->ioFlXFndrInfo = (struct FXInfo){0};
			pb->ioFlClpSiz = 0;
		}

		if (!strcmp(detail->name, "System")) {
			pb->ioFlFndrInfo = (struct FInfo){.fdType='ZSYS', .fdCreator='MACS'};
		} else if (!strcmp(detail->name, "Finder")) {
			pb->ioFlFndrInfo = (struct FInfo){.fdType='FNDR', .fdCreator='MACS'};
		} else if (!strcmp(detail->name, "Desktop DB")) {
			pb->ioFlFndrInfo = (struct FInfo){.fdType='BTFL', .fdCreator='DMGR'};
		} else if (!strcmp(detail->name, "Desktop DF")) {
			pb->ioFlFndrInfo = (struct FInfo){.fdType='DTFL', .fdCreator='DMGR'};
		}
	}

	return noErr;
}

static OSErr fsMakeFSSpec(struct HIOParam *pb) {
	if (!determineNumStr(pb)) return extFSErr;

	struct FSSpec *spec = (struct FSSpec *)pb->ioMisc;

	int32_t cnid = pbDirID(pb);
	cnid = browse(10, cnid, pb->ioNamePtr);
	if (cnid > 0) {
		// The target exists
		struct record *rec = HTlookup(&cnid, sizeof(cnid));

		if (cnid == 2) {
			spec->vRefNum = vcb.vcbVRefNum;
			spec->parID = 2;
			spec->name[0] = 0;
		} else {
			spec->vRefNum = vcb.vcbVRefNum;
			spec->parID = rec->parent;
			mr31name(spec->name, rec->name);
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
	pb->ioFRefNum = 0;

	if (!determineNumStr(pb)) return extFSErr;

	bool rfork = (pb->ioTrap & 0xff) == 0x0a;

	short refn;
	struct FCBRec *fcb;
	if (UnivAllocateFCB(&refn, &fcb) != noErr) return tmfoErr;

	uint32_t fid = 32 + refn;
	Clunk9(fid); // preemptive

	int32_t cnid = pbDirID(pb);
	cnid = browse(fid, cnid, pb->ioNamePtr);
	if (cnid < 0) return cnid;

	struct record *rec = HTlookup(&cnid, sizeof(cnid));
	if (!rec) return fnfErr;

	if (rfork) {
		char rname[512];
		sprintf(rname, "%s.rsrc", rec->name);
		if (Walk9(fid, fid, 2, (const char *[]){"..", rname}, NULL, NULL))
			return fnfErr;
		// TODO this actually doesn't check the error correctly
	}

	uint64_t size = 0;
	if (Getattr9(fid, NULL, &size, NULL)) return permErr;
	// TODO also check that it's not a directory

	if (Lopen9(fid, O_RDONLY, NULL, NULL))
		return permErr;

	*fcb = (struct FCBRec){
		.fcbFlNm = cnid,
		.fcbFlags = (rfork ? 2 : 0) | 0x20, // locked ?resource
		.fcbTypByt = 0, // MFS only
		.fcbSBlk = 99, // free for our use
		.fcbEOF = size,
		.fcbPLen = (size + 511) & -512,
		.fcbCrPs = 0,
		.fcbVPtr = &vcb,
		.fcbBfAdr = NULL, // reserved
		.fcbFlPos = 0, // free for own use
		.fcbClmpSize = 512,
		.fcbBTCBPtr = NULL, // reserved
		.fcbExtRec = {}, // own use
		.fcbFType = 'APPL',
		.fcbCatPos = 0, // own use
		.fcbDirID = rec->parent,
	};

	mr31name(fcb->fcbCName, rec->name);

	pb->ioFRefNum = refn;

	return noErr;
}

static OSErr fsGetEOF(struct IOParam *pb) {
	struct FCBRec *fcb;
	if (UnivResolveFCB(pb->ioRefNum, &fcb))
		return paramErr;

	if (fcb->fcbFlNm == 0)
		return fnOpnErr;

	if (fcb->fcbVPtr != &vcb)
		return extFSErr;

	pb->ioMisc = (Ptr)fcb->fcbEOF;

	return noErr;
}

static OSErr fsGetFCBInfo(struct FCBPBRec *pb) {
	short ref;
	struct FCBRec *fcb;

	if (pb->ioFCBIndx == 0) {
		// File refNum provided
		ref = pb->ioRefNum;
		OSErr err = UnivResolveFCB(ref, &fcb);
		if (err) return err;
	} else if (pb->ioFCBIndx > 0) {
		// Index provided, possibly with a "filter" vRefNum
		struct VCB *volfilter;
		if (pb->ioVRefNum == 0) {
			volfilter = NULL;
		} else {
			if (!determineNum(pb)) return extFSErr;
			volfilter = &vcb;
		}

		ref = 0;
		for (int i=0; i<0; i++)	{
			OSErr err = UnivIndexFCB(volfilter, &ref, &fcb);
			if (err) return err;
		}
	} else if (pb->ioFCBIndx < 0) {
		return paramErr;
	}

	if (fcb->fcbVPtr != &vcb)
		return extFSErr;

	if (pb->ioNamePtr != NULL) pstrcpy(pb->ioNamePtr, fcb->fcbCName);
	pb->ioFCBFlNm = fcb->fcbFlNm;
	pb->ioFCBFlags = fcb->fcbFlags;
	pb->ioFCBStBlk = fcb->fcbSBlk;
	pb->ioFCBEOF = fcb->fcbEOF;
	pb->ioFCBPLen = fcb->fcbPLen;
	pb->ioFCBCrPs = fcb->fcbCrPs;
	pb->ioFCBVRefNum = vcb.vcbVRefNum;
	pb->ioFCBClpSiz = fcb->fcbClmpSize;
	pb->ioFCBParID = fcb->fcbDirID;

	return noErr;
}

static OSErr fsClose(struct IOParam *pb) {
	struct FCBRec *fcb;
	if (UnivResolveFCB(pb->ioRefNum, &fcb))
		return paramErr;

	if (fcb->fcbFlNm == 0)
		return fnOpnErr;

	if (fcb->fcbVPtr != &vcb)
		return extFSErr;

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

	if (fcb->fcbFlNm == 0)
		return fnOpnErr;

	if (fcb->fcbVPtr != &vcb)
		return extFSErr;

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

	while (pb->ioActCount < pb->ioReqCount) {
		uint32_t todo = pb->ioReqCount - pb->ioActCount;
		if (todo > Max9) todo = Max9;

		uint32_t act = 0;
		Read9(32 + pb->ioRefNum, fcb->fcbCrPs, pb->ioReqCount, &act);

		BlockMoveData(Buf9, pb->ioBuffer + pb->ioActCount, act);

		pb->ioActCount += act;
		fcb->fcbCrPs += act;
		pb->ioPosOffset = fcb->fcbCrPs;

		if (act != todo) break;
	}

// 	if (pb->ioActCount != pb->ioReqCount)
// 		return eofErr;

	return noErr;
}

// "Working directories" are a compatibility shim for apps expecting flat disks:
// a table of fake volume reference numbers that actually refer to directories.
static OSErr fsOpenWD(struct WDParam *pb) {
	if (!determineNumStr(pb)) return extFSErr;

	int32_t cnid = pbDirID(pb);
	cnid = browse(12, cnid, pb->ioNamePtr);
	if (cnid < 0) return cnid;

	// The root: no need to create a WD, just return the volume's refnum
	if (cnid == 2) {
		pb->ioVRefNum = vcb.vcbVRefNum;
		return noErr;
	}

	// Ensure it's actually a directory
	struct Qid9 qid;
	if (Getattr9(12, &qid, NULL, NULL)) return permErr;
	if ((qid.type & 0x80) == 0) return dirNFErr;

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

// The File Manager has very kindly populated this call for us,
// and only asks that we return noErr (after checking we're the right fs!).
// FM does pass the WDCB address in d1, but we don't need it.
static OSErr fsGetWDInfo(struct WDParam *pb) {
	if (pb->ioWDVRefNum != vcb.vcbVRefNum) return extFSErr;
	return noErr;
}

// Does not actually check for the right volume: determine*() first.
static int32_t browse(uint32_t fid, int32_t cnid, const unsigned char *paspath) {
	enum {LISTFID=5};

	// Null termination makes tokenisation easier (just convert : to null)
	char cpath[256] = "";
	if (paspath != NULL) p2cstr(cpath, paspath);
	int pathlen=strlen(cpath);

	bool pathAbsolute = ((cpath[0] != ':') && (strstr(cpath, ":") != NULL))
		|| (cnid == 1);

	// Tokenize
	for (int i=0; i<sizeof cpath; i++) {
		if (cpath[i] == ':') cpath[i] = 0;
	}

	char *comp=cpath;
	if (pathAbsolute) {
		unsigned char pas[256];
		c2pstr(pas, comp)
		if (RelString(pas, vcb.vcbVN, 0, 1) != 0) {
			return extFSErr;
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

// Does not check whether our volume is the right one: use determineNumStr
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

// Does not actually check for the right volume: determine*() first.
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

	bool bad = Walk9(ROOTFID, fid, compcnt, (const char **)compptr, NULL/*numok*/, qids);
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

static void autoVolName(unsigned char *pas) {
	for (int i=1;; i++) {
		strcpy((char *)pas+1, "Shared Folder");
		if (i > 1) sprintf((char *)pas+14, " %d", i);
		pas[0] = strlen((char *)pas+1);

		// Find a volume with a matching name
		struct VCB *v = (struct VCB *)GetVCBQHdr()->qHead;
		while (v != NULL) {
			if (RelString(pas, v->vcbVN, 0, 1) == 0) {
				break;
			}
			v = (struct VCB *)v->qLink;
		}

		// None? Good.
		if (v == NULL) return;
	}
}

static bool visName(const char *name) {
	if (name[0] == '.') return false;

	int len = strlen(name);
	if (len >= 5 && !strcmp(name+len-5, ".rsrc")) return false;

	return true;
}

static long fsCall(void *pb, long selector) {
	unsigned short trap = ((struct IOParam *)pb)->ioTrap;

	// Use the selector format of the File System Manager
	if ((trap & 0xff) == 0x60) { // HFSDispatch
		selector = (selector & 0xff) | (trap & 0xf00);
	} else {
		selector = trap;
	}

	lprintf("FS_%s", PBPrint(pb, selector, 1));
	strcat(lprintf_prefix, "     ");

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

	lprintf_prefix[strlen(lprintf_prefix) - 5] = 0;
	lprintf("%s", PBPrint(pb, selector, result));

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
	case kFSMSetVol: return (struct handler){NULL, extFSErr};
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
	case kFSMGetWDInfo: return (struct handler){fsGetWDInfo};
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
