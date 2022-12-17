#include <Devices.h>
#include <DriverServices.h>
#include <Types.h>

#include "allocator.h"
#include "lprintf.h"
#include "transport.h"
#include "virtqueue.h"

#include "device.h"

OSStatus DoDriverIO(AddressSpaceID spaceID, IOCommandID cmdID,
	IOCommandContents pb, IOCommandCode code, IOCommandKind kind);
static OSStatus initialize(DriverInitInfo *info);

// Allocate one 4096-byte page for everything (for now)
static void *lpage;
static uint32_t ppage;

DriverDescription TheDriverDescription = {
	kTheDescriptionSignature,
	kInitialDriverDescriptor,
	"\ppci1af4,1059",
	0x00, 0x10, 0x80, 0x00, // v0.1
	kDriverIsLoadedUponDiscovery | kDriverIsOpenedUponLoad,
	"\p.virtio-snd",
	0, 0, 0, 0, 0, 0, 0, 0, // reserved
	1, // nServices
	kServiceCategoryNdrvDriver, kNdrvTypeIsGeneric, 0x00, 0x10, 0x80, 0x00, //v0.1
};

OSStatus DoDriverIO(AddressSpaceID spaceID, IOCommandID cmdID,
	IOCommandContents pb, IOCommandCode code, IOCommandKind kind) {
	OSStatus err;

	(void)spaceID; // Apple never implemented multiple address space support

	switch (code) {
	case kInitializeCommand:
	case kReplaceCommand:
		err = initialize(pb.initialInfo);
		break;
	case kFinalizeCommand:
//	case kSupersededCommand:
// 		err = finalize(pb.finalInfo);
// 		break;
// 	case kControlCommand:
// 		err = control((*pb.pb).cntrlParam.csCode, *(void **)&(*pb.pb).cntrlParam.csParam);
// 		break;
// 	case kStatusCommand:
// 		err = status((*pb.pb).cntrlParam.csCode, *(void **)&(*pb.pb).cntrlParam.csParam);
// 		break;
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
	long ram = 0;

	lprintf("Got far enough to load a driver for virtio-snd\n");

	// No need to signal FAILED if cannot communicate with device
	if (!VInit(&info->deviceEntry)) return paramErr;

	if (!VFeaturesOK()) goto fail;

	QInit(0, 1);

	// All our descriptors point into this wired-down page
	lpage = AllocPages(1, &ppage);
	if (lpage == NULL) goto fail;

	// Cannot go any further without touching virtqueues, which requires DRIVER_OK
	VDriverOK();

	return noErr;

fail:
	if (lpage) FreePages(lpage);
	VFail();
	return paramErr;
}

void DNotified(uint16_t q, uint16_t buf, size_t len, void *tag) {
}

void DConfigChange(void) {
}
