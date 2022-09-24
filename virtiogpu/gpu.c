/*
Concepts:
"Mode" means bit depth, not resolution nor refresh rate
*/

#include <Devices.h>
#include <DriverServices.h>
#include <NameRegistry.h>
#include <PCI.h>
#include <Video.h>
#include <VideoServices.h>
#include <string.h>

#include "viotransport.h"
#include "virtio-gpu-structs.h"
#include "lprintf.h"
#include "debugpollpatch.h"

static int interruptsOn = 1;
static InterruptServiceIDType interruptService;
struct virtio_gpu_ctrl_hdr *conf;
struct virtio_gpu_ctrl_hdr *obuf, *ibuf;
void *fb;
int W, H;

OSStatus DoDriverIO(AddressSpaceID spaceID, IOCommandID cmdID,
	IOCommandContents pb, IOCommandCode code, IOCommandKind kind);

static OSStatus initialize(DriverInitInfo *info);
static void queueSizer(uint16_t queue, uint16_t *count, size_t *osize, size_t *isize);
static void setVBL(void);
static OSStatus VBLBH(void *p1, void *p2);
static void scheduledRedraw(void);
static OSStatus finalize(DriverFinalInfo *info);
static OSStatus control(short csCode, void *param);
static OSStatus status(short csCode, void *param);
static void configChanged(void);
static OSStatus GetBaseAddr(VDPageInfo *rec);
static OSStatus MySetEntries(VDSetEntryRecord *rec);
static OSStatus DirectSetEntries(VDSetEntryRecord *rec);
static OSStatus GetEntries(VDSetEntryRecord *rec);
static OSStatus GetClutBehavior(VDClutBehavior *rec);
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
		err = control((*pb.pb).cntrlParam.csCode, *(void **)&(*pb.pb).cntrlParam.csParam);

		if ((*pb.pb).cntrlParam.csCode < sizeof(controlNames)/sizeof(*controlNames))
			lprintf("Control(%s) = %d\n", controlNames[(*pb.pb).cntrlParam.csCode], err);
		else
			lprintf("Control(%d) = %d\n", (*pb.pb).cntrlParam.csCode, err);
		break;
	case kStatusCommand:
		err = status((*pb.pb).cntrlParam.csCode, *(void **)&(*pb.pb).cntrlParam.csParam);

		if ((*pb.pb).cntrlParam.csCode < sizeof(statusNames)/sizeof(*statusNames))
			lprintf("Status(%s) = %d\n", statusNames[(*pb.pb).cntrlParam.csCode], err);
		else
			lprintf("Status(%d) = %d\n", (*pb.pb).cntrlParam.csCode, err);
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
	// size_t count = EndianSwap32Bit(((struct virtio_gpu_ctrl_hdr *)VTDeviceConfig)->num_scanouts);
	// size_t i;

	OSStatus err;
	err = VTInit(&info->deviceEntry, queueSizer, NULL /*queueRecv*/, configChanged);
	if (err) return err;

	conf = VTDeviceConfig;
	obuf = VTBuffers[0][0];
	ibuf = VTBuffers[0][1];

	memset(obuf, 0, sizeof(*obuf));
	obuf->le32_type = EndianSwap32Bit(VIRTIO_GPU_CMD_GET_DISPLAY_INFO);
	obuf->le32_flags = EndianSwap32Bit(VIRTIO_GPU_FLAG_FENCE);

	VTSend(0, 0);
	while (!VTDone(0)) {}

	if (EndianSwap32Bit(ibuf->le32_type) != VIRTIO_GPU_RESP_OK_DISPLAY_INFO) {
		lprintf("Did NOT get VIRTIO_GPU_RESP_OK_DISPLAY_INFO\n");
		return paramErr;
	}

	W = EndianSwap32Bit(((struct virtio_gpu_resp_display_info *)ibuf)->pmodes[0].r.le32_width);
	H = EndianSwap32Bit(((struct virtio_gpu_resp_display_info *)ibuf)->pmodes[0].r.le32_height);
	lprintf("display %dx%d\n", W, H);

	fb = PoolAllocateResident(64*1024*1024, false);
	lprintf("Allocated 64 mb FB at %08x\n", fb);
	{
		size_t x, y;
		uint32_t *ptr = (void *)fb;
		for (y=0; y<H; y++) {
			for (x=0; x<W; x++) {
				*ptr++ = ((x ^ y) & 1) ? 0x00ffffff : 0x00000000;
			}
		}
	}

	// Create a host resource using VIRTIO_GPU_CMD_RESOURCE_CREATE_2D.
	{
		struct virtio_gpu_resource_create_2d *buf = (void *)obuf;
		memset(buf, 0, sizeof(*buf));
		buf->hdr.le32_type = EndianSwap32Bit(VIRTIO_GPU_CMD_RESOURCE_CREATE_2D);
		buf->hdr.le32_flags = EndianSwap32Bit(VIRTIO_GPU_FLAG_FENCE);
		buf->le32_resource_id = EndianSwap32Bit(99); // guest-assigned
		buf->le32_format = EndianSwap32Bit(VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM);
		buf->le32_width = EndianSwap32Bit(W);
		buf->le32_height = EndianSwap32Bit(H);

		VTSend(0, 0);
		while (!VTDone(0)) {}

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

		VTSend(0, 0);
		while (!VTDone(0)) {}

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

		VTSend(0, 0);
		while (!VTDone(0)) {}

		lprintf("Reply to VIRTIO_GPU_CMD_SET_SCANOUT is %#x\n",
			EndianSwap32Bit(ibuf->le32_type));
	}

	VSLNewInterruptService(&info->deviceEntry, kVBLInterruptServiceType, &interruptService);

	setVBL();

	InstallDebugPollPatch(scheduledRedraw);

	return noErr;
}

