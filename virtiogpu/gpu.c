/*
Concepts:
"Mode" means bit depth, not resolution nor refresh rate
*/

#include <Devices.h>
#include <DriverServices.h>
#include <LowMem.h>
#include <NameRegistry.h>
#include <PCI.h>
#include <Video.h>
#include <VideoServices.h>
#include <string.h>

#include "viotransport.h"
#include "virtio-gpu-structs.h"
#include "lprintf.h"
#include "debugpollpatch.h"
#include "dirtyrectpatch.h"
#include "hardwarecursor.h"

#define MAXEDGE 1024
#define TRACECALLS 0
#define SCREEN_RESOURCE 99

// The classics
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

static int interruptsOn = 1;
static InterruptServiceIDType interruptService;
void *backbuf, *frontbuf;
int W, H;
int qdUpdatesWorking;

OSStatus DoDriverIO(AddressSpaceID spaceID, IOCommandID cmdID,
	IOCommandContents pb, IOCommandCode code, IOCommandKind kind);

static OSStatus initialize(DriverInitInfo *info);
static void queueSizer(uint16_t queue, uint16_t *count, size_t *osize, size_t *isize);
static void setVBL(void);
static OSStatus VBLBH(void *p1, void *p2);
static uint32_t checksum(uint32_t pixel);
static uint32_t checksumField(uint32_t pixel);
static uint32_t setChecksumField(uint32_t pixel, uint32_t checksum);
static void qdScreenUpdated(short top, short left, short bottom, short right);
static void updateScreen(short top, short left, short bottom, short right);
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

