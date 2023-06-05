#include <Disks.h>
#include <Files.h>
#include <FSM.h>
#include <Gestalt.h>
#include <DriverServices.h>
#include <MixedMode.h>

#include "lprintf.h"
#include "rpc9p.h"
#include "transport.h"
#include "virtqueue.h"

#include <stdbool.h> // leave till last, conflicts with Universal Interfaces
#include <string.h>

enum {
	CREATOR = (0x0613<<16) | ('9'<<8) | 'p',
};

static OSStatus initialize(DriverInitInfo *info);
static OSStatus finalize(DriverFinalInfo *info);
static OSErr CommProc(short message, struct IOParam *pb, void *globals);
static OSErr HFSProc(struct VCB *vcb, unsigned short selector, void *pb, void *globals, short fsid);
static OSErr MyVolumeMount(struct VolumeParam *pb, struct VCB *vcb);
static OSErr MyFlushVol(void);
static OSErr MyGetVolInfo(struct HVolumeParam *pb, struct VCB *vcb);

char stack[32*1024];
short drvRefNum;

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

	struct Qid9 root = {0};
	uint32_t rootfid = 99;

	if (Attach9(rootfid, (uint32_t)~0 /*NOFID*/, "", "", &root)) {
		return paramErr;
	}

	lprintf("qid %d %x %08x%08x\n", root.type, root.version, (uint32_t)(root.path >> 32), (uint32_t)root.path);


	Walk9(rootfid, rootfid+1, "builds", &root);
	lprintf("builds: qid %d %x %08x%08x\n", root.type, root.version, (uint32_t)(root.path >> 32), (uint32_t)root.path);

	uint32_t iounit = 0;
	Open9(rootfid, 0, &root, &iounit);
	lprintf("iounit %d\n", iounit);

	uint32_t succeeded = 0;
	Read9(rootfid, 0, Max9, &succeeded);

	for (int i=0; i<succeeded; i++) {
		lprintf("%02x ", (int)(unsigned char)Buf9[i]);
	}
	lprintf("\n");

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
	lprintf("PBVolumeMount returns %d\n", fserr);

	return noErr;
}

static OSStatus finalize(DriverFinalInfo *info) {
	return noErr;
}

