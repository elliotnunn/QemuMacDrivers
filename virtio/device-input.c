#include <Devices.h>
#include <DriverServices.h>
#include <Events.h>
#include <Types.h>

#include "allocator.h"
#include "lprintf.h"
#include "panic.h"
#include "transport.h"
#include "virtqueue.h"

#include "device.h"

struct event {
	uint16_t type;
	uint16_t code;
	uint32_t value;
} __attribute((scalar_storage_order("little-endian")));

static OSStatus finalize(DriverFinalInfo *info);
static OSStatus initialize(DriverInitInfo *info);
static void handleEvent(struct event e);
void reQueue(int bufnum);

struct event *lpage;
uint32_t ppage;

// Work around a ROM bug:
// If kDriverIsLoadedUponDiscovery is set, the ROM calls GetDriverDescription
// for a pointer to the global below, then frees it with DisposePtr. Padding
// the global to a positive offset within our global area defeats DisposePtr.
char BugWorkaroundExport1[] = "TheDriverDescription must not come first";

DriverDescription TheDriverDescription = {
	kTheDescriptionSignature,
	kInitialDriverDescriptor,
	{"\x0cpci1af4,1052", {0x00, 0x10, 0x80, 0x00}}, // v0.1
	{kDriverIsLoadedUponDiscovery |
		kDriverIsOpenedUponLoad,
		"\x09.virtioinput"},
	{1, // nServices
	{{kServiceCategoryNdrvDriver, kNdrvTypeIsGeneric, {0x00, 0x10, 0x80, 0x00}}}} //v0.1
};

char BugWorkaroundExport2[] = "TheDriverDescription must not come first";

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
	case kReadCommand:
		err = readErr;
		break;
	case kWriteCommand:
		err = writErr;
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

static OSStatus finalize(DriverFinalInfo *info) {
	return noErr;
}

static OSStatus initialize(DriverInitInfo *info) {
	if (!RegistryPropertyGet(&info->deviceEntry, "debug", NULL, 0)) {
		lprintf_enable = 1;
	}

	lprintf("Virtio-input driver starting\n");

	lpage = AllocPages(1, &ppage);
	if (lpage == NULL) return openErr;

	if (!VInit(&info->deviceEntry)) return openErr;

	VDriverOK();

	int nbuf = QInit(0, 4096 / sizeof (struct event));
	if (nbuf == 0) {
		VFail();
		return openErr;
	}

	QInterest(0, 1); // enable interrupts
	for (int i=0; i<nbuf; i++) {
		reQueue(i);
	}
	QNotify(0);

	lprintf("Virtio-input driver started\n");

	return noErr;
}

static void handleEvent(struct event e) {
	enum {
		EV_SYN = 0,
		EV_KEY = 1,
		EV_ABS = 3,

		BTN_LEFT = 272,
		BTN_RIGHT = 273,

		ABS_X = 0,
		ABS_Y = 1,
	};


	enum {MVFLAG = 1};

	static bool knowpos;
	static long x, y;

	static int knowmask, newbtn, oldbtn;

	if (e.type == EV_ABS && e.code == ABS_X) {
		knowpos = true;
		x = e.value;
	} else if (e.type == EV_ABS && e.code == ABS_Y) {
		knowpos = true;
		y = e.value;
	} else if (e.type == 1 && e.code == BTN_LEFT) {
		knowmask |= 1;
		if (e.value) newbtn |= 1;
	} else if (e.type == 1 && e.code == BTN_RIGHT) {
		knowmask |= 2;
		if (e.value) newbtn |= 2;
	} else if (e.type == EV_SYN) {
		if (knowpos) {
			// Scale to screen size (in lowmem globals)
			unsigned long realx = x * *(int16_t *)0xc20 / 0x8000 + 1;
			unsigned long realy = y * *(int16_t *)0xc22 / 0x8000 + 1;

			unsigned long point = (realy << 16) | realx;

			*(unsigned long *)0x828 = point; // MTemp
			*(unsigned long *)0x82c = point; // RawMouse

			*(char *)0x8ce = *(char *)0x8cf; // CrsrNew = CrsrCouple

			// Call JCrsrTask to redraw the cursor immediately.
			// Feels much more responsive than waiting for another interrupt.
			// Could a race condition garble the cursor? Haven't seen it happen.
			// if (*(char *)(0x174 + 7) & 1) // Uncomment to switch on shift key
			CallUniversalProc(*(void **)0x8ee, kPascalStackBased);
		}

		knowpos = false;

		newbtn = (newbtn & knowmask) | (oldbtn & ~knowmask);

		if ((oldbtn != 0) != (newbtn != 0)) {
			*(unsigned char *)0x172 = newbtn ? 0 : 0x80;

			EvQEl *osevent;
			PPostEvent(newbtn ? mouseDown : mouseUp, 0, &osevent);

			// Right-click becomes control-click
			if (newbtn & 2) {
				osevent->evtQModifiers |= 0x1000;
			}
		}

		oldbtn = newbtn;
		knowmask = 0;
		newbtn = 0;
	}
}

void reQueue(int bufnum) {
	QSend(0, 0/*n-send*/, 1/*n-recv*/,
		(uint32_t []){ppage + sizeof (struct event) * bufnum},
		(uint32_t []){sizeof (struct event)},
		(void *)bufnum);
}

void DNotified(uint16_t q, uint16_t buf, size_t len, void *tag) {
	struct event *e = &lpage[(int)tag];
	lprintf("Virtio-input event type=%d code=%d value=%d\n", e->type, e->code, e->value);

	handleEvent(*e);

	QFree(q, buf);
	reQueue((int)tag);
	QNotify(0);
}

void DConfigChange(void) {
}