unsigned char gamma[] = {
	0x00, 0x04, 0x07, 0x09, 0x0b, 0x0d, 0x0f, 0x11,
	0x13, 0x15, 0x16, 0x18, 0x1a, 0x1b, 0x1d, 0x1e,
	0x20, 0x21, 0x23, 0x24, 0x26, 0x27, 0x29, 0x2a,
	0x2b, 0x2d, 0x2e, 0x2f, 0x31, 0x32, 0x33, 0x34,
	0x36, 0x37, 0x38, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e,
	0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x47, 0x48,
	0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x50, 0x51,
	0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
	0x5a, 0x5b, 0x5d, 0x5e, 0x5f, 0x60, 0x61, 0x62,
	0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a,
	0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72,
	0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a,
	0x7b, 0x7c, 0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81,
	0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
	0x8a, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0x90,
	0x91, 0x92, 0x93, 0x94, 0x94, 0x95, 0x96, 0x97,
	0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9d, 0x9e,
	0x9f, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa4, 0xa5,
	0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xaa, 0xab, 0xac,
	0xad, 0xae, 0xaf, 0xb0, 0xb0, 0xb1, 0xb2, 0xb3,
	0xb4, 0xb5, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba,
	0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf, 0xbf, 0xc0,
	0xc1, 0xc2, 0xc3, 0xc4, 0xc4, 0xc5, 0xc6, 0xc7,
	0xc8, 0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xcd,
	0xce, 0xcf, 0xd0, 0xd1, 0xd1, 0xd2, 0xd3, 0xd4,
	0xd5, 0xd5, 0xd6, 0xd7, 0xd8, 0xd8, 0xd9, 0xda,
	0xdb, 0xdc, 0xdc, 0xdd, 0xde, 0xdf, 0xe0, 0xe0,
	0xe1, 0xe2, 0xe3, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
	0xe7, 0xe8, 0xe9, 0xea, 0xea, 0xeb, 0xec, 0xed,
	0xee, 0xee, 0xef, 0xf0, 0xf1, 0xf1, 0xf2, 0xf3,
	0xf4, 0xf4, 0xf5, 0xf6, 0xf7, 0xf7, 0xf8, 0xf9,
	0xfa, 0xfa, 0xfb, 0xfc, 0xfd, 0xfd, 0xfe, 0xff,
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
		if (TRACECALLS) {
			if ((*pb.pb).cntrlParam.csCode < sizeof(controlNames)/sizeof(*controlNames)) {
				lprintf("Control(%s)\n", controlNames[(*pb.pb).cntrlParam.csCode]);
			} else {
				lprintf("Control(%d)\n", (*pb.pb).cntrlParam.csCode);
			}
		}

		err = control((*pb.pb).cntrlParam.csCode, *(void **)&(*pb.pb).cntrlParam.csParam);
		if (TRACECALLS) lprintf("    = %d\n", err);
		break;
	case kStatusCommand:
		if (TRACECALLS) {
			if ((*pb.pb).cntrlParam.csCode < sizeof(controlNames)/sizeof(*controlNames)) {
				lprintf("Status(%s)\n", controlNames[(*pb.pb).cntrlParam.csCode]);
			} else {
				lprintf("Status(%d)\n", (*pb.pb).cntrlParam.csCode);
			}
		}

		err = status((*pb.pb).cntrlParam.csCode, *(void **)&(*pb.pb).cntrlParam.csParam);
		if (TRACECALLS) lprintf("    = %d\n", err);
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
	OSStatus err;
	err = VTInit(&info->deviceEntry, queueSizer, NULL /*queueRecv*/, configChanged);
	if (err) return err;

	// Allocate our two enormous framebuffers
	backbuf = PoolAllocateResident(MAXEDGE * MAXEDGE * 4, true);
	if (backbuf == NULL) return paramErr;
	backbuf = (void *)(((long)backbuf + 0xfff) & ~0xfff);

	frontbuf = PoolAllocateResident(MAXEDGE * MAXEDGE * 4 + 0x1000, true);
	if (frontbuf == NULL) return paramErr;
	frontbuf = (void *)(((long)frontbuf + 0xfff) & ~0xfff);

	InitHardwareCursor();

	// Get a list of displays ("scanouts") and their sizes
	// (for now, only the first display)
	{
		struct virtio_gpu_ctrl_hdr *req = VTBuffers[0][0];
		struct virtio_gpu_resp_display_info *reply = VTBuffers[0][1];

		memset(req, 0, sizeof(*req));
		req->le32_type = EndianSwap32Bit(VIRTIO_GPU_CMD_GET_DISPLAY_INFO);
		req->le32_flags = EndianSwap32Bit(VIRTIO_GPU_FLAG_FENCE);

		VTSend(0, 0);
		while (!VTDone(0)) {}

		if (EndianSwap32Bit(reply->hdr.le32_type) != VIRTIO_GPU_RESP_OK_DISPLAY_INFO) return paramErr;
		W = EndianSwap32Bit(reply->pmodes[0].r.le32_width);
		H = EndianSwap32Bit(reply->pmodes[0].r.le32_height);
		if (W > MAXEDGE || H > MAXEDGE) return paramErr;
	}

	// Create a host resource using VIRTIO_GPU_CMD_RESOURCE_CREATE_2D.
	{
		struct virtio_gpu_resource_create_2d *req = VTBuffers[0][0];
		struct virtio_gpu_ctrl_hdr *reply = VTBuffers[0][1];

		memset(req, 0, sizeof(*req));
		req->hdr.le32_type = EndianSwap32Bit(VIRTIO_GPU_CMD_RESOURCE_CREATE_2D);
		req->hdr.le32_flags = EndianSwap32Bit(VIRTIO_GPU_FLAG_FENCE);
		req->le32_resource_id = EndianSwap32Bit(SCREEN_RESOURCE);
		req->le32_format = EndianSwap32Bit(VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM);
		req->le32_width = EndianSwap32Bit(W);
		req->le32_height = EndianSwap32Bit(H);

		VTSend(0, 0);
		while (!VTDone(0)) {}

		if (EndianSwap32Bit(reply->le32_type) != VIRTIO_GPU_RESP_OK_NODATA) return paramErr;
	}

	// Allocate a framebuffer from guest ram, and attach it as backing
	// storage to the resource just created, using
	// VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING. Scatter lists are supported,
	// so the framebuffer doesn’t need to be contignous in guest physical
	// memory.
	{
		struct virtio_gpu_resource_attach_backing *req = VTBuffers[0][0];
		struct virtio_gpu_mem_entry *req2 = (void *)((char *)req + sizeof(*req));
		struct virtio_gpu_ctrl_hdr *reply = VTBuffers[0][1];

		memset(req, 0, sizeof(*req) + sizeof(*req2));
		req->hdr.le32_type = EndianSwap32Bit(VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING);
		req->hdr.le32_flags = EndianSwap32Bit(VIRTIO_GPU_FLAG_FENCE);
		req->le32_resource_id = EndianSwap32Bit(SCREEN_RESOURCE);
		req->le32_nr_entries = EndianSwap32Bit(1);
		req2->le32_addr = EndianSwap32Bit((uint32_t)frontbuf + 0x4000);
		req2->le32_length = EndianSwap32Bit(64*1024*1024);

		VTSend(0, 0);
		while (!VTDone(0)) {}

		if (EndianSwap32Bit(reply->le32_type) != VIRTIO_GPU_RESP_OK_NODATA) return paramErr;
	}

	// Use VIRTIO_GPU_CMD_SET_SCANOUT to link the framebuffer to a display
	// scanout.
	{
		struct virtio_gpu_set_scanout *req = VTBuffers[0][0];
		struct virtio_gpu_ctrl_hdr *reply = VTBuffers[0][1];
		memset(req, 0, sizeof(*req));

		req->hdr.le32_type = EndianSwap32Bit(VIRTIO_GPU_CMD_SET_SCANOUT);
		req->hdr.le32_flags = EndianSwap32Bit(VIRTIO_GPU_FLAG_FENCE);
		req->r.le32_x = 0;
		req->r.le32_y = 0;
		req->r.le32_width = EndianSwap32Bit(W);
		req->r.le32_height = EndianSwap32Bit(H);
		req->le32_scanout_id = EndianSwap32Bit(0); // index, 0-15
		req->le32_resource_id = EndianSwap32Bit(SCREEN_RESOURCE);

		VTSend(0, 0);
		while (!VTDone(0)) {}

		if (EndianSwap32Bit(reply->le32_type) != VIRTIO_GPU_RESP_OK_NODATA) return paramErr;
	}

	VSLNewInterruptService(&info->deviceEntry, kVBLInterruptServiceType, &interruptService);
	setVBL();

	InstallDirtyRectPatch(qdScreenUpdated);

	// With copying:
	// Performance test: 1x1 at 6744 Hz
	// Performance test: 2x2 at 6819 Hz
	// Performance test: 4x4 at 6786 Hz
	// Performance test: 8x8 at 6827 Hz
	// Performance test: 16x16 at 6560 Hz
	// Performance test: 32x32 at 5719 Hz
	// Performance test: 64x64 at 3333 Hz
	// Performance test: 128x128 at 1313 Hz
	// Performance test: 256x256 at 400 Hz
	// Performance test: 512x512 at 103 Hz
	// (removing gamma lookup makes minimal difference)

	// Without copying:
	// Performance test: 1x1 at 12445 Hz
	// Performance test: 2x2 at 12479 Hz
	// Performance test: 4x4 at 12546 Hz
	// Performance test: 8x8 at 12675 Hz
	// Performance test: 16x16 at 12656 Hz
	// Performance test: 32x32 at 12643 Hz
	// Performance test: 64x64 at 12532 Hz
	// Performance test: 128x128 at 12376 Hz
	// Performance test: 256x256 at 10937 Hz
	// Performance test: 512x512 at 7667 Hz
	{
		int size;
		long t;
		long n;
		for (size=1; size<=512; size*=2) {
			t = LMGetTicks() + 1;
			while (t > LMGetTicks()) {}
			t += 60;
			n = 0;
			while (t > LMGetTicks()) {
				updateScreen(0, 0, size, size);
				n++;
			}
			lprintf("Performance test: %dx%d at %d Hz\n", size, size, n);
		}
	}

	return noErr;
}

