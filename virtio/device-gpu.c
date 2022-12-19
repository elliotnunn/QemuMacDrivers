#include <stdbool.h>
#include <stdio.h>
#include <Devices.h>
#include <Displays.h>
#include <DriverServices.h>
#include <Events.h>
#include <fp.h>
#include <Gestalt.h>
#include <LowMem.h>
#include <NameRegistry.h>
#include <string.h>
#include <Video.h>
#include <VideoServices.h>

#include "allocator.h"
#include "atomic.h"
#include "byteswap.h"
#include "debugpollpatch.h"
#include "dirtyrectpatch.h"
#include "gammatables.h"
#include "lateboothook.h"
#include "lprintf.h"
#include "transport.h"
#include "structs-gpu.h"
#include "virtqueue.h"
#include "wait.h"

#include "device.h"

// The classics
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

enum {
	TRACECALLS = 0,
	MAXBUF = 64*1024*1024, // enough for 4096x4096
	MINBUF = 2*1024*1024, // enough for 800x600
	FAST_REFRESH = -16626, // before QD callbacks work, microsec, 60.15 Hz
	SLOW_REFRESH = 1000, // after QD callbacks work, millisec, 1 Hz
};

enum {
	k1bit = kDepthMode1,
	k2bit = kDepthMode2,
	k4bit = kDepthMode3,
	k8bit = kDepthMode4,
	k16bit = kDepthMode5,
	k32bit = kDepthMode6,
	kDepthModeMax = kDepthMode6
};

OSStatus DoDriverIO(AddressSpaceID spaceID, IOCommandID cmdID,
	IOCommandContents pb, IOCommandCode code, IOCommandKind kind);
static OSStatus initialize(DriverInitInfo *info);
static void transact(void *req, size_t req_size, void *reply, size_t reply_size);
static void getSuggestedSizes(struct virtio_gpu_display_one pmodes[16]);
static void getBestSize(short *width, short *height);
static uint32_t idForRes(short width, short height, bool force);
static uint32_t resCount(void);
static uint32_t setScanout(int idx, short w, short h, uint32_t *page_list);
static void notificationProc(NMRecPtr nmReqPtr);
static void notificationAtomic(void *nmReqPtr);
static void updateScreen(short top, short left, short bottom, short right);
static void sendPixels(void *topleft_voidptr, void *botright_voidptr);
static void perfTest(void);
static void perfTestNotification(NMRecPtr nmReqPtr);
static OSStatus VBL(void *p1, void *p2);
static OSStatus finalize(DriverFinalInfo *info);
static OSStatus control(short csCode, void *param);
static OSStatus status(short csCode, void *param);
static long rowbytesFor(int relativeDepth, long width);
static void reCLUT(int index);
static OSStatus GetBaseAddr(VDPageInfo *rec);
static OSStatus MySetEntries(VDSetEntryRecord *rec);
static OSStatus DirectSetEntries(VDSetEntryRecord *rec);
static OSStatus GetEntries(VDSetEntryRecord *rec);
static OSStatus GetClutBehavior(VDClutBehavior *rec);
static OSStatus SetClutBehavior(VDClutBehavior *rec);
static OSStatus SetGamma(VDGammaRecord *rec);
static OSStatus GetGamma(VDGammaRecord *rec);
static OSStatus GetGammaInfoList(VDGetGammaListRec *rec);
static OSStatus RetrieveGammaTable(VDRetrieveGammaRec *rec);
static void setGammaTable(GammaTbl *tbl);
static OSStatus GrayPage(VDPageInfo *rec);
static void gray(void);
static OSStatus SetGray(VDGrayRecord *rec);
static OSStatus GetGray(VDGrayRecord *rec);
static OSStatus GetPages(VDPageInfo *rec);
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

// Allocate one 4096-byte page for all our screen-update buffers.
// (16 is the maximum number of 192-byte chunks fitting in a page)
static void *lpage;
static uint32_t ppage;
static int maxinflight = 16;
static uint16_t freebufs;

// Allocate two large framebuffers
static void *backbuf, *frontbuf;
static size_t bufsize;
static uint32_t fbpages[MAXBUF/4096];
static uint32_t screen_resource = 100;

// init routine polls this after sending a buffer
static volatile void *last_tag;

// Current dimensions, depth and color settings
struct rez {short w; short h;};
struct rez rezzes[] = {
	{512, 342},
	{640, 480},
	{800, 600},
	{1024, 768},
	{0, 0},
};
static short W, H, rowbytes;
static int depth;
static ColorSpec publicCLUT[256];
static uint32_t privateCLUT[256];

uint8_t gamma_red[256];
uint8_t gamma_grn[256];
uint8_t gamma_blu[256];
char gamma_public[1024];

// Fake vertical blanking interrupts
static InterruptServiceIDType vblservice;
static AbsoluteTime vbltime;
static TimerID vbltimer;
static bool vblon = true;
static bool qdworks;

static bool pending_notification; // deduplicate NMInstall
static bool change_in_progress; // SetMode/SwitchMode lock out frame interrupts

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

	(void)spaceID; // Apple never implemented multiple address space support

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
			if ((*pb.pb).cntrlParam.csCode < sizeof(statusNames)/sizeof(*statusNames)) {
				lprintf("Status(%s)\n", statusNames[(*pb.pb).cntrlParam.csCode]);
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
	long ram = 0;

	// No need to signal FAILED if cannot communicate with device
	if (!VInit(&info->deviceEntry)) return paramErr;

	if (!VFeaturesOK()) goto fail;

	// Can have (descriptor count)/4 updateScreens in flight at once
	maxinflight = QInit(0, 4*maxinflight /*n(descriptors)*/) / 4;
	if (maxinflight < 1) goto fail;

	freebufs = (1 << maxinflight) - 1;

	// All our descriptors point into this wired-down page
	lpage = AllocPages(1, &ppage);
	if (lpage == NULL) goto fail;

	// Use no more than a quarter of RAM (but allow for a few KB short of a MB)
	bufsize = MAXBUF;
	Gestalt('ram ', &ram);
	while (ram != 0 && bufsize > (ram+0x10000)/8) bufsize /= 2;

	// Allocate the largest two framebuffers possible
	for (;;) {
		backbuf = PoolAllocateResident(bufsize, true);
		frontbuf = AllocPages(bufsize/4096, fbpages);

		if (backbuf != NULL && frontbuf != NULL) break;

		if (backbuf != NULL) PoolDeallocate(backbuf);
		if (frontbuf != NULL) FreePages(frontbuf);

		bufsize /= 2;
		if (bufsize < MINBUF) goto fail;
	}

	// Cannot go any further without touching virtqueues, which requires DRIVER_OK
	VDriverOK();

	// Connect front buffer to scanout
	getBestSize(&W, &H);
	screen_resource = setScanout(0 /*scanout id*/, W, H, fbpages);
	if (!screen_resource) goto fail;

	// Connect back buffer to front buffer
	depth = k32bit;
	rowbytes = rowbytesFor(depth, W);
	setGammaTable((GammaTbl *)&builtinGamma[0].table);

	// Initially VBL interrupts must be fast
	VSLNewInterruptService(&info->deviceEntry, kVBLInterruptServiceType, &vblservice);
	vbltime = AddDurationToAbsolute(FAST_REFRESH, UpTime());
	SetInterruptTimer(&vbltime, VBL, NULL, &vbltimer);

	// Catch MacsBug redraws (when interrupts are disabled)
	InstallDebugPollPatch();

	InstallLateBootHook();

	return noErr;

fail:
	if (lpage) FreePages(lpage);
	if (backbuf) PoolDeallocate(backbuf);
	if (frontbuf) FreePages(frontbuf);
	VFail();
	return paramErr;
}