static OSErr CommProc(short message, struct IOParam *pb, void *globals) {
	lprintf("CommProc message=%#02x paramBlock=%#08x\n", message, pb);

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
	lprintf("HFSProc selector=%#02x pb=%#08x fsid=%#02x\n", selector, pb, fsid);

	selector &= 0xf0ff; // strip off OS trap modifier bits
	// No need to pass on the selector... pb.ioTrap is enough for the "HFS" bit

	// "MyX" funcs have 0/1/2 arguments: unsafe calling convention magic!
	typedef OSErr (*responderPtr)(void *pb, struct VCB *vcb);

	void *responder =
		/*a000*/ selector==kFSMOpen ? NULL :
		/*a001*/ selector==kFSMClose ? NULL :
		/*a002*/ selector==kFSMRead ? NULL :
		/*a003*/ selector==kFSMWrite ? NULL :
		/*a007*/ selector==kFSMGetVolInfo ? MyGetVolInfo :
		/*a008*/ selector==kFSMCreate ? NULL :
		/*a009*/ selector==kFSMDelete ? NULL :
		/*a00a*/ selector==kFSMOpenRF ? NULL :
		/*a00b*/ selector==kFSMRename ? NULL :
		/*a00c*/ selector==kFSMGetFileInfo ? NULL :
		/*a00d*/ selector==kFSMSetFileInfo ? NULL :
		/*a00e*/ selector==kFSMUnmountVol ? NULL :
		/*a00f*/ selector==kFSMMountVol ? NULL :
		/*a010*/ selector==kFSMAllocate ? NULL :
		/*a011*/ selector==kFSMGetEOF ? NULL :
		/*a012*/ selector==kFSMSetEOF ? NULL :
		/*a013*/ selector==kFSMFlushVol ? MyFlushVol :
		/*a014*/ selector==kFSMGetVol ? NULL :
		/*a015*/ selector==kFSMSetVol ? NULL :
		/*a017*/ selector==kFSMEject ? NULL :
		/*a018*/ selector==kFSMGetFPos ? NULL :
		/*a035*/ selector==kFSMOffline ? NULL :
		/*a041*/ selector==kFSMSetFilLock ? NULL :
		/*a042*/ selector==kFSMRstFilLock ? NULL :
		/*a043*/ selector==kFSMSetFilType ? NULL :
		/*a044*/ selector==kFSMSetFPos ? NULL :
		/*a045*/ selector==kFSMFlushFile ? NULL :
		/*0001*/ selector==kFSMOpenWD ? NULL :
		/*0002*/ selector==kFSMCloseWD ? NULL :
		/*0005*/ selector==kFSMCatMove ? NULL :
		/*0006*/ selector==kFSMDirCreate ? NULL :
		/*0007*/ selector==kFSMGetWDInfo ? NULL :
		/*0008*/ selector==kFSMGetFCBInfo ? NULL :
		/*0009*/ selector==kFSMGetCatInfo ? NULL :
		/*000a*/ selector==kFSMSetCatInfo ? NULL :
		/*000b*/ selector==kFSMSetVolInfo ? NULL :
		/*0010*/ selector==kFSMLockRng ? NULL :
		/*0011*/ selector==kFSMUnlockRng ? NULL :
		/*0012*/ selector==kFSMXGetVolInfo ? NULL :
		/*0014*/ selector==kFSMCreateFileIDRef ? NULL :
		/*0015*/ selector==kFSMDeleteFileIDRef ? NULL :
		/*0016*/ selector==kFSMResolveFileIDRef ? NULL :
		/*0017*/ selector==kFSMExchangeFiles ? NULL :
		/*0018*/ selector==kFSMCatSearch ? NULL :
		/*001a*/ selector==kFSMOpenDF ? NULL :
		/*001b*/ selector==kFSMMakeFSSpec ? NULL :
		/*0020*/ selector==kFSMDTGetPath ? NULL :
		/*0021*/ selector==kFSMDTCloseDown ? NULL :
		/*0022*/ selector==kFSMDTAddIcon ? NULL :
		/*0023*/ selector==kFSMDTGetIcon ? NULL :
		/*0024*/ selector==kFSMDTGetIconInfo ? NULL :
		/*0025*/ selector==kFSMDTAddAPPL ? NULL :
		/*0026*/ selector==kFSMDTRemoveAPPL ? NULL :
		/*0027*/ selector==kFSMDTGetAPPL ? NULL :
		/*0028*/ selector==kFSMDTSetComment ? NULL :
		/*0029*/ selector==kFSMDTRemoveComment ? NULL :
		/*002a*/ selector==kFSMDTGetComment ? NULL :
		/*002b*/ selector==kFSMDTFlush ? NULL :
		/*002c*/ selector==kFSMDTReset ? NULL :
		/*002d*/ selector==kFSMDTGetInfo ? NULL :
		/*002e*/ selector==kFSMDTOpenInform ? NULL :
		/*002f*/ selector==kFSMDTDelete ? NULL :
		/*0030*/ selector==kFSMGetVolParms ? NULL :
		/*0031*/ selector==kFSMGetLogInInfo ? NULL :
		/*0032*/ selector==kFSMGetDirAccess ? NULL :
		/*0033*/ selector==kFSMSetDirAccess ? NULL :
		/*0034*/ selector==kFSMMapID ? NULL :
		/*0035*/ selector==kFSMMapName ? NULL :
		/*0036*/ selector==kFSMCopyFile ? NULL :
		/*0037*/ selector==kFSMMoveRename ? NULL :
		/*0038*/ selector==kFSMOpenDeny ? NULL :
		/*0039*/ selector==kFSMOpenRFDeny ? NULL :
		/*003a*/ selector==kFSMGetXCatInfo ? NULL :
		/*003f*/ selector==kFSMGetVolMountInfoSize ? NULL :
		/*0040*/ selector==kFSMGetVolMountInfo ? NULL :
		/*0041*/ selector==kFSMVolumeMount ? MyVolumeMount :
		/*0042*/ selector==kFSMShare ? NULL :
		/*0043*/ selector==kFSMUnShare ? NULL :
		/*0044*/ selector==kFSMGetUGEntry ? NULL :
		/*0060*/ selector==kFSMGetForeignPrivs ? NULL :
		/*0061*/ selector==kFSMSetForeignPrivs ? NULL :
		/*001d*/ selector==kFSMGetVolumeInfo ? NULL :
		/*001e*/ selector==kFSMSetVolumeInfo ? NULL :
		/*0051*/ selector==kFSMReadFork ? NULL :
		/*0052*/ selector==kFSMWriteFork ? NULL :
		/*0053*/ selector==kFSMGetForkPosition ? NULL :
		/*0054*/ selector==kFSMSetForkPosition ? NULL :
		/*0055*/ selector==kFSMGetForkSize ? NULL :
		/*0056*/ selector==kFSMSetForkSize ? NULL :
		/*0057*/ selector==kFSMAllocateFork ? NULL :
		/*0058*/ selector==kFSMFlushFork ? NULL :
		/*0059*/ selector==kFSMCloseFork ? NULL :
		/*005a*/ selector==kFSMGetForkCBInfo ? NULL :
		/*005b*/ selector==kFSMCloseIterator ? NULL :
		/*005c*/ selector==kFSMGetCatalogInfoBulk ? NULL :
		/*005d*/ selector==kFSMCatalogSearch ? NULL :
		/*006e*/ selector==kFSMMakeFSRef ? NULL :
		/*0070*/ selector==kFSMCreateFileUnicode ? NULL :
		/*0071*/ selector==kFSMCreateDirUnicode ? NULL :
		/*0072*/ selector==kFSMDeleteObject ? NULL :
		/*0073*/ selector==kFSMMoveObject ? NULL :
		/*0074*/ selector==kFSMRenameUnicode ? NULL :
		/*0075*/ selector==kFSMExchangeObjects ? NULL :
		/*0076*/ selector==kFSMGetCatalogInfo ? NULL :
		/*0077*/ selector==kFSMSetCatalogInfo ? NULL :
		/*0078*/ selector==kFSMOpenIterator ? NULL :
		/*0079*/ selector==kFSMOpenFork ? NULL :
		/*007a*/ selector==kFSMMakeFSRefUnicode ? NULL :
		/*007c*/ selector==kFSMCompareFSRefs ? NULL :
		/*007d*/ selector==kFSMCreateFork ? NULL :
		/*007e*/ selector==kFSMDeleteFork ? NULL :
		/*007f*/ selector==kFSMIterateForks ? NULL :
		NULL;

	if (responder == NULL) {
		lprintf("UNIMPLEMENTED, returning extFSErr\n");
		return extFSErr;
	}

	return ((responderPtr)responder)(pb, vcb);
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
	memcpy(vcb->vcbVN, "\x04Elmo", 5);
	vcb->vcbFSID = CREATOR & 0xffff;
	vcb->vcbFilCnt = 1;
	vcb->vcbDirCnt = 1;

	err = UTAddNewVCB(22, &pb->ioVRefNum, vcb);
	if (err) return err;

	PostEvent(diskEvt, 22);

	return noErr;
}

static OSErr MyFlushVol(void) {
	return noErr;
}

static OSErr MyGetVolInfo(struct HVolumeParam *pb, struct VCB *vcb) {
	if (pb->ioNamePtr) {
		memcpy(pb->ioNamePtr, "\x04Elmo", 5);
	}

	pb->ioVCrDate = 0;
	pb->ioVLsMod = 0;
	pb->ioVAtrb = 0;
	pb->ioVNmFls = 0;
	pb->ioVBitMap = 0;
	pb->ioAllocPtr = 0;
	pb->ioVNmAlBlks = 0xffff;
	pb->ioVAlBlkSiz = 512;
	pb->ioVClpSiz = 512;
	pb->ioAlBlSt = 0;
	pb->ioVNxtCNID = 100;
	pb->ioVFrBlk = 0xffff;

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