static void queueSizer(uint16_t queue, uint16_t *count, size_t *osize, size_t *isize) {
	// Tiny queues
	*count = 4;
	*osize = 64;
	*isize = 2048;
}

static uint32_t checksum(uint32_t pixel) {
	return (pixel << 8) ^ ((pixel + 1) << 16) ^ (pixel << 24) ^ ((uint32_t)'E' << 24);
}

static uint32_t checksumField(uint32_t pixel) {
	return pixel & 0xff000000;
}

static uint32_t setChecksumField(uint32_t pixel, uint32_t checksum) {
	return (checksum & 0xff000000) | (pixel & 0x00ffffff);
}

static void qdScreenUpdated(short top, short left, short bottom, short right) {
	qdUpdatesWorking = 1;

	top = MAX(MIN(top, H), 0);
	bottom = MAX(MIN(bottom, H), 0);
	left = MAX(MIN(left, W), 0);
	right = MAX(MIN(right, W), 0);

	if (top >= bottom || left >= right) return;

	updateScreen(top, left, bottom, right);
}

static void updateScreen(short top, short left, short bottom, short right) {
	int x, y;

	for (y=top; y<bottom; y++) {
		uint32_t *src = (void *)((char *)backbuf + y * W * 4 + left * 4);
		uint32_t *dest = (void *)((char *)frontbuf + y * W * 4 + left * 4);
		for (x=left; x<right; x++) {
			uint32_t s = *src++;
			*dest++ =
				((uint32_t)gamma[s & 0xff] << 24) |
				((uint32_t)gamma[(s >> 8) & 0xff] << 16) |
				((uint32_t)gamma[(s >> 16) & 0xff] << 8);
		}
	}

	// Use VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D to update the host resource from guest memory.
	{
		struct virtio_gpu_transfer_to_host_2d *buf = (void *)VTBuffers[0][0];
		memset(buf, 0, sizeof(*buf));
		buf->hdr.le32_type = EndianSwap32Bit(VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D);
		buf->hdr.le32_flags = EndianSwap32Bit(VIRTIO_GPU_FLAG_FENCE);

		buf->r.le32_x = EndianSwap32Bit(left);
		buf->r.le32_y = EndianSwap32Bit(top);
		buf->r.le32_width = EndianSwap32Bit(right - left);
		buf->r.le32_height = EndianSwap32Bit(bottom - top);
		
		buf->le32_offset = EndianSwap32Bit(top*W*4 + left*4);

		buf->le32_resource_id = EndianSwap32Bit(SCREEN_RESOURCE);

		VTSend(0, 0);
	}

	// Use VIRTIO_GPU_CMD_RESOURCE_FLUSH to flush the updated resource to the display.
	{
		struct virtio_gpu_resource_flush *buf = (void *)VTBuffers[0][2];
		memset(buf, 0, sizeof(*buf));
		buf->hdr.le32_type = EndianSwap32Bit(VIRTIO_GPU_CMD_RESOURCE_FLUSH);
		buf->hdr.le32_flags = EndianSwap32Bit(VIRTIO_GPU_FLAG_FENCE);

		buf->r.le32_x = EndianSwap32Bit(left);
		buf->r.le32_y = EndianSwap32Bit(top);
		buf->r.le32_width = EndianSwap32Bit(right - left);
		buf->r.le32_height = EndianSwap32Bit(bottom - top);

		buf->le32_resource_id = EndianSwap32Bit(SCREEN_RESOURCE);

		VTSend(0, 2);
	}

	while (!VTDone(0)) {}
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

	if (!qdUpdatesWorking) {
		updateScreen(0, 0, H, W);
	}

	setVBL();
	return noErr;
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
	rec->csBaseAddr = backbuf;
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
	rec->csBaseAddr = backbuf;
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