// Synchronous transaction instead of the usual async queue, must not interrupt
static void transact(void *req, size_t req_size, void *reply, size_t reply_size) {
	uint32_t physical_bufs[2], sizes[2];

	physical_bufs[0] = ppage;
	physical_bufs[1] = ppage + 2048;
	sizes[0] = req_size;
	sizes[1] = reply_size;

	while (freebufs != ((1 << maxinflight) - 1)) QPoll(0);

	memcpy(lpage, req, req_size);
	last_tag = (void *)'wait';
	QSend(0, 1, 1, physical_bufs, sizes, (void *)'done');
	QNotify(0);
	while (last_tag != (void *)'done') QPoll(0);
	memcpy(reply, (char *)lpage + 2048, reply_size);
}

static void getSuggestedSizes(struct virtio_gpu_display_one pmodes[16]) {
	struct virtio_gpu_ctrl_hdr request = {
		LE32(VIRTIO_GPU_CMD_GET_DISPLAY_INFO),
		LE32(VIRTIO_GPU_FLAG_FENCE)};
	struct virtio_gpu_resp_display_info reply = {0};

	transact(&request, sizeof(request), &reply, sizeof(reply));
	memcpy(pmodes, reply.pmodes, sizeof(reply.pmodes));
}

static void getBestSize(short *width, short *height) {
	long w, h;
	int i;
	struct virtio_gpu_display_one pmodes[16];

	getSuggestedSizes(pmodes);

	for (i=0; i<16; i++) {
		if (GETLE32(&pmodes[i].le32_enabled)) break;
	}

	if (i == 16) {
		*width = 800;
		*height = 600;
		return;
	}

	w = GETLE32(&pmodes[i].r.le32_width);
	h = GETLE32(&pmodes[i].r.le32_height);

	// Not enough RAM allocated? Try for smaller
	if (h * rowbytesFor(k32bit, w) > bufsize) {
		// Cancel aspect ratio down to its simplest form
		long wr=w, hr=h;
		for (i=2; i<wr && i<hr; i++) {
			while (wr%i == 0 && hr%i == 0) {
				wr /= i;
				hr /= i;
			}
		}

		do {
			if (wr<=256 && hr<=256) {
				// Match aspect ratio precisely
				w -= wr;
				h -= hr;
			} else if (w > h) {
				// Approximate smaller widescreen
				h -= 1;
				w = (h*wr + hr/2) / hr; // round to nearest integer
			} else {
				// Approximate smaller tallscreen
				w -= 1;
				h = (w*hr + wr/2) / wr;
			}
		} while (h * rowbytesFor(k32bit, w) > bufsize);
	}

	*width = w;
	*height = h;
}

static uint32_t idForRes(short width, short height, bool force) {
	int i;
	for (i=0; i<sizeof(rezzes)/sizeof(*rezzes)-1; i++) {
		if (rezzes[i].w == width && rezzes[i].h == height) break;
	}
	if (force) {
		rezzes[i].w = width;
		rezzes[i].h = height;
	}
	return i + 1;
}

static uint32_t resCount(void) {
	uint32_t n;
	n = sizeof(rezzes)/sizeof(*rezzes);
	if (rezzes[n-1].w == 0) n--;
	return n;
}

