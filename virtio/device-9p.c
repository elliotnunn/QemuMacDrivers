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

static OSStatus initialize(DriverInitInfo *info);
static OSStatus finalize(DriverFinalInfo *info);
static OSErr CommProc(short message, void *paramBlock, void *globalsPtr);
static OSErr HFSProc(VCBPtr theVCB, short selectCode, void *paramBlock, void *fsdGlobalPtr, short fsid);

char stack[32*1024];

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
			.dQFSID = ('9'<<8) | 'p',
		}
	};

	AddDrive(info->refNum, 22 /*todo*/, &dqe.dqe);
	lprintf("DQE qLink %#08x\n", dqe.dqe.qLink);

	static RoutineDescriptor commDesc = BUILD_ROUTINE_DESCRIPTOR(uppFSDCommProcInfo, CommProc);
	static RoutineDescriptor hfsDesc = BUILD_ROUTINE_DESCRIPTOR(uppHFSCIProcInfo, HFSProc);

	static struct FSDRec fsdesc = {
		.fsdLength = sizeof (struct FSDRec),
		.fsdVersion = 1,
		.fileSystemFSID = ('9'<<8) | 'p',
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
	fserr = SetFSInfo(('9'<<8) | 'p', sizeof fsdesc, &fsdesc);
	lprintf("SetFSInfo returns %d\n", fserr);

	static struct VolumeMountInfoHeader vmi = {
		.length = 8,
		.media = ('9'<<24) | ('p'<<16) | ('9'<<8) | 'p',
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

static OSErr CommProc(short message, void *paramBlock, void *globalsPtr) {
	lprintf("CommProc message=%#02x paramBlock=%#08x\n", message, paramBlock);
	return paramErr;
}

static OSErr HFSProc(VCBPtr theVCB, short selectCode, void *paramBlock, void *fsdGlobalPtr, short fsid) {
	lprintf("HFSProc selectCode=%#02x paramBlock=%#08x fsid=%#02x\n", selectCode, paramBlock, fsid);
	return paramErr;
}