static void queueSizer(uint16_t queue, uint16_t *count, size_t *osize, size_t *isize) {
	// Tiny queues
	*count = 4;
	*osize = 64;
	*isize = 2048;
}

static void setVBL(void) {
	AbsoluteTime time = AddDurationToAbsolute(10, UpTime());
	TimerID id;
	SetInterruptTimer(&time, VBLBH, NULL, &id);
}

static OSStatus VBLBH(void *p1, void *p2) {
	if (interruptsOn) {
		VSLDoInterruptService(interruptService);
	}

	scheduledRedraw();

	setVBL();
	return noErr;
}

static void scheduledRedraw(void) {
	if (!VTDone(0)) return; // cancel if still waiting

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
	return controlErr;
}

static OSStatus status(short csCode, void *param) {
	switch (csCode) {
		case cscGetBaseAddr: return GetBaseAddr(param);
		case cscGetClutBehavior: return GetClutBehavior(param);
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
	return statusErr;
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
	return statusErr;
}

// If the video card is an indexed device, the SetEntries control routine
// should change the contents of the card’s CLUT.
// --> csTable     Pointer to ColorSpec array
// --> csStart     First entry in table
// --> csCount     Number of entries to set
static OSStatus MySetEntries(VDSetEntryRecord *rec) {
	lprintf("SetEntries csStart=%d csCount=%d\n", rec->csStart, rec->csCount);
	return noErr;
}

// Normally, color table animation is not used on a direct device, but
// there are some special circumstances under which an application may want
// to change the color table hardware. The DirectSetEntries routine
// provides the direct device with indexed mode functionality identical to
// the regular SetEntries control routine.
static OSStatus DirectSetEntries(VDSetEntryRecord *rec) {
	return controlErr;
}

// Returns the specified number of consecutive CLUT entries, starting with
// the specified first entry.
// <-> csTable     Pointer to ColorSpec array
// --> csStart     First entry in table
// --> csCount     Number of entries to set
static OSStatus GetEntries(VDSetEntryRecord *rec) {
	int i;
	ColorSpec *array = (ColorSpec *)rec->csTable;
	lprintf("GetEntries csStart=%d csCount=%d\n", rec->csStart, rec->csCount);
	
	for (i=0; i<=rec->csCount; i++) {
		array[i].value = 0;
		array[i].rgb.red = 0;
		array[i].rgb.green = 0;
		array[i].rgb.blue = 0;
	}
	return noErr;
}

// Not well documented, but needed by MacsBug
static OSStatus GetClutBehavior(VDClutBehavior *rec) {
	*rec = kSetClutAtSetEntries;
	return noErr;
}

// Sets the gamma table in the driver that corrects RGB color values.
// --> csGTable    Pointer to gamma table
static OSStatus SetGamma(VDGammaRecord *rec) {
	return controlErr;
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
	return controlErr;
}

// Specify whether subsequent SetEntries calls fill a card’s CLUT with
// actual colors or with the luminance-equivalent gray tones.
// --> csMode      Enable or disable luminance mapping
static OSStatus SetGray(VDGrayRecord *rec) {
	return controlErr;
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
	return statusErr;
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
	interruptsOn = !rec->csMode;
	return noErr;
}

// Returns a value of 0 if VBL interrupts are enabled and a value of 1 if
// VBL interrupts are disabled.
// <-- csMode      Interrupts enabled or disabled
static OSStatus GetInterrupt(VDFlagRecord *rec) {
	rec->csMode = !interruptsOn;
	return noErr;
}

// GetSync and SetSync can be used to implement the VESA DPMS as well as
// enable a sync-on-green mode for the frame buffer.
static OSStatus SetSync(VDSyncInfoRec *rec) {
	return controlErr;
}

static OSStatus GetSync(VDSyncInfoRec *rec) {
	return statusErr;
}

// --> powerState  Switch display hardware to this state
// <-- powerFlags  Describes the status of the new state
static OSStatus SetPowerState(VDPowerStateRec *rec) {
	return controlErr;
}

// <-- powerState  Current power state of display hardware
// <-- powerFlags  Status of current state
static OSStatus GetPowerState(VDPowerStateRec *rec) {
	return statusErr;
}

// Save the preferred relative bit depth (depth mode) and display mode.
// This means that a PCI card should save this information in NVRAM so that
// it persists across system restarts.
// --> csMode      Relative bit depth of preferred resolution
// --> csData      DisplayModeID of preferred resolution
// --- csPage      Unused
// --- csBaseAddr  Unused
static OSStatus SavePreferredConfiguration(VDSwitchInfoRec *rec) {
	return controlErr;
}

// <-- csMode      Relative bit depth of preferred resolution
// <-- csData      DisplayModeID of preferred resolution
// --- csPage      Unused
// --- csBaseAddr  Unused
static OSStatus GetPreferredConfiguration(VDSwitchInfoRec *rec) {
	return statusErr;
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
	rec->csMode = kDepthMode1;
	rec->csPage = 0;
	rec->csBaseAddr = fb;
	return noErr;
}

// Like GetMode, except:
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
	return statusErr;
}