// Must be called atomically
// Returns the resource ID, or zero if you prefer
static uint32_t setScanout(int idx, short w, short h, uint32_t *page_list) {
	struct virtio_gpu_resource_detach_backing detach_backing = {0};
	struct virtio_gpu_resource_create_2d create_2d = {0};
	struct virtio_gpu_resource_attach_backing attach_backing = {0};
	struct virtio_gpu_set_scanout set_scanout = {0};
	struct virtio_gpu_resource_unref resource_unref = {0};
	struct virtio_gpu_ctrl_hdr reply;

	static uint32_t res_ids[16];
	uint32_t old_resource = res_ids[idx];
	uint32_t new_resource = res_ids[idx] ? (res_ids[idx] ^ 1) : (100 + 2*idx);

	// Divide page_list into contiguous segments, hopefully not too many
	size_t pgcnt = ((size_t)w * h * 4 + 0xfff) / 0x1000;
	int extents[126] = {0};
	int extcnt = 0;
	int i, j;

	for (i=0; i<pgcnt; i++) {
		if (i == 0 || page_list[i] != page_list[i-1]+0x1000) {
			// page_list too fragmented so fail gracefully
			if (extcnt == sizeof(extents)/sizeof(*extents)) return 0;

			extcnt++;
		}

		extents[extcnt-1]++;
	}

	// Create a host resource using VIRTIO_GPU_CMD_RESOURCE_CREATE_2D.
	SETLE32(&create_2d.hdr.le32_type, VIRTIO_GPU_CMD_RESOURCE_CREATE_2D);
	SETLE32(&create_2d.hdr.le32_flags, VIRTIO_GPU_FLAG_FENCE);
	SETLE32(&create_2d.le32_resource_id, new_resource);
	SETLE32(&create_2d.le32_format, VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM);
	SETLE32(&create_2d.le32_width, w);
	SETLE32(&create_2d.le32_height, h);

	transact(&create_2d, sizeof(create_2d), &reply, sizeof(reply));

	// Gracefully handle host running out of room for the resource
	if (GETLE32(&reply.le32_type) != VIRTIO_GPU_RESP_OK_NODATA) return 0;

	// Detach backing from the old resource, but don't delete it yet.
	if (old_resource != 0) {
		SETLE32(&detach_backing.hdr.le32_type, VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING);
		SETLE32(&detach_backing.hdr.le32_flags, VIRTIO_GPU_FLAG_FENCE);
		SETLE32(&detach_backing.le32_resource_id, old_resource);

		transact(&detach_backing, sizeof(detach_backing), &reply, sizeof(reply));
	}

	// Attach guest allocated backing memory to the resource just created.
	SETLE32(&attach_backing.hdr.le32_type, VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING);
	SETLE32(&attach_backing.hdr.le32_flags, VIRTIO_GPU_FLAG_FENCE);
	SETLE32(&attach_backing.le32_resource_id, new_resource);
	SETLE32(&attach_backing.le32_nr_entries, extcnt);

	for (i=j=0; i<extcnt; j+=extents[i++]) {
		SETLE32(&attach_backing.entries[i].le32_addr, page_list[j]);
		SETLE32(&attach_backing.entries[i].le32_length, extents[i] * 0x1000);
	}

	transact(&attach_backing, sizeof(attach_backing), &reply, sizeof(reply));

	// Use VIRTIO_GPU_CMD_SET_SCANOUT to link the framebuffer to a display scanout.
	SETLE32(&set_scanout.hdr.le32_type, VIRTIO_GPU_CMD_SET_SCANOUT);
	SETLE32(&set_scanout.hdr.le32_flags, VIRTIO_GPU_FLAG_FENCE);
	SETLE32(&set_scanout.r.le32_x, 0);
	SETLE32(&set_scanout.r.le32_y, 0);
	SETLE32(&set_scanout.r.le32_width, w);
	SETLE32(&set_scanout.r.le32_height, h);
	SETLE32(&set_scanout.le32_scanout_id, 0); // index, 0-15
	SETLE32(&set_scanout.le32_resource_id, new_resource);

	transact(&set_scanout, sizeof(set_scanout), &reply, sizeof(reply));

	if (old_resource != 0) {
		SETLE32(&resource_unref.hdr.le32_type, VIRTIO_GPU_CMD_RESOURCE_UNREF);
		SETLE32(&resource_unref.hdr.le32_flags, VIRTIO_GPU_FLAG_FENCE);
		SETLE32(&resource_unref.le32_resource_id, old_resource);

		transact(&resource_unref, sizeof(resource_unref), &reply, sizeof(reply));
	}

	res_ids[idx] = new_resource;
	return new_resource;
}

void DConfigChange(void) {
	// Post a notification to get some system task time
	if (!pending_notification) {
		static RoutineDescriptor descriptor = BUILD_ROUTINE_DESCRIPTOR(
			kPascalStackBased | STACK_ROUTINE_PARAMETER(1, kFourByteCode),
			notificationProc);

		static NMRec rec = {
			NULL, // qLink
			8, // qType
			0, 0, 0, // reserved fields
			0, NULL, NULL, NULL, // nmMark, nmIcon, nmSound, nmStr
			&descriptor
		};

		NMInstall(&rec);
		pending_notification = true;
	}
}

// Got some system task time to access the Toolbox safely
// TN1033 suggests using the Notification Manager
static void notificationProc(NMRecPtr nmReqPtr) {
	Atomic1(notificationAtomic, nmReqPtr);
}

// Now, in addition to the Toolbox being safe, interrupts are (mostly) disabled
static void notificationAtomic(void *nmReqPtr) {
	struct virtio_gpu_config *config = VConfig;
	short width, height;
	unsigned long newdepth = depth;

	NMRemove(nmReqPtr);
	pending_notification = false;

	if ((config->le32_events_read & LE32(VIRTIO_GPU_EVENT_DISPLAY)) == 0) return;

	config->le32_events_clear = LE32(VIRTIO_GPU_EVENT_DISPLAY);
	SynchronizeIO();

	getBestSize(&width, &height);
	if (W == width && H == height) return;

	// Kick the Display Manager
	DMSetDisplayMode(DMGetFirstScreenDevice(true),
		idForRes(width, height, true), &newdepth, NULL, NULL);
}

void DNotified(uint16_t q, uint16_t buf, size_t len, void *tag) {
	last_tag = tag;
	QFree(q, buf);
	if ((unsigned long)tag < 256) {
		freebufs |= 1 << (char)tag;
		sendPixels((void *)0x7fff7fff, (void *)0x00000000);
	}
}

void DebugPollCallback(void) {
	// If we enter the debugger with all descriptors in flight (rare),
	// we will stall waiting for an interrupt to free up a descriptor.
	while (freebufs == 0) QPoll(0);

	updateScreen(0, 0, H, W);
}

void LateBootHook(void) {
	InstallDirtyRectPatch();
	updateScreen(0, 0, H, W);
	DConfigChange();
}

void DirtyRectCallback(short top, short left, short bottom, short right) {
	qdworks = true;

	top = MAX(MIN(top, H), 0);
	bottom = MAX(MIN(bottom, H), 0);
	left = MAX(MIN(left, W), 0);
	right = MAX(MIN(right, W), 0);

	if (top >= bottom || left >= right) return;

	updateScreen(top, left, bottom, right);
}

