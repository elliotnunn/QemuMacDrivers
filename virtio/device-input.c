#include <Devices.h>
#include <DriverServices.h>
#include <Events.h>
#include <LowMem.h>
#include <MixedMode.h>
#include <Quickdraw.h>
#include <Types.h>
#include <Traps.h>

#include "allocator.h"
#include "lprintf.h"
#include "panic.h"
#include "transport.h"
#include "virtqueue.h"
#include "patch68k.h"

#include "device.h"

#include <string.h>

struct event {
	int16_t type;
	int16_t code;
	int32_t value;
} __attribute((scalar_storage_order("little-endian")));

static OSStatus finalize(DriverFinalInfo *info);
static OSStatus initialize(DriverInitInfo *info);
static void handleEvent(struct event e);
static void myFilter(EventRecord *event, Boolean *result);
static long myDefProc(short varCode, ControlHandle theControl, short message, long param);
static void lateBootHook(void);
static ControlPartCode myTrackControl(ControlRef theControl, Point startPoint, void *actionProc);
static void reQueue(int bufnum);

static struct event *lpage;
static uint32_t ppage;
static void *oldFilter;
static long pendingScroll;
// uint32_t eventPostedTime;
static Handle myCDEF, oldCDEF;
static void *oldTrackControl;
static char oldCDEFState;
static ControlRecord **curScroller;
static Point fakeClickPoint;


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
// 	if (!RegistryPropertyGet(&info->deviceEntry, "debug", NULL, 0)) {
		lprintf_enable = 1;
// 	}

	lprintf("Virtio-input driver starting\n");

	myCDEF = NewHandleSysClear(12 + sizeof (RoutineDescriptor));
	memcpy(*myCDEF, "\x60\x0a", 2);
	*(RoutineDescriptor *)(*myCDEF + 12) = (RoutineDescriptor)BUILD_ROUTINE_DESCRIPTOR(
		kPascalStackBased
			| STACK_ROUTINE_PARAMETER(1, kTwoByteCode)
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode)
			| STACK_ROUTINE_PARAMETER(3, kTwoByteCode)
			| STACK_ROUTINE_PARAMETER(4, kFourByteCode)
			| RESULT_SIZE(kFourByteCode),
		myDefProc);
	BlockMove(*myCDEF, *myCDEF, 12); // remove from emulator code cache

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

	lprintf("Installing GNEFilter\n");

	oldFilter = LMGetGNEFilter();
	LMSetGNEFilter(NewGetNextEventFilterProc(myFilter));

	lprintf("Installing late-boot hook\n");

	Patch68k(
		_Gestalt,
		"0c80 6f732020" //      cmp.l   #'os  ',d0
		"661c"          //      bne.s   old
		"0801 0009"     //      btst    #9,d1
		"6716"          //      beq.s   old
		"0801 000a"     //      btst    #10,d1
		"6610"          //      bne.s   old
		"48e7 e0e0"     //      movem.l d0-d2/a0-a2,-(sp)
		"4eb9 %l"       //      jsr     lateBootHook
		"4cdf 0707"     //      movem.l (sp)+,d0-d2/a0-a2
		"6106"          //      bsr.s   uninstall
		"4ef9 %o",      // old: jmp     originalGestalt
		                // uninstall: (fallthrough code)
		NewRoutineDescriptor((ProcPtr)lateBootHook, kCStackBased, GetCurrentISA())
	);

	return noErr;
}