// Sets the pixel depth of the screen.
// --> csMode          Desired relative bit depth
// --- csData          Unused
// --> csPage          Desired display page
// <-- csBaseAddr      Base address of video RAM for this csMode
static OSStatus SetMode(VDPageInfo *rec) {
	return controlErr;
}

// --> csMode          Relative bit depth to switch to
// --> csData          DisplayModeID to switch into
// --> csPage          Video page number to switch into
// <-- csBaseAddr      Base address of the new DisplayModeID
static OSStatus SwitchMode(VDSwitchInfoRec *rec) {
	return controlErr;
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
	return statusErr;
}

// Looks quite a bit hard-coded, isn't it ?
// --> csDisplayModeID   ID of the desired DisplayModeID
// --> csDepthMode       Relative bit depth
// <-> *csVPBlockPtr     Pointer to a VPBlock
// <-- csPageCount       Number of pages supported for resolution
//                       and relative bit depth
// <-- csDeviceType      Direct, fixed, or CLUT
static OSStatus GetVideoParameters(VDVideoParametersInfoRec *rec) {
	lprintf("GetVideoParameters csDisplayModeID=%d csDepthMode=%d\n",
		rec->csDisplayModeID, rec->csDisplayModeID);

	memset(rec->csVPBlockPtr, 0, sizeof(*rec->csVPBlockPtr));
	rec->csVPBlockPtr->vpBaseOffset = 0; // For us, it's always 0
	rec->csVPBlockPtr->vpRowBytes = 4 * W;
	rec->csVPBlockPtr->vpBounds.top = 0; // Always 0
	rec->csVPBlockPtr->vpBounds.left = 0; // Always 0
	rec->csVPBlockPtr->vpBounds.bottom	= H;
	rec->csVPBlockPtr->vpBounds.right = W;
	rec->csVPBlockPtr->vpVersion = 0; // Always 0
	rec->csVPBlockPtr->vpPackType = 0; // Always 0
	rec->csVPBlockPtr->vpPackSize = 0; // Always 0
	rec->csVPBlockPtr->vpHRes = 0x00480000;	// Hard coded to 72 dpi
	rec->csVPBlockPtr->vpVRes = 0x00480000;	// Hard coded to 72 dpi
	rec->csVPBlockPtr->vpPixelType = 16;
	rec->csVPBlockPtr->vpPixelSize = 32;
	rec->csVPBlockPtr->vpCmpCount = 3;
	rec->csVPBlockPtr->vpCmpSize = 8;
	rec->csVPBlockPtr->vpPlaneBytes = 0;

	rec->csPageCount = 1;
	rec->csDeviceType = directType;

	return noErr;
}