static void updateScreen(short top, short left, short bottom, short right) {
	int x, y;

	if (change_in_progress) return;

	logTime('Blit', 0);

	// These blitters are not satisfactory
	if (depth == k1bit) {
		uint32_t c0 = privateCLUT[0], c1 = privateCLUT[1];
		int leftBytes = (left / 8) & ~3;
		int rightBytes = ((right + 31) / 8) & ~3;
		for (y=top; y<bottom; y++) {
			uint32_t *src = (void *)((char *)backbuf + y * rowbytes + leftBytes);
			uint32_t *dest = (void *)((char *)frontbuf + y * W * 4 + (left & ~31) * 4);
			for (x=leftBytes; x<rightBytes; x+=4) {
				uint32_t s = *src++;
				*dest++ = (s & 0x80000000) ? c1 : c0;
				*dest++ = (s & 0x40000000) ? c1 : c0;
				*dest++ = (s & 0x20000000) ? c1 : c0;
				*dest++ = (s & 0x10000000) ? c1 : c0;
				*dest++ = (s & 0x08000000) ? c1 : c0;
				*dest++ = (s & 0x04000000) ? c1 : c0;
				*dest++ = (s & 0x02000000) ? c1 : c0;
				*dest++ = (s & 0x01000000) ? c1 : c0;
				*dest++ = (s & 0x00800000) ? c1 : c0;
				*dest++ = (s & 0x00400000) ? c1 : c0;
				*dest++ = (s & 0x00200000) ? c1 : c0;
				*dest++ = (s & 0x00100000) ? c1 : c0;
				*dest++ = (s & 0x00080000) ? c1 : c0;
				*dest++ = (s & 0x00040000) ? c1 : c0;
				*dest++ = (s & 0x00020000) ? c1 : c0;
				*dest++ = (s & 0x00010000) ? c1 : c0;
				*dest++ = (s & 0x00008000) ? c1 : c0;
				*dest++ = (s & 0x00004000) ? c1 : c0;
				*dest++ = (s & 0x00002000) ? c1 : c0;
				*dest++ = (s & 0x00001000) ? c1 : c0;
				*dest++ = (s & 0x00000800) ? c1 : c0;
				*dest++ = (s & 0x00000400) ? c1 : c0;
				*dest++ = (s & 0x00000200) ? c1 : c0;
				*dest++ = (s & 0x00000100) ? c1 : c0;
				*dest++ = (s & 0x00000080) ? c1 : c0;
				*dest++ = (s & 0x00000040) ? c1 : c0;
				*dest++ = (s & 0x00000020) ? c1 : c0;
				*dest++ = (s & 0x00000010) ? c1 : c0;
				*dest++ = (s & 0x00000008) ? c1 : c0;
				*dest++ = (s & 0x00000004) ? c1 : c0;
				*dest++ = (s & 0x00000002) ? c1 : c0;
				*dest++ = (s & 0x00000001) ? c1 : c0;
			}
		}
	} else if (depth == k2bit) {
		int leftBytes = (left / 4) & ~3;
		int rightBytes = ((right + 15) / 4) & ~3;
		for (y=top; y<bottom; y++) {
			uint32_t *src = (void *)((char *)backbuf + y * rowbytes + leftBytes);
			uint32_t *dest = (void *)((char *)frontbuf + y * W * 4 + (left & ~15) * 4);
			for (x=leftBytes; x<rightBytes; x+=4) {
				uint32_t s = *src++;
				*dest++ = privateCLUT[(s >> 30) & 3];
				*dest++ = privateCLUT[(s >> 28) & 3];
				*dest++ = privateCLUT[(s >> 26) & 3];
				*dest++ = privateCLUT[(s >> 24) & 3];
				*dest++ = privateCLUT[(s >> 22) & 3];
				*dest++ = privateCLUT[(s >> 20) & 3];
				*dest++ = privateCLUT[(s >> 18) & 3];
				*dest++ = privateCLUT[(s >> 16) & 3];
				*dest++ = privateCLUT[(s >> 14) & 3];
				*dest++ = privateCLUT[(s >> 12) & 3];
				*dest++ = privateCLUT[(s >> 10) & 3];
				*dest++ = privateCLUT[(s >> 8) & 3];
				*dest++ = privateCLUT[(s >> 6) & 3];
				*dest++ = privateCLUT[(s >> 4) & 3];
				*dest++ = privateCLUT[(s >> 2) & 3];
				*dest++ = privateCLUT[(s >> 0) & 3];
			}
		}
	} else if (depth == k4bit) {
		int leftBytes = (left / 2) & ~3;
		int rightBytes = ((right + 7) / 2) & ~3;
		for (y=top; y<bottom; y++) {
			uint32_t *src = (void *)((char *)backbuf + y * rowbytes + leftBytes);
			uint32_t *dest = (void *)((char *)frontbuf + y * W * 4 + (left & ~7) * 4);
			for (x=leftBytes; x<rightBytes; x+=4) {
				uint32_t s = *src++;
				*dest++ = privateCLUT[(s >> 28) & 15];
				*dest++ = privateCLUT[(s >> 24) & 15];
				*dest++ = privateCLUT[(s >> 20) & 15];
				*dest++ = privateCLUT[(s >> 16) & 15];
				*dest++ = privateCLUT[(s >> 12) & 15];
				*dest++ = privateCLUT[(s >> 8) & 15];
				*dest++ = privateCLUT[(s >> 4) & 15];
				*dest++ = privateCLUT[(s >> 0) & 15];
			}
		}
	} else if (depth == k8bit) {
		for (y=top; y<bottom; y++) {
			uint8_t *src = (void *)((char *)backbuf + y * rowbytes + left);
			uint32_t *dest = (void *)((char *)frontbuf + y * W * 4 + left * 4);
			for (x=left; x<right; x++) {
				*dest++ = privateCLUT[*src++];
			}
		}
	} else if (depth == k16bit) {
		for (y=top; y<bottom; y++) {
			uint16_t *src = (void *)((char *)backbuf + y * rowbytes + left * 2);
			uint32_t *dest = (void *)((char *)frontbuf + y * W * 4 + left * 4);
			for (x=left; x<right; x++) {
				uint16_t s = *src++;
				*dest++ =
					((uint32_t)gamma_blu[((s & 0x1f) << 3) | ((s & 0x1f) << 3 >> 5)] << 24) |
					((uint32_t)gamma_grn[((s & 0x3e0) >> 5 << 3) | ((s & 0x3e0) >> 5 << 3 >> 5)] << 16) |
					((uint32_t)gamma_red[((s & 0x7c00) >> 10 << 3) | ((s & 0x7c00) >> 10 << 3 >> 5)] << 8);
			}
		}
	} else if (depth == k32bit) {
		for (y=top; y<bottom; y++) {
			uint32_t *src = (void *)((char *)backbuf + y * rowbytes + left * 4);
			uint32_t *dest = (void *)((char *)frontbuf + y * W * 4 + left * 4);
			for (x=left; x<right; x++) {
				uint32_t s = *src++;
				*dest++ =
					((uint32_t)gamma_blu[s & 0xff] << 24) |
					((uint32_t)gamma_grn[(s >> 8) & 0xff] << 16) |
					((uint32_t)gamma_red[(s >> 16) & 0xff] << 8);
			}
		}
	}

	Atomic2(sendPixels,
		(void *)(((unsigned long)top << 16) | left),
		(void *)(((unsigned long)bottom << 16) | right));
}