static void handleEvent(struct event e) {
	enum {
		EV_SYN = 0,
		EV_KEY = 1,
		EV_REL = 2,
		EV_ABS = 3,

		BTN_LEFT = 272,
		BTN_RIGHT = 273,
		BTN_GEAR_DOWN = 336,
		BTN_GEAR_UP = 337,

		REL_WHEEL = 8,

		ABS_X = 0,
		ABS_Y = 1,
	};


	enum {MVFLAG = 1};

	static bool knowpos;
	static long x, y;

	static int knowmask, newbtn, oldbtn;

	// Using a macOS Qemu host, each pixel of desired scroll returns both:
	// type=EV_REL code=REL_WHEEL value=0/1
	// type=EV_KEY code=BTN_GEAR_DOWN/BTN_GEAR_UP value=0
	// But actually I would prefer the new-ish "REL_WHEEL_HI_RES"!

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
	} else if (e.type == EV_REL && e.code == REL_WHEEL) {
// 		lprintf_enable++;
// 		lprintf(e.value > 0 ? "^" : "v");
// 		lprintf_enable--;

		pendingScroll += e.value;

		uint32_t t = LMGetTicks();
// 		if (eventPostedTime == 0 || t - eventPostedTime > 30) {
			PostEvent(mouseDown, 'scrl');
			PostEvent(mouseUp, 'scrl');
// 			eventPostedTime = t;
// 		}
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

// struct EventRecord {
//   EventKind           what;
//   UInt32              message;
//   UInt32              when;
//   Point               where;
//   EventModifiers      modifiers;
// };

static void myFilter(EventRecord *event, Boolean *result) {
	if (curScroller && (*curScroller)->contrlDefProc == myCDEF)
		(*curScroller)->contrlDefProc = oldCDEF;

	if (event->what == mouseDown && event->message == 'scrl') {
		lprintf("scroll event %d\n", pendingScroll);

		struct WindowRecord *wind = (void *)FrontWindow();
		unsigned char *name = *wind->titleHandle;
		lprintf("window title %.*s\n", *name, name+1);

		// Find the scroller to move
		// Currently it's the first vertical in the front window
		// In future select more smartly
		for (curScroller = (void *)wind->controlList; curScroller; curScroller = (**curScroller).nextControl) {
			struct ControlRecord *ptr = *curScroller;

			int16_t w = ptr->contrlRect.right - ptr->contrlRect.left;
			int16_t h = ptr->contrlRect.bottom - ptr->contrlRect.top;

			void *defproc = *ptr->contrlDefProc;
			short cdefnum = *(int16_t *)(defproc + 8);

			lprintf("    control %p %.*s w=%d h=%d hilite=%d cdef=%d\n", ptr, ptr->contrlTitle[0], ptr->contrlTitle+1,
				w, h, ptr->contrlHilite, cdefnum);

			if (h > w && ptr->contrlHilite != 255 && cdefnum == 24) break;
		}

		if (curScroller) {
			// Yes, it's the right event
			// Click in the upper-left of the scroller
			fakeClickPoint = (Point){(*curScroller)->contrlRect.top, (*curScroller)->contrlRect.left};

			// Convert to window coordinates
			GrafPtr oldport;
			GetPort(&oldport);
			SetPort((*curScroller)->contrlOwner);
			LocalToGlobal(&fakeClickPoint);
			SetPort(oldport);

			event->what = mouseDown;
			event->message = 0;
			event->where = fakeClickPoint;
			*result = true;

			lprintf("fakeClickPoint %d,%d\n", fakeClickPoint.v, fakeClickPoint.h);

// 			// Copy some header guff from the old CDEF
// 			oldCDEF = (*curScroller)->contrlDefProc;
// 			memcpy(((char *)(*myCDEF) + 2), ((char *)(*oldCDEF) + 2), 10);
// 			(*curScroller)->contrlDefProc = myCDEF;
		} else {
			// Not the right event
			event->what = nullEvent;
			event->message = 0;
			*result = false;
		}
	}

	if (oldFilter) CallGetNextEventFilterProc(oldFilter, event, result);
}

// drawCntl                      = 0,
// testCntl                      = 1,
// calcCRgns                     = 2,
// initCntl                      = 3,
// dispCntl                      = 4,
// posCntl                       = 5,
// thumbCntl                     = 6,
// dragCntl                      = 7,
// autoTrack                     = 8,
// calcCntlRgn                   = 10,
// calcThumbRgn                  = 11,
// drawThumbOutline              = 12,
// kControlMsgDrawGhost          = 13,
// kControlMsgCalcBestRect       = 14,   /* Calculate best fitting rectangle for control*/
// kControlMsgHandleTracking     = 15,
// kControlMsgFocus              = 16,   /* param indicates action.*/
// kControlMsgKeyDown            = 17,
// kControlMsgIdle               = 18,
// kControlMsgGetFeatures        = 19,
// kControlMsgSetData            = 20,
// kControlMsgGetData            = 21,
// kControlMsgActivate           = 22,
// kControlMsgSetUpBackground    = 23,
// kControlMsgCalcValueFromPos   = 26,
// kControlMsgTestNewMsgSupport  = 27,   /* See if this control supports new messaging*/
// kControlMsgSubValueChanged    = 25,   /* Available in Appearance 1.0.1 or later*/
// kControlMsgSubControlAdded    = 28,   /* Available in Appearance 1.0.1 or later*/
// kControlMsgSubControlRemoved  = 29,   /* Available in Appearance 1.0.1 or later*/
// kControlMsgApplyTextColor     = 30,   /* Available in Appearance 1.1 or later*/
// kControlMsgGetRegion          = 31,   /* Available in Appearance 1.1 or later*/
// kControlMsgFlatten            = 32,   /* Available in Carbon. Param is Collection.*/
// kControlMsgSetCursor          = 33,   /* Available in Carbon. Param is ControlSetCursorRec*/
// kControlMsgDragEnter          = 38,   /* Available in Carbon. Param is DragRef, result is boolean indicating acceptibility of drag.*/
// kControlMsgDragLeave          = 39,   /* Available in Carbon. As above.*/
// kControlMsgDragWithin         = 40,   /* Available in Carbon. As above.*/
// kControlMsgDragReceive        = 41,   /* Available in Carbon. Param is DragRef, result is OSStatus indicating success/failure.*/
// kControlMsgDisplayDebugInfo   = 46,   /* Available in Carbon on X.*/
// kControlMsgContextualMenuClick = 47,  /* Available in Carbon. Param is ControlContextualMenuClickRec*/
// kControlMsgGetClickActivation = 48    /* Available in Carbon. Param is ControlClickActivationRec*/

// WHEN SCROLLER AT TOP-LEFT
// myDefProc called with message 1 param 00140185
//     returns 129
// myDefProc called with message 1 param 00140185
//     returns 129
// myDefProc called with message 1 param 00140185
//     returns 129
// myDefProc called with message 6 param 06fcc068
//     returns 0
// myDefProc called with message 7 param 0000ffff
//     returns 0
// myDefProc called with message 6 param 06fcbfac
//     returns 0
// myDefProc called with message 11 param 06f2f55c
//     returns 0
// myDefProc called with message 0 param 00000081
//     returns 0
// myDefProc called with message 0 param 00000081
//     returns 0

// WHEN SCROLLER ELSEWHERE
// myDefProc called with message 1 param 00140185
//     returns 22
// myDefProc called with message 1 param 00140185
//     returns 22
// myDefProc called with message 34 param 06fcc0f0
//     returns 0
// myDefProc called with message 0 param 00000016
//     returns 0
// myDefProc called with message 0 param 00000081
//     returns 0

static long myDefProc(short varCode, ControlHandle theControl, short message, long param) {
	lprintf("myDefProc called with message %d param %08x\n", message, param);

// 	(*curScroller)->contrlDefProc = oldCDEF;

	long ret = CallUniversalProc((void *)*oldCDEF,
		kPascalStackBased
			| STACK_ROUTINE_PARAMETER(1, kTwoByteCode)
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode)
			| STACK_ROUTINE_PARAMETER(3, kTwoByteCode)
			| STACK_ROUTINE_PARAMETER(4, kFourByteCode)
			| RESULT_SIZE(kFourByteCode),
		varCode, theControl, message, param);

	lprintf("    returns %d\n", ret);
	return ret;

	int32_t min = GetControl32BitMinimum(curScroller);
	int32_t max = GetControl32BitMaximum(curScroller);
	int32_t val = GetControl32BitValue(curScroller);

	val -= pendingScroll;
	pendingScroll = 0;
	if (val < min) val = min;
	if (val > max) val = max;
	SetControl32BitValue(curScroller, val);
	lprintf("manipulated it\n");

	return 129; // ???
}

