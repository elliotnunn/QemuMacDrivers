/*
Concepts:
"Mode" means bit depth, not resolution nor refresh rate
*/

#include <Devices.h>
#include <DriverServices.h>
#include <NameRegistry.h>
#include <PCI.h>
#include <Video.h>
#include <string.h>

#include "viotransport.h"
#include "virtio-gpu-structs.h"
#include "lprintf.h"

static volatile int waiting;

OSStatus DoDriverIO(AddressSpaceID spaceID, IOCommandID cmdID,
	IOCommandContents pb, IOCommandCode code, IOCommandKind kind);

static OSStatus initialize(DriverInitInfo *info);
static void queueSizer(uint16_t queue, uint16_t *count, size_t *osize, size_t *isize);
static void setVBL(void);
static OSStatus VBLBH(void *p1, void *p2);
static void queueRecv(uint16_t queue, uint16_t buffer, size_t len);
static OSStatus finalize(DriverFinalInfo *info);
static OSStatus control(short csCode, void *param);
static OSStatus status(short csCode, void *param);
static void configChanged(void);
static OSStatus GetBaseAddr(VDPageInfo *rec);
static OSStatus MySetEntries(VDSetEntryRecord *rec);
static OSStatus DirectSetEntries(VDSetEntryRecord *rec);
static OSStatus GetEntries(VDSetEntryRecord *rec);
static OSStatus SetGamma(VDGammaRecord *rec);
static OSStatus GetGammaInfoList(VDGetGammaListRec *rec);
static OSStatus RetrieveGammaTable(VDRetrieveGammaRec *rec);
static OSStatus GetGamma(VDGammaRecord *rec);
static OSStatus GrayPage(VDPageInfo *rec);
static OSStatus SetGray(VDGrayRecord *rec);
static OSStatus GetPages(VDPageInfo *rec);
static OSStatus GetGray(VDGrayRecord *rec);
static OSStatus SupportsHardwareCursor(VDSupportsHardwareCursorRec *rec);
static OSStatus SetHardwareCursor(VDSetHardwareCursorRec *rec);
static OSStatus DrawHardwareCursor(VDDrawHardwareCursorRec *rec);
static OSStatus GetHardwareCursorDrawState(VDHardwareCursorDrawStateRec *rec);
static OSStatus SetInterrupt(VDFlagRecord *rec);
static OSStatus GetInterrupt(VDFlagRecord *rec);
static OSStatus SetSync(VDSyncInfoRec *rec);
static OSStatus GetSync(VDSyncInfoRec *rec);
static OSStatus SetPowerState(VDPowerStateRec *rec);
static OSStatus GetPowerState(VDPowerStateRec *rec);
static OSStatus SavePreferredConfiguration(VDSwitchInfoRec *rec);
static OSStatus GetPreferredConfiguration(VDSwitchInfoRec *rec);
static OSStatus GetConnection(VDDisplayConnectInfoRec *rec);
static OSStatus GetMode(VDPageInfo *rec);
static OSStatus GetCurMode(VDSwitchInfoRec *rec);
static OSStatus GetModeTiming(VDTimingInfoRec *rec);
static OSStatus SetMode(VDPageInfo *rec);
static OSStatus SwitchMode(VDSwitchInfoRec *rec);
static OSStatus GetNextResolution(VDResolutionInfoRec *rec);
static OSStatus GetVideoParameters(VDVideoParametersInfoRec *rec);

DriverDescription TheDriverDescription = {
	kTheDescriptionSignature,
	kInitialDriverDescriptor,
	"\ppci1af4,1050",
	0x00, 0x10, 0x80, 0x00, // v0.1
	kDriverIsUnderExpertControl |
		kDriverIsOpenedUponLoad,
	"\p.Display_Video_VirtIO",
	0, 0, 0, 0, 0, 0, 0, 0, // reserved
	1, // nServices
	kServiceCategoryNdrvDriver, kNdrvTypeIsVideo, 0x00, 0x10, 0x80, 0x00, //v0.1
};

static const char *controlNames[] = {
	"Reset",                        // 0
	"KillIO",                       // 1
	"SetMode",                      // 2
	"SetEntries",                   // 3
	"SetGamma",                     // 4
	"GrayScreen",                   // 5
	"SetGray",                      // 6
	"SetInterrupt",                 // 7
	"DirectSetEntries",             // 8
	"SetDefaultMode",               // 9
	"SwitchMode",                   // 10
	"SetSync",                      // 11
	NULL,                           // 12
	NULL,                           // 13
	NULL,                           // 14
	NULL,                           // 15
	"SavePreferredConfiguration",   // 16
	NULL,                           // 17
	NULL,                           // 18
	NULL,                           // 19
	NULL,                           // 20
	NULL,                           // 21
	"SetHardwareCursor",            // 22
	"DrawHardwareCursor",           // 23
	"SetConvolution",               // 24
	"SetPowerState",                // 25
	"PrivateControlCall",           // 26
	NULL,                           // 27
	"SetMultiConnect",              // 28
	"SetClutBehavior",              // 29
	NULL,                           // 30
	"SetDetailedTiming",            // 31
	NULL,                           // 32
	"DoCommunication",              // 33
	"ProbeConnection",              // 34
};