// Non-reentrant, must be called atomically
// MacsBug time might be an exception
// Kick at interrupt time: sendPixels((void *)0x7fff7fff, (void *)0x00000000);
static void sendPixels(void *topleft_voidptr, void *botright_voidptr) {
	static bool reentered;
	static bool interest;

	static int n;

	// Stored pending rect
	static short ptop = 0x7fff;
	static short pleft = 0x7fff;
	static short pbottom = 0;
	static short pright = 0;

	short top = (unsigned long)topleft_voidptr >> 16;
	short left = (short)topleft_voidptr;
	short bottom = (unsigned long)botright_voidptr >> 16;
	short right = (short)botright_voidptr;

	int i;

	struct virtio_gpu_transfer_to_host_2d *obuf1; // 56 bytes
	struct virtio_gpu_ctrl_hdr *ibuf1;            // 24 bytes
	uint32_t physicals1[2];
	uint32_t sizes1[2] = {56, 24};

	struct virtio_gpu_resource_flush *obuf2;      // 48 bytes
	struct virtio_gpu_ctrl_hdr *ibuf2;            // 24 bytes
	uint32_t physicals2[2];
	uint32_t sizes2[2] = {48, 24};

	// We have been reentered via QPoll and DNotified -- nothing to do
	if (reentered) return;

	// Union the pending rect and the passed-in rect
	top = MIN(top, ptop);
	left = MIN(left, pleft);
	bottom = MAX(bottom, pbottom);
	right = MAX(right, pright);
	if (top >= bottom || left >= right) return;

	// Enable queue notifications so that none are missed after QPoll
	if (!interest) {
		interest = true;
		QInterest(0, 1);
	}

	// Might reenter this routine, in which case must return early (above)
	reentered = true;
	QPoll(0);
	reentered = false;

	// Now we are guaranteed that a free buffer won't be missed (unless we turn off rupts)

	// No free buffer yet... return and wait for a queue notification
	if (!freebufs) {
		ptop = top;
		pleft = left;
		pbottom = bottom;
		pright = right;
		return;
	}

	// We have a free buffer, so don't need to wait for a notification to provide one
	interest = false;
	QInterest(0, -1);

	// Pick a buffer
	for (i=0; i<maxinflight; i++) {
		if (freebufs & (1 << i)) {
			freebufs &= ~(1 << i);
			break;
		}
	}

	// The 4096-byte page is divided into 192-byte blocks for locality
	obuf1 = (void *)((char *)lpage + 192*i);
	ibuf1 = (void *)((char *)obuf1 + 128);
	obuf2 = (void *)((char *)obuf1 + 64);
	ibuf2 = (void *)((char *)obuf1 + 160);

	physicals1[0] = ppage + 192*i;
	physicals1[1] = physicals1[0] + 128;
	physicals2[0] = physicals1[0] + 64;
	physicals2[1] = physicals1[0] + 160;

	// Update the host resource from guest memory.
	SETLE32(&obuf1->hdr.le32_type, VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D);
	SETLE32(&obuf1->hdr.le32_flags, 0);
	SETLE32(&obuf1->r.le32_x, left);
	SETLE32(&obuf1->r.le32_y, top);
	SETLE32(&obuf1->r.le32_width, right - left);
	SETLE32(&obuf1->r.le32_height, bottom - top);
	SETLE32(&obuf1->le32_offset, top*W*4 + left*4);
	SETLE32(&obuf1->le32_resource_id, screen_resource);

	QSend(0, 1, 1, physicals1, sizes1, (void *)'tfer');

	// Flush the updated resource to the display.
	SETLE32(&obuf2->hdr.le32_type, VIRTIO_GPU_CMD_RESOURCE_FLUSH);
	SETLE32(&obuf2->hdr.le32_flags, 0);
	SETLE32(&obuf2->r.le32_x, left);
	SETLE32(&obuf2->r.le32_y, top);
	SETLE32(&obuf2->r.le32_width, right - left);
	SETLE32(&obuf2->r.le32_height, bottom - top);
	SETLE32(&obuf2->le32_resource_id, screen_resource);

	QSend(0, 1, 1, physicals2, sizes2, (void *)i);
	QNotify(0);

	ptop = 0x7fff;
	pleft = 0x7fff;
	pbottom = 0;
	pright = 0;
}

static void perfTest(void) {
	static RoutineDescriptor descriptor = BUILD_ROUTINE_DESCRIPTOR(
		kPascalStackBased | STACK_ROUTINE_PARAMETER(1, kFourByteCode),
		perfTestNotification);

	static char str[256];

	static NMRec rec = {
		NULL, // qLink
		8, // qType
		0, 0, 0, // reserved fields
		0, NULL, NULL, // nmMark, nmIcon, nmSound
		(StringPtr)str, // nmStr
		&descriptor,
		0 // nmRefCon, 1 = onscreen
	};

	long t=LMGetTicks();
	long ctr1=0, ctr2=0;
	KeyMap keys;

	// control-shift
	GetKeys(keys);
	if ((keys[1]&9) != 9) return;

	if (rec.nmRefCon != 0) NMRemove(&rec);
	rec.nmRefCon = 1;

	// Warm up
	t += 2;
	while (t > LMGetTicks()) {
		while (freebufs == 0) QPoll(0);
		updateScreen(0, 0, H, W);
	}

	// Measure with our blitter involved
	t += 30;
	while (t > LMGetTicks()) {
		while (freebufs == 0) QPoll(0);
		updateScreen(0, 0, H, W);
		ctr1++;
	}

	// Warm up
	t += 2;
	while (t > LMGetTicks()) {
		while (freebufs == 0) QPoll(0);
		Atomic2(sendPixels, (void *)0x00000000, (void *)0x7fff7fff);
	}

	// Measure without our blitter
	t += 30;
	while (t > LMGetTicks()) {
		while (freebufs == 0) QPoll(0);
		Atomic2(sendPixels, (void *)0x00000000, (void *)0x7fff7fff);
		ctr2++;
	}

	// The return value of sprintf becomes the Pascal length byte
	str[0] =
		sprintf(str+1,
		"virtio-gpu\n"
		"  Mode: %dx%dx%d\n"
		"  Frame rate with guest blitter: %ld Hz\n"
		"  Frame rate without guest blitter: %ld Hz",
		W, H, 1<<(depth-kDepthMode1), ctr1*2, ctr2*2);

	NMInstall(&rec);
}

static void perfTestNotification(NMRecPtr nmReqPtr) {
	NMRemove(nmReqPtr);
	nmReqPtr->nmRefCon = 0;
}

static OSStatus VBL(void *p1, void *p2) {
	if (vblon) {
		//if (*(signed char *)0x910 >= 0) Debugger();
		VSLDoInterruptService(vblservice);
	}

	updateScreen(0, 0, H, W);

	vbltime = AddDurationToAbsolute(qdworks ? SLOW_REFRESH : FAST_REFRESH, vbltime);
	SetInterruptTimer(&vbltime, VBL, NULL, &vbltimer);

	return noErr;
}

static OSStatus finalize(DriverFinalInfo *info) {
	return noErr;
}