static void lateBootHook(void) {
	lprintf("At late boot, now patching TrackControl\n");

	oldTrackControl = GetToolTrapAddress(_TrackControl);

	static RoutineDescriptor descTrackControl = BUILD_ROUTINE_DESCRIPTOR(
		kPascalStackBased
			| STACK_ROUTINE_PARAMETER(1, kFourByteCode)
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode)
			| STACK_ROUTINE_PARAMETER(3, kFourByteCode)
			| RESULT_SIZE(kTwoByteCode),
		myTrackControl);

	SetToolTrapAddress(&descTrackControl, _TrackControl);
}

static ControlPartCode myTrackControl(ControlRef theControl, Point startPoint, void *actionProc) {
	lprintf("myTrackControl! point %p\n", startPoint);

	if (theControl != curScroller /*|| startPoint.v != fakeClickPoint.v || startPoint.h != fakeClickPoint.h*/) {
		int ret = CallUniversalProc((void *)oldTrackControl,
			kPascalStackBased
				| STACK_ROUTINE_PARAMETER(1, kFourByteCode)
				| STACK_ROUTINE_PARAMETER(2, kFourByteCode)
				| STACK_ROUTINE_PARAMETER(3, kFourByteCode)
				| RESULT_SIZE(kTwoByteCode),
			theControl, startPoint, actionProc);
		lprintf("aborting with return value %d\n", ret);
		return ret;
	}

	int32_t min = GetControl32BitMinimum(curScroller);
	int32_t max = GetControl32BitMaximum(curScroller);
	int32_t val = GetControl32BitValue(curScroller);

	val -= pendingScroll * 3;
	pendingScroll = 0;
	if (val < min) val = min;
	if (val > max) val = max;
	SetControlValue(curScroller, val);
	lprintf("manipulated it, actionProc is %p\n", actionProc);

	if ((int)actionProc & 1) {
		actionProc = (*curScroller)->contrlAction;
		lprintf("actually changed to %p\n", actionProc);
	}

	if (actionProc) {
		CallControlActionProc(actionProc, curScroller, 129);
	}

	curScroller = NULL;

	return 129; // click was in the thumb
}

static void reQueue(int bufnum) {
	QSend(0, 0/*n-send*/, 1/*n-recv*/,
		(uint32_t []){ppage + sizeof (struct event) * bufnum},
		(uint32_t []){sizeof (struct event)},
		(void *)bufnum);
}

void DNotified(uint16_t q, uint16_t buf, size_t len, void *tag) {
	struct event *e = &lpage[(int)tag];
	//lprintf("Virtio-input event type=%d code=%d value=%d\n", e->type, e->code, e->value);

	handleEvent(*e);

	QFree(q, buf);
	reQueue((int)tag);
	QNotify(0);
}

void DConfigChange(void) {
}