static const char *statusNames[] = {
	NULL,                           // 0
	NULL,                           // 1
	"GetMode",                      // 2
	"GetEntries",                   // 3
	"GetPages",                     // 4
	"GetBaseAddr",                  // 5
	"GetGray",                      // 6
	"GetInterrupt",                 // 7
	"GetGamma",                     // 8
	"GetDefaultMode",               // 9
	"GetCurMode",                   // 10
	"GetSync",                      // 11
	"GetConnection",                // 12
	"GetModeTiming",                // 13
	"GetModeBaseAddress",           // 14
	"GetScanProc",                  // 15
	"GetPreferredConfiguration",    // 16
	"GetNextResolution",            // 17
	"GetVideoParameters",           // 18
	NULL,                           // 19
	"GetGammaInfoList",             // 20
	"RetrieveGammaTable",           // 21
	"SupportsHardwareCursor",       // 22
	"GetHardwareCursorDrawState",   // 23
	"GetConvolution",               // 24
	"GetPowerState",                // 25
	"PrivateStatusCall",            // 26
	"GetDDCBlock",                  // 27
	"GetMultiConnect",              // 28
	"GetClutBehavior",              // 29
	"GetTimingRanges",              // 30
	"GetDetailedTiming",            // 31
	"GetCommunicationInfo",         // 32
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
		err = control(((CntrlParam *)pb)->csCode, (void *)((CntrlParam *)pb)->csParam);
		/*if (err == -1993)*/ {
			int n = ((CntrlParam *)pb)->csCode;
			if (n < sizeof(controlNames)/sizeof(*controlNames))
				lprintf("Control(%s) = %d\n", controlNames[n], err);
			else
				lprintf("Control(%d) = %d\n", n, err);
		}
		if (err == -1993) err = controlErr;
		break;
	case kStatusCommand:
		err = status(((CntrlParam *)pb)->csCode, (void *)((CntrlParam *)pb)->csParam);
		/*if (err == -1993)*/ {
			int n = ((CntrlParam *)pb)->csCode;
			if (n < sizeof(statusNames)/sizeof(*statusNames))
				lprintf("Status(%s) = %d\n", statusNames[n], err);
			else
				lprintf("Status(%d) = %d\n", n, err);
		}
		if (err == -1993) err = statusErr;
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

struct virtio_gpu_ctrl_hdr *conf;
struct virtio_gpu_ctrl_hdr *obuf, *ibuf;

int W, H;

void *fb;

static OSStatus initialize(DriverInitInfo *info) {
// 	size_t count = EndianSwap32Bit(((struct virtio_gpu_ctrl_hdr *)VTDeviceConfig)->num_scanouts);
// 	size_t i;

	OSStatus err;
	err = VTInit(&info->deviceEntry, queueSizer, queueRecv, configChanged);
	if (err) return err;

	conf = VTDeviceConfig;
	obuf = VTBuffers[0][0];
	ibuf = VTBuffers[0][1];

	memset(obuf, 0, sizeof(*obuf));
	obuf->le32_type = EndianSwap32Bit(VIRTIO_GPU_CMD_GET_DISPLAY_INFO);
	obuf->le32_flags = EndianSwap32Bit(VIRTIO_GPU_FLAG_FENCE);

	waiting = 1;
	VTSend(0, 0);
	while (waiting) {};

	if (EndianSwap32Bit(ibuf->le32_type) != VIRTIO_GPU_RESP_OK_DISPLAY_INFO) {
		lprintf("Did NOT get VIRTIO_GPU_RESP_OK_DISPLAY_INFO\n");
		return paramErr;
	}

	W = EndianSwap32Bit(((struct virtio_gpu_resp_display_info *)ibuf)->pmodes[0].r.le32_width);
	H = EndianSwap32Bit(((struct virtio_gpu_resp_display_info *)ibuf)->pmodes[0].r.le32_height);
	lprintf("display %dx%d\n", W, H);

	fb = PoolAllocateResident(64*1024*1024, false);
	lprintf("Allocated 64 mb FB at %08x\n", fb);

	// Create a host resource using VIRTIO_GPU_CMD_RESOURCE_CREATE_2D.
	{
		struct virtio_gpu_resource_create_2d *buf = (void *)obuf;
		memset(buf, 0, sizeof(*buf));
		buf->hdr.le32_type = EndianSwap32Bit(VIRTIO_GPU_CMD_RESOURCE_CREATE_2D);
		buf->hdr.le32_flags = EndianSwap32Bit(VIRTIO_GPU_FLAG_FENCE);
		buf->le32_resource_id = EndianSwap32Bit(99); // guest-assigned
		buf->le32_format = EndianSwap32Bit(VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM);
		buf->le32_width = EndianSwap32Bit(W);
		buf->le32_height = EndianSwap32Bit(H);

		waiting = 1;
		VTSend(0, 0);
		while (waiting) {};

		lprintf("Reply to VIRTIO_GPU_CMD_RESOURCE_CREATE_2D is %#x\n",
			EndianSwap32Bit(ibuf->le32_type));
	}

	// Allocate a framebuffer from guest ram, and attach it as backing
	// storage to the resource just created, using
	// VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING. Scatter lists are supported,
	// so the framebuffer doesn’t need to be contignous in guest physical
	// memory.
	{
		struct virtio_gpu_resource_attach_backing *buf = (void *)obuf;
		struct virtio_gpu_mem_entry *buf2 = (void *)((char *)buf + sizeof(*buf));
		memset(buf, 0, sizeof(*buf) + sizeof(*buf2));
		buf->hdr.le32_type = EndianSwap32Bit(VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING);
		buf->hdr.le32_flags = EndianSwap32Bit(VIRTIO_GPU_FLAG_FENCE);
		buf->le32_resource_id = EndianSwap32Bit(99); // guest-assigned
		buf->le32_nr_entries = EndianSwap32Bit(1);

		buf2->le32_addr = EndianSwap32Bit((uint32_t)fb + 0x4000); // obviously a bad hack
		buf2->le32_length = EndianSwap32Bit(64*1024*1024);

		waiting = 1;
		VTSend(0, 0);
		while (waiting) {};

		lprintf("Reply to VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING is %#x\n",
			EndianSwap32Bit(ibuf->le32_type));
	}

	// Use VIRTIO_GPU_CMD_SET_SCANOUT to link the framebuffer to a display
	// scanout.
	{
		struct virtio_gpu_set_scanout *buf = (void *)obuf;
		memset(buf, 0, sizeof(*buf));
		buf->hdr.le32_type = EndianSwap32Bit(VIRTIO_GPU_CMD_SET_SCANOUT);
		buf->hdr.le32_flags = EndianSwap32Bit(VIRTIO_GPU_FLAG_FENCE);

		buf->r.le32_x = 0;
		buf->r.le32_y = 0;
		buf->r.le32_width = EndianSwap32Bit(W);
		buf->r.le32_height = EndianSwap32Bit(H);

		buf->le32_scanout_id = EndianSwap32Bit(0); // index, 0-15
		buf->le32_resource_id = EndianSwap32Bit(99); // guest-assigned

		waiting = 1;
		VTSend(0, 0);
		while (waiting) {};

		lprintf("Reply to VIRTIO_GPU_CMD_SET_SCANOUT is %#x\n",
			EndianSwap32Bit(ibuf->le32_type));
	}

	setVBL();

	return noErr;
}

static void queueSizer(uint16_t queue, uint16_t *count, size_t *osize, size_t *isize) {
	// Tiny queues
	*count = 4;
	*osize = 64;
	*isize = 2048;
}

static void setVBL(void) {
	AbsoluteTime time = AddDurationToAbsolute(100, UpTime());
	TimerID id;
	SetInterruptTimer(&time, VBLBH, NULL, &id);
}

static OSStatus VBLBH(void *p1, void *p2) {
	int i;
	for (i=0; i<80*1024; i++) {
		((char *)fb)[i] = 0xff;
	}

	// Use VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D to update the host resource from guest memory.
	{
		struct virtio_gpu_transfer_to_host_2d *buf = (void *)VTBuffers[0][0];
		memset(buf, 0, sizeof(*buf));
		buf->hdr.le32_type = EndianSwap32Bit(VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D);
		buf->hdr.le32_flags = EndianSwap32Bit(VIRTIO_GPU_FLAG_FENCE);

		buf->r.le32_x = 0;
		buf->r.le32_y = 0;
		buf->r.le32_width = EndianSwap32Bit(W);
		buf->r.le32_height = EndianSwap32Bit(H);

		buf->le32_resource_id = EndianSwap32Bit(99); // guest-assigned

		VTSend(0, 0);
	}

	// Use VIRTIO_GPU_CMD_RESOURCE_FLUSH to flush the updated resource to the display.
	{
		struct virtio_gpu_resource_flush *buf = (void *)VTBuffers[0][2];
		memset(buf, 0, sizeof(*buf));
		buf->hdr.le32_type = EndianSwap32Bit(VIRTIO_GPU_CMD_RESOURCE_FLUSH);
		buf->hdr.le32_flags = EndianSwap32Bit(VIRTIO_GPU_FLAG_FENCE);

		buf->r.le32_x = 0;
		buf->r.le32_y = 0;
		buf->r.le32_width = EndianSwap32Bit(W);
		buf->r.le32_height = EndianSwap32Bit(H);

		buf->le32_resource_id = EndianSwap32Bit(99); // guest-assigned

		VTSend(0, 2);
	}

	setVBL();
	return noErr;
}

static void queueRecv(uint16_t queue, uint16_t buffer, size_t len) {
	waiting = 0;

	lprintf("Received: q%d buf%d reply=%#x\n", queue, buffer,
		EndianSwap32Bit(*(uint32_t *)(VTBuffers[queue][buffer+1])));
}

static OSStatus finalize(DriverFinalInfo *info) {
	return noErr;
}

static OSStatus control(short csCode, void *param) {
	switch (csCode) {
		case cscDirectSetEntries: return DirectSetEntries(param);
		case cscDrawHardwareCursor: return DrawHardwareCursor(param);
		case cscGrayPage: return GrayPage(param);
		case cscSavePreferredConfiguration: return SavePreferredConfiguration(param);
		case cscSetEntries: return MySetEntries(param);
		case cscSetGamma: return SetGamma(param);
		case cscSetGray: return SetGray(param);
		case cscSetHardwareCursor: return SetHardwareCursor(param);
		case cscSetInterrupt: return SetInterrupt(param);
		case cscSetMode: return SetMode(param);
		case cscSetPowerState: return SetPowerState(param);
		case cscSetSync: return SetSync(param);
		case cscSwitchMode: return SwitchMode(param);
	}
	return -1993;
}

static OSStatus status(short csCode, void *param) {
	switch (csCode) {
		case cscGetBaseAddr: return GetBaseAddr(param);
		case cscGetConnection: return GetConnection(param);
		case cscGetCurMode: return GetCurMode(param);
		case cscGetEntries: return GetEntries(param);
		case cscGetGamma: return GetGamma(param);
		case cscGetGammaInfoList: return GetGammaInfoList(param);
		case cscGetGray: return GetGray(param);
		case cscGetHardwareCursorDrawState: return GetHardwareCursorDrawState(param);
		case cscGetInterrupt: return GetInterrupt(param);
		case cscGetMode: return GetMode(param);
		case cscGetModeTiming: return GetModeTiming(param);
		case cscGetNextResolution: return GetNextResolution(param);
		case cscGetPages: return GetPages(param);
		case cscGetPowerState: return GetPowerState(param);
		case cscGetPreferredConfiguration: return GetPreferredConfiguration(param);
		case cscGetSync: return GetSync(param);
		case cscGetVideoParameters: return GetVideoParameters(param);
		case cscRetrieveGammaTable: return RetrieveGammaTable(param);
		case cscSupportsHardwareCursor: return SupportsHardwareCursor(param);
	}
	return -1993;
}

static void configChanged(void) {
	lprintf("configChanged\n");
}

// Returns the base address of a specified page in the current mode.
// --- csMode      Unused
// --- csData      Unused
// --> csPage      Desired page
// <-- csBaseAddr  Base address of VRAM for the desired page
static OSStatus GetBaseAddr(VDPageInfo *rec) {
	if (rec->csPage != 1) return -1993;
	rec->csBaseAddr = fb;
	return noErr;
}

// If the video card is an indexed device, the SetEntries control routine
// should change the contents of the card’s CLUT.
// --> csTable     Pointer to ColorSpec array
// --> csStart     First entry in table
// --> csCount     Number of entries to set
static OSStatus MySetEntries(VDSetEntryRecord *rec) {
	return noErr;
}

// Normally, color table animation is not used on a direct device, but
// there are some special circumstances under which an application may want
// to change the color table hardware. The DirectSetEntries routine
// provides the direct device with indexed mode functionality identical to
// the regular SetEntries control routine.
static OSStatus DirectSetEntries(VDSetEntryRecord *rec) {
	return noErr;
}

// Returns the specified number of consecutive CLUT entries, starting with
// the specified first entry.
// <-> csTable     Pointer to ColorSpec array
// --> csStart     First entry in table
// --> csCount     Number of entries to set
static OSStatus GetEntries(VDSetEntryRecord *rec) {
//
//
// 	Boolean useValue	= (entryRecord->csStart < 0);
// 	UInt32	start		= useValue ? 0UL : (UInt32)entryRecord->csStart;
// 	UInt32	stop		= start + entryRecord->csCount;
// 	UInt32	i;
//
// 	Trace(GraphicsCoreGetEntries);
//
// 	if (GLOBAL.depth != 8)
// 		return controlErr;
// 	for(i=start;i<=stop;i++) {
// 		UInt32	colorIndex = useValue ? entryRecord->csTable[i].value : i;
// 		QemuVga_GetColorEntry(colorIndex, &entryRecord->csTable[i].rgb);
// 	}
//
// 	return noErr;
//
//

	return statusErr;
}

// Sets the gamma table in the driver that corrects RGB color values.
// --> csGTable    Pointer to gamma table
static OSStatus SetGamma(VDGammaRecord *rec) {
	return noErr;
}

// Returns a pointer to the current gamma table.
// <-- csGTable    Pointer to gamma table
static OSStatus GetGamma(VDGammaRecord *rec) {
	return statusErr;
}

// Clients wishing to find a graphics card’s available gamma tables
// formerly accessed the Slot Manager data structures. PCI graphics drivers
// must return this information directly.
// --> csPreviousGammaTableID  ID of the previous gamma table
// <-- csGammaTableID          ID of the gamma table following
//                             csPreviousDisplayModeID
// <-- csGammaTableSize        Size of the gamma table in bytes
// <-- csGammaTableName        Gamma table name (C string)
static OSStatus GetGammaInfoList(VDGetGammaListRec *rec) {
	return statusErr;
}

// Copies the designated gamma table into the designated location.
// --> csGammaTableID      ID of gamma table to retrieve
// <-> csGammaTablePtr     Location to copy table into
static OSStatus RetrieveGammaTable(VDRetrieveGammaRec *rec) {
	return statusErr;
}

// Fills the specified video page with a dithered gray pattern in the
// current video mode. The page number is 0 based.
// --- csMode      Unused
// --- csData      Unused
// --> csPage      Desired display page to gray
// --- csBaseAddr  Unused
static OSStatus GrayPage(VDPageInfo *rec) {
	lprintf("Gray page requested\n");
	return noErr;
}

// Specify whether subsequent SetEntries calls fill a card’s CLUT with
// actual colors or with the luminance-equivalent gray tones.
// --> csMode      Enable or disable luminance mapping
static OSStatus SetGray(VDGrayRecord *rec) {
	// rec->csMode
	return noErr;
}

// Describes the behavior of subsequent SetEntries control calls to indexed
// devices.
// <-- csMode      Luminance mapping enabled or disabled
static OSStatus GetGray(VDGrayRecord *rec) {
	rec->csMode = 0;
	return noErr;
}

// Returns the total number of video pages available in the current video
// card mode, not the current page number. This is a counting number and is
// not 0 based.
// --- csMode      Unused
// --- csData      Unused
// <-- csPage      Number of display pages available
// --- csBaseAddr  Unused
static OSStatus GetPages(VDPageInfo *rec) {
	return -1993;
	/*
	UInt32 pageCount, depth;

	CHECK_OPEN( statusErr );

	Trace(GetPages);

	depth = DepthModeToDepth(pageInfo->csMode);
	QemuVga_GetModePages(GLOBAL.curMode, depth, NULL, &pageCount);
	pageInfo->csPage = pageCount;

	return noErr;
	*/
}

// Graphics drivers that support hardware cursors must return true.
// <-- csSupportsHardwareCursor  true if hardware cursor is supported
static OSStatus SupportsHardwareCursor(VDSupportsHardwareCursorRec *rec) {
	rec->csSupportsHardwareCursor = 0;
	return noErr;
}

// QuickDraw uses the SetHardwareCursor control call to set up the hardware
// cursor and determine whether the hardware can support it. The driver
// must determine whether it can support the given cursor and, if so,
// program the hardware cursor frame buffer (or equivalent), set up the
// CLUT, and return noErr. If the driver cannot support the cursor it must
// return controlErr. The driver must remember whether this call was
// successful for subsequent GetHardwareCursorDrawState or
// DrawHardwareCursor calls, but should not change the cursor’s x or y
// coordinates or its visible state.
//  --> csCursorRef    Reference to cursor data
static OSStatus SetHardwareCursor(VDSetHardwareCursorRec *rec) {
	return controlErr;
}

// Sets the cursor’s x and y coordinates and visible state. If the cursor
// was successfully set by a previous call to SetHardwareCursor, the driver
// must program the hardware with the given x, y, and visible parameters
// and then return noErr. If the cursor was not successfully set by the
// last SetHardwareCursor call, the driver must return controlErr.
// --> csCursorX           X coordinate
// --> csCursorY           Y coordinate
// --> csCursorVisible     true if the cursor must be visible
static OSStatus DrawHardwareCursor(VDDrawHardwareCursorRec *rec) {
	return controlErr;
}

// The csCursorSet parameter should be true if the last SetHardwareCursor
// control call was successful and false otherwise. If csCursorSet is true,
// the csCursorX, csCursorY, and csCursorVisible values must match the
// parameters passed in to the last DrawHardwareCursor control call.
// <-- csCursorX           X coordinate from last DrawHardwareCursor call
// <-- csCursorY           Y coordinate from last DrawHardwareCursor call
// <-- csCursorVisible     true if the cursor is visible
// <-- csCursorSet         true if cursor was successfully set by the last
//                         SetHardwareCursor call
static OSStatus GetHardwareCursorDrawState(VDHardwareCursorDrawStateRec *rec) {
	return statusErr;
}

// To enable interrupts, pass a csMode value of 0; to disable interrupts,
// pass a csMode value of 1.
// --> csMode      Enable or disable interrupts
static OSStatus SetInterrupt(VDFlagRecord *rec) {
	return -1993;
	/*
	CHECK_OPEN( controlErr );

	Trace(SetInterrupt);

	if (!flagRecord->csMode)
	    QemuVga_EnableInterrupts();
	else
	    QemuVga_DisableInterrupts();

	return noErr;
	*/
}

// Returns a value of 0 if VBL interrupts are enabled and a value of 1 if
// VBL interrupts are disabled.
// <-- csMode      Interrupts enabled or disabled
static OSStatus GetInterrupt(VDFlagRecord *rec) {
	return -1993;
	/*
	Trace(GetInterrupt);

	CHECK_OPEN( statusErr );

	flagRecord->csMode = !GLOBAL.qdInterruptsEnable;
	return noErr;
	*/
}

// GetSync and SetSync can be used to implement the VESA DPMS as well as
// enable a sync-on-green mode for the frame buffer.
static OSStatus SetSync(VDSyncInfoRec *rec) {
	return -1993;
	/*
	UInt8 sync, mask;

	Trace(SetSync);

	CHECK_OPEN( controlErr );

	sync = syncInfo->csMode;
	mask = syncInfo->csFlags;

	/* Unblank shortcut
	if (sync == 0 && mask == 0) {
		sync = 0;
		mask = kDPMSSyncMask;
	}
	/* Blank shortcut
	if (sync == 0xff && mask == 0xff) {
		sync = 0x7;
		mask = kDPMSSyncMask;
	}

	lprintf("SetSync req: sync=%x mask=%x\n", sync, mask);

	/* Only care about the DPMS mode
	if ((mask & kDPMSSyncMask) == 0)
		return noErr;

	/* If any sync is disabled, blank
	if (sync & kDPMSSyncMask)
		QemuVga_Blank(true);
	else
		QemuVga_Blank(false);

	return noErr;
	*/
}

static OSStatus GetSync(VDSyncInfoRec *rec) {
	return -1993;
	/*
	Trace(GetSync);

	if (syncInfo->csMode == 0xff) {
		/* Return HW caps
		syncInfo->csMode = (1 << kDisableHorizontalSyncBit) |
						   (1 << kDisableVerticalSyncBit) |
						   (1 << kDisableCompositeSyncBit) |
						   (1 << kNoSeparateSyncControlBit);
	} else if (syncInfo->csMode == 0x00){
		syncInfo->csMode = GLOBAL.blanked ? kDPMSSyncMask : 0;
	} else
		return statusErr;

	syncInfo->csFlags = 0;

	return noErr;
	*/
}

// --> powerState  Switch display hardware to this state
// <-- powerFlags  Describes the status of the new state
static OSStatus SetPowerState(VDPowerStateRec *rec) {
	return -1993;
	/*
	Trace(SetPowerState);

	return paramErr;
	*/
}

// <-- powerState  Current power state of display hardware
// <-- powerFlags  Status of current state
static OSStatus GetPowerState(VDPowerStateRec *rec) {
	return -1993;
	/*
	Trace(GetPowerState);

	return paramErr;
	*/
}

// Save the preferred relative bit depth (depth mode) and display mode.
// This means that a PCI card should save this information in NVRAM so that
// it persists across system restarts.
// --> csMode      Relative bit depth of preferred resolution
// --> csData      DisplayModeID of preferred resolution
// --- csPage      Unused
// --- csBaseAddr  Unused
static OSStatus SavePreferredConfiguration(VDSwitchInfoRec *rec) {
	return -1993;
	/*
	Trace(SavePreferredConfiguration);

	CHECK_OPEN( controlErr );

	return noErr;
	*/
}

// <-- csMode      Relative bit depth of preferred resolution
// <-- csData      DisplayModeID of preferred resolution
// --- csPage      Unused
// --- csBaseAddr  Unused
static OSStatus GetPreferredConfiguration(VDSwitchInfoRec *rec) {
	return -1993;
	/*
	Trace(GetPreferredConfiguration);

	CHECK_OPEN( statusErr );

	switchInfo->csMode 	 	= DepthToDepthMode(GLOBAL.bootDepth);
	switchInfo->csData		= GLOBAL.bootMode + 1; /* Modes are 1 based
	switchInfo->csPage		= 0;
	switchInfo->csBaseAddr	= FB_START;

	return noErr;
	*/
}

// Gathers information about the attached display.
// <-- csDisplayType         Display type of attached display
// <-- csConnectTaggedType   Type of tagging
// <-- csConnectTaggedData   Tagging data
// <-- csConnectFlags        Connection flags
// <-- csDisplayComponent    Return display component, if available
static OSStatus GetConnection(VDDisplayConnectInfoRec *rec) {
	rec->csDisplayType = kGenericLCD;
	rec->csConnectTaggedType = 0;
	rec->csConnectTaggedData = 0;
	rec->csConnectFlags = (1 << kTaggingInfoNonStandard) | (1 << kUncertainConnection);
	rec->csDisplayComponent = 0;
	return noErr;
}

// Returns the current relative bit depth, page, and base address.
// <-- csMode      Current relative bit depth
// --- csData      Unused
// <-- csPage      Current display page
// <-- csBaseAddr  Base address of video RAM for the current
//                 DisplayModeID and relative bit depth
static OSStatus GetMode(VDPageInfo *rec) {
	return -1993;
	/*
	Trace(GetMode);

	CHECK_OPEN( statusErr );

	pageInfo->csMode		= DepthToDepthMode(GLOBAL.depth);
	pageInfo->csPage		= GLOBAL.curPage;
	pageInfo->csBaseAddr	= GLOBAL.curBaseAddress;

	return noErr;
	*/
}

// PCI graphics drivers return the current DisplayModeID value in the
// csData field.
// <-- csMode      Current relative bit depth
// <-- csData      DisplayModeID of current resolution
// <-- csPage      Current page
// <-- csBaseAddr  Base address of current page
static OSStatus GetCurMode(VDSwitchInfoRec *rec) {
	rec->csMode = kDepthMode1;
	rec->csData = 1;
	rec->csPage = 0;
	rec->csBaseAddr = fb;
	return noErr;
}

// Report timing information for the desired displayModeID.
// --> csTimingMode    Desired DisplayModeID
// <-- csTimingFormat  Format for timing info (kDeclROMtables)
// <-- csTimingData    Scan timing for desired DisplayModeID
// <-- csTimingFlags   Report whether this scan timing is optional or required
static OSStatus GetModeTiming(VDTimingInfoRec *rec) {
	lprintf("GetModeTiming csTimingMode=%d\n", rec->csTimingMode);

	//if (rec->csTimingMode != 1) return paramErr;

	rec->csTimingFormat = kDeclROMtables;
	rec->csTimingData = timingVESA_640x480_60hz;
	rec->csTimingFlags = (1 << kModeValid) | (1 << kModeDefault) | (1 <<kModeSafe);
	return noErr;
}

// Sets the pixel depth of the screen.
// --> csMode          Desired relative bit depth
// --- csData          Unused
// --> csPage          Desired display page
// <-- csBaseAddr      Base address of video RAM for this csMode
static OSStatus SetMode(VDPageInfo *rec) {
	return -1993;
	/*
	UInt32 newDepth, newPage, pageCount;

	Trace(SetMode);

	CHECK_OPEN(controlErr);

	newDepth = DepthModeToDepth(pageInfo->csMode);
	newPage = pageInfo->csPage;
	QemuVga_GetModePages(GLOBAL.curMode, newDepth, NULL, &pageCount);

	lprintf("Requested depth=%d page=%d\n", newDepth, newPage);
	if (pageInfo->csPage >= pageCount)
		return paramErr;

	if (newDepth != GLOBAL.depth || newPage != GLOBAL.curPage)
		QemuVga_SetMode(GLOBAL.curMode, newDepth, newPage);

	pageInfo->csBaseAddr = GLOBAL.curBaseAddress;
	lprintf("Returning BA: %lx\n", pageInfo->csBaseAddr);

	return noErr;
	*/
}

// --> csMode          Relative bit depth to switch to
// --> csData          DisplayModeID to switch into
// --> csPage          Video page number to switch into
// <-- csBaseAddr      Base address of the new DisplayModeID
static OSStatus SwitchMode(VDSwitchInfoRec *rec) {
	return -1993;
	/*
	UInt32 newMode, newDepth, newPage, pageCount;

	Trace(SwitchMode);

	CHECK_OPEN(controlErr);

	newMode = switchInfo->csData - 1;
	newDepth = DepthModeToDepth(switchInfo->csMode);
	newPage = switchInfo->csPage;
	QemuVga_GetModePages(GLOBAL.curMode, newDepth, NULL, &pageCount);

	if (newPage >= pageCount)
		return paramErr;

	if (newMode != GLOBAL.curMode || newDepth != GLOBAL.depth ||
	    newPage != GLOBAL.curPage) {
		if (QemuVga_SetMode(newMode, newDepth, newPage))
			return controlErr;
	}
	switchInfo->csBaseAddr = GLOBAL.curBaseAddress;

	return noErr;
	*/
}

// Reports all display resolutions that the driver supports.
// --> csPreviousDisplayModeID   ID of the previous display mode
// <-- csDisplayModeID           ID of the display mode following
//                               csPreviousDisplayModeID
// <-- csHorizontalPixels        Number of pixels in a horizontal line
// <-- csVerticalLines           Number of lines in a screen
// <-- csRefreshRate             Vertical refresh rate of the screen
// <-- csMaxDepthMode            Max relative bit depth for this DisplayModeID
static OSStatus GetNextResolution(VDResolutionInfoRec *rec) {
	return -1993;
	/*
	UInt32 width, height;
	int id = resInfo->csPreviousDisplayModeID;

	Trace(GetNextResolution);

	CHECK_OPEN(statusErr);

	if (id == kDisplayModeIDFindFirstResolution)
		id = 0;
	else if (id == kDisplayModeIDCurrent)
		id = GLOBAL.curMode;
	id++;

	if (id == GLOBAL.numModes + 1) {
		resInfo->csDisplayModeID = kDisplayModeIDNoMoreResolutions;
		return noErr;
	}
	if (id < 1 || id > GLOBAL.numModes)
		return paramErr;

	if (QemuVga_GetModeInfo(id - 1, &width, &height))
		return paramErr;

	resInfo->csDisplayModeID	= id;
	resInfo->csHorizontalPixels	= width;
	resInfo->csVerticalLines	= height;
	resInfo->csRefreshRate		= 60;
	resInfo->csMaxDepthMode		= MAX_DEPTH_MODE; /* XXX Calculate if it fits !

	return noErr;
	*/
}

// Looks quite a bit hard-coded, isn't it ?
// --> csDisplayModeID   ID of the desired DisplayModeID
// --> csDepthMode       Relative bit depth
// <-> *csVPBlockPtr     Pointer to a VPBlock
// <-- csPageCount       Number of pages supported for resolution
//                       and relative bit depth
// <-- csDeviceType      Direct, fixed, or CLUT
static OSStatus GetVideoParameters(VDVideoParametersInfoRec *rec) {
	return -1993;
	/*
	UInt32 width, height, depth, rowBytes, pageCount;
	OSStatus err = noErr;

	Trace(GetVideoParameters);

	CHECK_OPEN(statusErr);

	if (videoParams->csDisplayModeID < 1 || videoParams->csDisplayModeID > GLOBAL.numModes)
		return paramErr;
	if (videoParams->csDepthMode > MAX_DEPTH_MODE)
		return paramErr;
	if (QemuVga_GetModeInfo(videoParams->csDisplayModeID - 1, &width, &height))
		return paramErr;

	depth = DepthModeToDepth(videoParams->csDepthMode);
	QemuVga_GetModePages(videoParams->csDisplayModeID - 1, depth, NULL, &pageCount);
	videoParams->csPageCount = pageCount;
	lprintf("Video Params says %d pages\n", pageCount);

	rowBytes = width * ((depth + 7) / 8);
	(videoParams->csVPBlockPtr)->vpBaseOffset 		= 0;			// For us, it's always 0
	(videoParams->csVPBlockPtr)->vpBounds.top 		= 0;			// Always 0
	(videoParams->csVPBlockPtr)->vpBounds.left 		= 0;			// Always 0
	(videoParams->csVPBlockPtr)->vpVersion 			= 0;			// Always 0
	(videoParams->csVPBlockPtr)->vpPackType 		= 0;			// Always 0
	(videoParams->csVPBlockPtr)->vpPackSize 		= 0;			// Always 0
	(videoParams->csVPBlockPtr)->vpHRes 			= 0x00480000;	// Hard coded to 72 dpi
	(videoParams->csVPBlockPtr)->vpVRes 			= 0x00480000;	// Hard coded to 72 dpi
	(videoParams->csVPBlockPtr)->vpPlaneBytes 		= 0;			// Always 0
	(videoParams->csVPBlockPtr)->vpBounds.bottom	= height;
	(videoParams->csVPBlockPtr)->vpBounds.right		= width;
	(videoParams->csVPBlockPtr)->vpRowBytes			= rowBytes;

	switch (depth) {
	case 8:
		videoParams->csDeviceType 						= clutType;
		(videoParams->csVPBlockPtr)->vpPixelType 		= 0;
		(videoParams->csVPBlockPtr)->vpPixelSize 		= 8;
		(videoParams->csVPBlockPtr)->vpCmpCount 		= 1;
		(videoParams->csVPBlockPtr)->vpCmpSize 			= 8;
		(videoParams->csVPBlockPtr)->vpPlaneBytes 		= 0;
		break;
	case 15:
	case 16:
		videoParams->csDeviceType 						= directType;
		(videoParams->csVPBlockPtr)->vpPixelType 		= 16;
		(videoParams->csVPBlockPtr)->vpPixelSize 		= 16;
		(videoParams->csVPBlockPtr)->vpCmpCount 		= 3;
		(videoParams->csVPBlockPtr)->vpCmpSize 			= 5;
		(videoParams->csVPBlockPtr)->vpPlaneBytes 		= 0;
		break;
	case 32:
		videoParams->csDeviceType 						= directType;
		(videoParams->csVPBlockPtr)->vpPixelType 		= 16;
		(videoParams->csVPBlockPtr)->vpPixelSize 		= 32;
		(videoParams->csVPBlockPtr)->vpCmpCount 		= 3;
		(videoParams->csVPBlockPtr)->vpCmpSize 			= 8;
		(videoParams->csVPBlockPtr)->vpPlaneBytes 		= 0;
		break;
	default:
		err = paramErr;
		break;
	}

	return err;
	*/
}