static OSStatus control(short csCode, void *param) {
	switch (csCode) {
		case cscDirectSetEntries: return DirectSetEntries(param);
		//case cscDrawHardwareCursor: return DrawHardwareCursor(param);
		case cscGrayPage: return GrayPage(param);
		case cscSavePreferredConfiguration: return SavePreferredConfiguration(param);
		case cscSetClutBehavior: return SetClutBehavior(param);
		case cscSetEntries: return MySetEntries(param);
		case cscSetGamma: return SetGamma(param);
		case cscSetGray: return SetGray(param);
		//case cscSetHardwareCursor: return SetHardwareCursor(param);
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
		//case cscGetHardwareCursorDrawState: return GetHardwareCursorDrawState(param);
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
		//case cscSupportsHardwareCursor: return SupportsHardwareCursor(param);
	}
	return statusErr;
}

static long rowbytesFor(int relativeDepth, long width) {
	long ret;

	if (relativeDepth == k1bit) {
		ret = (width + 7) / 8;
	} else if (relativeDepth == k2bit) {
		ret = (width + 3) / 4;
	} else if (relativeDepth == k4bit) {
		ret = (width + 1) / 2;
	} else if (relativeDepth == k8bit) {
		ret = width;
	} else if (relativeDepth == k16bit) {
		ret = width * 2;
	} else if (relativeDepth == k32bit) {
		ret = width * 4;
	}

	// 32-bit align
	ret += 3;
	ret -= ret % 4;

	return ret;
}

// Update privateCLUT from publicCLUT
static void reCLUT(int index) {
	privateCLUT[index] =
		((uint32_t)gamma_red[publicCLUT[index].rgb.red >> 8] << 8) |
		((uint32_t)gamma_grn[publicCLUT[index].rgb.green >> 8] << 16) |
		((uint32_t)gamma_blu[publicCLUT[index].rgb.blue >> 8] << 24);
}

// Returns the base address of a specified page in the current mode.
// --- csMode      Unused
// --- csData      Unused
// --> csPage      Desired page
// <-- csBaseAddr  Base address of VRAM for the desired page
static OSStatus GetBaseAddr(VDPageInfo *rec) {
	if (rec->csPage != 0) return statusErr;
	rec->csBaseAddr = backbuf;
	return noErr;
}

// If the video card is an indexed device, the SetEntries control routine
// should change the contents of the card’s CLUT.
// --> csTable     Pointer to ColorSpec array
// --> csStart     First entry in table
// --> csCount     Number of entries to set
static OSStatus MySetEntries(VDSetEntryRecord *rec) {
	if (depth > k8bit) return controlErr;
	return DirectSetEntries(rec);
}

// Normally, color table animation is not used on a direct device, but
// there are some special circumstances under which an application may want
// to change the color table hardware. The DirectSetEntries routine
// provides the direct device with indexed mode functionality identical to
// the regular SetEntries control routine.
static OSStatus DirectSetEntries(VDSetEntryRecord *rec) {
	int src, dst;

	for (src=0; src<=rec->csCount; src++) {
		if (rec->csStart == -1) {
			dst = rec->csTable[src].value;
		} else {
			dst = rec->csStart + src;
		}

		publicCLUT[dst].rgb = rec->csTable[src].rgb;
		reCLUT(dst);
	}

	updateScreen(0, 0, H, W);

	return noErr;
}

// Returns the specified number of consecutive CLUT entries, starting with
// the specified first entry.
// <-> csTable     Pointer to ColorSpec array
// --> csStart     First entry in table
// --> csCount     Number of entries to set
static OSStatus GetEntries(VDSetEntryRecord *rec) {
	int src, dst;

	for (dst=0; dst<=rec->csCount; dst++) {
		if (rec->csStart == -1) {
			src = rec->csTable[dst].value;
		} else {
			src = rec->csStart + dst;
		}

		rec->csTable[dst] = publicCLUT[src];
	}

	return noErr;
}

// Not well documented, but needed by MacsBug
static OSStatus GetClutBehavior(VDClutBehavior *rec) {
	*rec = kSetClutAtVBL;
	return noErr;
}

static OSStatus SetClutBehavior(VDClutBehavior *rec) {
	return controlErr;
}

// Sets the gamma table in the driver that corrects RGB color values.
// If NIL is passed for the csGTable value, the driver should build a
// linear ramp in the gamma table to allow for an uncorrected display.
// --> csGTable    Pointer to gamma table
static OSStatus SetGamma(VDGammaRecord *rec) {
	struct myGammaTable {
		 short gVersion;           // always 0
		 short gType;              // 0 means "independent from CLUT"
		 short gFormulaSize;       // display-identifying bytes after gDataWidth = 0
		 short gChanCnt;           // 1 in this case, could be 3 in others
		 short gDataCnt;           // 256
		 short gDataWidth;         // 8 bits per element
		 unsigned char data[256];
	};

	static const struct myGammaTable uncorrectedTable = {0, 0, 0, 1, 256, 8, {
		0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
		0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,
		0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,
		0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,
		0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x4b,0x4c,0x4d,0x4e,0x4f,
		0x50,0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x5b,0x5c,0x5d,0x5e,0x5f,
		0x60,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x6b,0x6c,0x6d,0x6e,0x6f,
		0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x7b,0x7c,0x7d,0x7e,0x7f,
		0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x8b,0x8c,0x8d,0x8e,0x8f,
		0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0x9b,0x9c,0x9d,0x9e,0x9f,
		0xa0,0xa1,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xab,0xac,0xad,0xae,0xaf,
		0xb0,0xb1,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xbb,0xbc,0xbd,0xbe,0xbf,
		0xc0,0xc1,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xcb,0xcc,0xcd,0xce,0xcf,
		0xd0,0xd1,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xdb,0xdc,0xdd,0xde,0xdf,
		0xe0,0xe1,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xeb,0xec,0xed,0xee,0xef,
		0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa,0xfb,0xfc,0xfd,0xfe,0xff}};

	if (rec->csGTable != NULL)
		setGammaTable((GammaTbl *)rec->csGTable);
	else
		setGammaTable((GammaTbl *)&uncorrectedTable);

	// SetGamma guaranteed to be followed by SetEntries, which will updateScreen;
	// but this can't apply to direct modes
	if (depth >= k16bit) updateScreen(0, 0, H, W);

	return noErr;
}

// Returns a pointer to the current gamma table.
// <-- csGTable    Pointer to gamma table
static OSStatus GetGamma(VDGammaRecord *rec) {
	rec->csGTable = gamma_public;
	return noErr;
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
	enum {first = 128};
	long last = first + builtinGammaCount - 1;

	long id;

	if (rec->csPreviousGammaTableID == kGammaTableIDFindFirst) {
		id = first;
	} else if (rec->csPreviousGammaTableID == kGammaTableIDSpecific) {
		id = rec->csGammaTableID;

		if (id < first || id > last) {
			return paramErr;
		}
	} else if (rec->csPreviousGammaTableID >= first && rec->csPreviousGammaTableID < last) {
		id = rec->csPreviousGammaTableID + 1;
	} else if (rec->csPreviousGammaTableID == last) {
		rec->csGammaTableID = kGammaTableIDNoMoreTables;
		lprintf("GetGammaInfoList prevID=%d ... ID=%d size=%d name=%s\n",
			rec->csPreviousGammaTableID,
			rec->csGammaTableID,
			rec->csGammaTableSize,
			rec->csGammaTableName);
		return noErr;
	} else {
		return paramErr;
	}

	rec->csGammaTableID = id;
	rec->csGammaTableSize = sizeof(builtinGamma[0].table);
	memcpy(rec->csGammaTableName, builtinGamma[id-first].name, 32);

	lprintf("GetGammaInfoList prevID=%d ... ID=%d size=%d name=%s\n",
		rec->csPreviousGammaTableID,
		rec->csGammaTableID,
		rec->csGammaTableSize,
		rec->csGammaTableName);

	return noErr;
}

// Copies the designated gamma table into the designated location.
// --> csGammaTableID      ID of gamma table to retrieve
// <-> csGammaTablePtr     Location to copy table into
static OSStatus RetrieveGammaTable(VDRetrieveGammaRec *rec) {
	enum {first = 128};
	long last = first + builtinGammaCount - 1;

	long id = rec->csGammaTableID;

	if (id < first || id > last) {
		return paramErr;
	}

	lprintf("copying gamma table %d x %db\n", id, sizeof(builtinGamma[0].table));

	memcpy(rec->csGammaTablePtr, &builtinGamma[id-first].table, sizeof(builtinGamma[0].table));

	return noErr;
}

static void setGammaTable(GammaTbl *tbl) {
	void *data = (char *)tbl + 12 + tbl->gFormulaSize;
	long size = 12 +
		tbl->gFormulaSize +
		(long)tbl->gChanCnt * tbl->gDataCnt * tbl->gDataWidth / 8;
	int i, j;

	memcpy(gamma_public, tbl, size);

	// red, green, blue
	for (i=0; i<3; i++) {
		uint8_t *src, *dst;
		if (tbl->gChanCnt == 3) {
			src = (uint8_t *)data + i * tbl->gDataCnt * tbl->gDataWidth / 8;
		} else {
			src = (uint8_t *)data;
		}

		if (i == 0) {
			dst = (void *)gamma_red;
		} else if (i == 1) {
			dst = (void *)gamma_grn;
		} else {
			dst = (void *)gamma_blu;
		}

		// Calculate and report approximate exponent
		// {
		// 	const char colors[] = "RGB";
		// 	double middle = ((double)src[127] + (double)src[128]) / 2.0 / 255.0;
		// 	double exponent = -log2(middle);
		// 	lprintf("Approximate %c exponent = %.3f\n", colors[i], exponent);
		// }

		for (j=0; j<256 && j<tbl->gDataCnt; j++) {
			dst[j] = src[j * tbl->gDataWidth / 8];
		}
	}
}

// Fills the specified video page with a dithered gray pattern in the
// current video mode. The page number is 0 based.
// --- csMode      Unused
// --- csData      Unused
// --> csPage      Desired display page to gray
// --- csBaseAddr  Unused
static OSStatus GrayPage(VDPageInfo *rec) {
	if (rec->csPage != 0) return controlErr;
	gray();
	updateScreen(0, 0, H, W);
	return noErr;
}

static void gray(void) {
	short x, y;
	uint32_t value;

	if (depth <= k16bit) {
		if (depth == k1bit) {
			value = 0x55555555;
		} else if (depth == k2bit) {
			value = 0x33333333;
		} else if (depth == k4bit) {
			value = 0x0f0f0f0f;
		} else if (depth == k8bit) {
			value = 0x00ff00ff;
		} else if (depth == k16bit) {
			value = 0x0000ffff;
		}

		for (y=0; y<H; y++) {
			for (x=0; x<rowbytes; x+=4) {
				*(uint32_t *)((char *)backbuf + y*rowbytes + x) = value;
			}
			value = ~value;
		}
	} else if (depth == k32bit) {
		value = 0x00000000;
		for (y=0; y<H; y++) {
			for (x=0; x<rowbytes; x+=8) {
				*(uint32_t *)((char *)backbuf + y*rowbytes + x) = value;
				*(uint32_t *)((char *)backbuf + y*rowbytes + x + 4) = ~value;
			}
			value = ~value;
		}
	}
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
	rec->csPage = 1;
	return noErr;
}

// To enable interrupts, pass a csMode value of 0; to disable interrupts,
// pass a csMode value of 1.
// --> csMode      Enable or disable interrupts
static OSStatus SetInterrupt(VDFlagRecord *rec) {
	vblon = !rec->csMode;
	return noErr;
}

// Returns a value of 0 if VBL interrupts are enabled and a value of 1 if
// VBL interrupts are disabled.
// <-- csMode      Interrupts enabled or disabled
static OSStatus GetInterrupt(VDFlagRecord *rec) {
	rec->csMode = !vblon;
	return noErr;
}

// GetSync and SetSync can be used to implement the VESA DPMS as well as
// enable a sync-on-green mode for the frame buffer.
static OSStatus SetSync(VDSyncInfoRec *rec) {
	return noErr;
}

static OSStatus GetSync(VDSyncInfoRec *rec) {
	rec->csMode = 0;
	return noErr;
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
	rec->csConnectFlags = (1 << kTaggingInfoNonStandard);
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
	rec->csMode = depth;
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
	rec->csMode = depth;
	rec->csData = idForRes(W, H, false);
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
	if (rec->csTimingMode < 1 || rec->csTimingMode > resCount()) return paramErr;

	rec->csTimingFormat = kDeclROMtables;
	rec->csTimingData = timingApple_FixedRateLCD;
	rec->csTimingFlags = (1 << kModeValid) | (1 << kModeSafe) | (1 << kModeShowNow);
	if (rezzes[rec->csTimingMode-1].w == W && rezzes[rec->csTimingMode-1].h == H)
		rec->csTimingFlags |= (1 << kModeDefault);

	return noErr;
}

// Sets the pixel depth of the screen.
// --> csMode          Desired relative bit depth
// --- csData          Unused
// --> csPage          Desired display page
// <-- csBaseAddr      Base address of video RAM for this csMode
static OSStatus SetMode(VDPageInfo *rec) {
	size_t i;

	if (rec->csMode < kDepthMode1 || rec->csMode > kDepthModeMax) return paramErr;
	if (rec->csPage != 0) return controlErr;

	for (i=0; i<256; i++) {
		publicCLUT[i].rgb.red = 0x7fff;
		publicCLUT[i].rgb.green = 0x7fff;
		publicCLUT[i].rgb.blue = 0x7fff;
		reCLUT(i);
	}

	change_in_progress = true;
	depth = rec->csMode;
	rowbytes = rowbytesFor(depth, W);
	change_in_progress = false;

	perfTest();
	updateScreen(0, 0, H, W);

	rec->csBaseAddr = backbuf;
	return noErr;
}

// --> csMode          Relative bit depth to switch to
// --> csData          DisplayModeID to switch into
// --> csPage          Video page number to switch into
// <-- csBaseAddr      Base address of the new DisplayModeID
static OSStatus SwitchMode(VDSwitchInfoRec *rec) {
	uint32_t resource;
	short width, height;
	size_t i;

	if (rec->csMode < kDepthMode1 || rec->csMode > kDepthModeMax) return paramErr;
	if (rec->csData < 1 || rec->csData > resCount()) return paramErr;
	if (rec->csPage != 0) return paramErr;

	for (i=0; i<256; i++) {
		publicCLUT[i].rgb.red = 0x7fff;
		publicCLUT[i].rgb.green = 0x7fff;
		publicCLUT[i].rgb.blue = 0x7fff;
		reCLUT(i);
	}

	width = rezzes[rec->csData-1].w;
	height = rezzes[rec->csData-1].h;

	change_in_progress = true;
	resource = setScanout(0 /*scanout id*/, width, height, fbpages);
	if (!resource) {
		change_in_progress = false;
		return paramErr;
	}
	screen_resource = resource;
	W = width;
	H = height;
	depth = rec->csMode;
	rowbytes = rowbytesFor(depth, W);
	change_in_progress = false;

	perfTest();
	updateScreen(0, 0, H, W);

	rec->csBaseAddr = backbuf;
	return noErr;
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
	uint32_t id;

	if (rec->csPreviousDisplayModeID == kDisplayModeIDFindFirstResolution) {
		id = 1;
	} else if (rec->csPreviousDisplayModeID >= 1 && rec->csPreviousDisplayModeID < resCount()) {
		id = rec->csPreviousDisplayModeID + 1;
	} else if (rec->csPreviousDisplayModeID == resCount()) {
		rec->csDisplayModeID = kDisplayModeIDNoMoreResolutions;
		return noErr;
	} else if (rec->csPreviousDisplayModeID == kDisplayModeIDCurrent) {
		id = idForRes(W, H, false);
	} else {
		return paramErr;
	}

	rec->csDisplayModeID = id;
	rec->csHorizontalPixels = rezzes[id-1].w;
	rec->csVerticalLines = rezzes[id-1].h;
	rec->csRefreshRate = 60;
	rec->csMaxDepthMode = k32bit;

	return noErr;
}

// --> csDisplayModeID   ID of the desired DisplayModeID
// --> csDepthMode       Relative bit depth
// <-> *csVPBlockPtr     Pointer to a VPBlock
// <-- csPageCount       Number of pages supported for resolution
//                       and relative bit depth
// <-- csDeviceType      Direct, fixed, or CLUT
static OSStatus GetVideoParameters(VDVideoParametersInfoRec *rec) {
	if (rec->csDepthMode < kDepthMode1 || rec->csDepthMode > kDepthModeMax) {
		return paramErr;
	}

	if (rec->csDisplayModeID < 1 || rec->csDisplayModeID > sizeof(rezzes)/sizeof(*rezzes)) {
		return paramErr;
	}

	memset(rec->csVPBlockPtr, 0, sizeof(*rec->csVPBlockPtr));

	// These fields are always left at zero:
	// vpBaseOffset (offset from NuBus slot to first page, always zero for us)
	// vpBounds.topLeft vpVersion vpPackType vpPackSize vpPlaneBytes

	// These fields don't change per mode:
	rec->csPageCount = 1;
	rec->csVPBlockPtr->vpHRes = 0x00480000;	// Hard coded to 72 dpi
	rec->csVPBlockPtr->vpVRes = 0x00480000;	// Hard coded to 72 dpi

	rec->csVPBlockPtr->vpBounds.bottom = rezzes[rec->csDisplayModeID-1].h;
	rec->csVPBlockPtr->vpBounds.right = rezzes[rec->csDisplayModeID-1].w;
	rec->csVPBlockPtr->vpRowBytes = rowbytesFor(rec->csDepthMode, rezzes[rec->csDisplayModeID-1].w);

	switch (rec->csDepthMode) {
	case k1bit:
		rec->csVPBlockPtr->vpPixelType = 0; // indexed
		rec->csVPBlockPtr->vpPixelSize = 1;
		rec->csVPBlockPtr->vpCmpCount = 1;
		rec->csVPBlockPtr->vpCmpSize = 1;
		rec->csDeviceType = clutType;
		break;
	case k2bit:
		rec->csVPBlockPtr->vpPixelType = 0; // indexed
		rec->csVPBlockPtr->vpPixelSize = 2;
		rec->csVPBlockPtr->vpCmpCount = 1;
		rec->csVPBlockPtr->vpCmpSize = 2;
		rec->csDeviceType = clutType;
		break;
	case k4bit:
		rec->csVPBlockPtr->vpPixelType = 0; // indexed
		rec->csVPBlockPtr->vpPixelSize = 4;
		rec->csVPBlockPtr->vpCmpCount = 1;
		rec->csVPBlockPtr->vpCmpSize = 4;
		rec->csDeviceType = clutType;
		break;
	case k8bit:
		rec->csVPBlockPtr->vpPixelType = 0; // indexed
		rec->csVPBlockPtr->vpPixelSize = 8;
		rec->csVPBlockPtr->vpCmpCount = 1;
		rec->csVPBlockPtr->vpCmpSize = 8;
		rec->csDeviceType = clutType;
		break;
	case k16bit:
		rec->csVPBlockPtr->vpPixelType = 16; // direct
		rec->csVPBlockPtr->vpPixelSize = 16;
		rec->csVPBlockPtr->vpCmpCount = 3;
		rec->csVPBlockPtr->vpCmpSize = 5;
		rec->csDeviceType = directType;
		break;
	case k32bit:
		rec->csVPBlockPtr->vpPixelType = 16; // direct
		rec->csVPBlockPtr->vpPixelSize = 32;
		rec->csVPBlockPtr->vpCmpCount = 3;
		rec->csVPBlockPtr->vpCmpSize = 8;
		rec->csDeviceType = directType;
		break;
	}

	return noErr;
}
