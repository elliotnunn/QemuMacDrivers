#include "dirtyrectpatch.h"

#include <stdint.h>
#include <MixedMode.h>
#include <Patches.h>
#include <QuickDraw.h>
#include <Traps.h>
#include <Types.h>

#include "lprintf.h"

// X(trap, StdName, arguments, procInfo)
#define PATCH_LIST \
	X( \
		0xa922, \
		BeginUpdate, \
		(GrafPort *window), \
		kPascalStackBased \
			| STACK_ROUTINE_PARAMETER(1, kFourByteCode) \
	) \
	X( \
		0xa923, \
		EndUpdate, \
		(GrafPort *window), \
		kPascalStackBased \
			| STACK_ROUTINE_PARAMETER(1, kFourByteCode) \
	) \
	X( \
		0xa882, \
		StdText, \
		(short count, const void *textAddr, Point numer, Point denom), \
		kPascalStackBased \
			| STACK_ROUTINE_PARAMETER(1, kTwoByteCode) \
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(3, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(4, kFourByteCode) \
	) \
	X( \
		0xa890, \
		StdLine, \
		(Point newPt), \
		kPascalStackBased \
			| STACK_ROUTINE_PARAMETER(1, kFourByteCode) \
	) \
	X( \
		0xa8a0, \
		StdRect, \
		(GrafVerb verb, const Rect *rect), \
		kPascalStackBased \
			| STACK_ROUTINE_PARAMETER(1, kOneByteCode) \
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode) \
	) \
	X( \
		0xa8af, \
		StdRRect, \
		(GrafVerb verb, const Rect *rect, short ovalWidth, short ovalHeight), \
		kPascalStackBased \
			| STACK_ROUTINE_PARAMETER(1, kOneByteCode) \
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(3, kTwoByteCode) \
			| STACK_ROUTINE_PARAMETER(4, kTwoByteCode) \
	) \
	X( \
		0xa8b6, \
		StdOval, \
		(GrafVerb verb, const Rect *r), \
		kPascalStackBased \
			| STACK_ROUTINE_PARAMETER(1, kOneByteCode) \
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode) \
	) \
	X( \
		0xa8bd, \
		StdArc, \
		(GrafVerb verb, const Rect *r, short startAngle, short arcAngle), \
		kPascalStackBased \
			| STACK_ROUTINE_PARAMETER(1, kOneByteCode) \
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(3, kTwoByteCode) \
			| STACK_ROUTINE_PARAMETER(4, kTwoByteCode) \
	) \
	X( \
		0xa8c5, \
		StdPoly, \
		(GrafVerb verb, PolyHandle poly), \
		kPascalStackBased \
			| STACK_ROUTINE_PARAMETER(1, kOneByteCode) \
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode) \
	) \
	X( \
		0xa8d1, \
		StdRgn, \
		(GrafVerb verb, RgnHandle rgn), \
		kPascalStackBased \
			| STACK_ROUTINE_PARAMETER(1, kOneByteCode) \
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode) \
	) \
	X( \
		0xa8eb, \
		StdBits, \
		(const BitMap *srcBits, const Rect *srcRect, const Rect *dstRect, short mode, RgnHandle maskRgn), \
		kPascalStackBased \
			| STACK_ROUTINE_PARAMETER(1, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(3, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(4, kTwoByteCode) \
			| STACK_ROUTINE_PARAMETER(5, kFourByteCode) \
	) \

// The classics
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

// Macro so we can efficiently pass rect by reference
#define QUICKCLIP(port, tp, lt, bt, rt) { \
	Rect *bb1 = &(*port->clipRgn)->rgnBBox; \
	Rect *bb2 = &(*port->visRgn)->rgnBBox; \
	tp = MAX(tp, bb1->top); \
	lt = MAX(lt, bb1->left); \
	bt = MIN(bt, bb1->bottom); \
	rt = MIN(rt, bb1->right); \
	tp = MAX(tp, bb2->top); \
	lt = MAX(lt, bb2->left); \
	bt = MIN(bt, bb2->bottom); \
	rt = MIN(rt, bb2->right); \
}

#define LOCALTOGLOBAL(port, tp, lt, bt, rt) { \
	Rect *MACROBOUNDS; \
	if ((port->portBits.rowBytes & 0xc000) == 0xc000) \
		MACROBOUNDS = &(*((CGrafPort *)port)->portPixMap)->bounds; \
	else \
		MACROBOUNDS = &port->portBits.bounds; \
	tp -= MACROBOUNDS->top; \
	bt -= MACROBOUNDS->top; \
	lt -= MACROBOUNDS->left; \
	rt -= MACROBOUNDS->left; \
}

// Enum of MixedMode function signatures for the traps we patch
enum {
	#define X(trap, StdName, args, procInfo) k##StdName##ProcInfo = procInfo,
	PATCH_LIST
	#undef X
};

// Globals in which to store UPPs to old trap handlers
#define X(trap, StdName, args, procInfo) UniversalProcPtr their##StdName;
PATCH_LIST
#undef X

// Prototypes for our new trap handlers
#define X(trap, StdName, args, procInfo) static void my##StdName args;
PATCH_LIST
#undef X

// MixedMode outine descriptors for our new trap handlers
#define X(trap, StdName, args, procInfo) \
	static RoutineDescriptor my##StdName##Desc = BUILD_ROUTINE_DESCRIPTOR( \
		k##StdName##ProcInfo, my##StdName);
PATCH_LIST
#undef X

// Other globals and prototypes
static void secondStage(void);
static void (*gCallback)(short top, short left, short bottom, short right);
static int isOnscreen(GrafPort *thePort);
static void tintRect(long color, int t, int l, int b, int r);
static void delay(unsigned long ticks);

void InstallDirtyRectPatch(void (*callback)(short top, short left, short bottom, short right)) {
	static char patch[] = {
		0x48, 0xe7, 0xe0, 0xe0,             //      movem.l d0-d2/a0-a2,-(sp)
		0x4e, 0xb9, 0xff, 0xff, 0xff, 0xff, //      jsr     <callback>
		0x4c, 0xdf, 0x07, 0x07,             //      movem.l (sp)+,d0-d2/a0-a2
		0x4e, 0xf9, 0xff, 0xff, 0xff, 0xff  // old: jmp     <original>
	};

	static RoutineDescriptor desc = BUILD_ROUTINE_DESCRIPTOR(0, secondStage);

	*(void **)(patch + 6) = &desc;
	*(void **)(patch + 16) = GetOSTrapAddress(_InitEvents);

	// Clear 68k emulator's instruction cache
	BlockMove(patch, patch, sizeof(patch));
	BlockMove(&desc, &desc, sizeof(desc));

	SetOSTrapAddress((void *)patch, _InitEvents);

	gCallback = callback;
}

static void secondStage(void) {
	static int done = 0;
	if (done) return;

	// If CurApName is set then we missed our chance
	if (*(signed char *)0x910 >= 0) {
		done = 1;
		return;
	}

	#define X(trap, StdName, args, procInfo) \
		if (*(unsigned short *)GetToolTrapAddress(trap) != 0xaafe) return;
	PATCH_LIST
	#undef X

	done = 1;

	// Install our patches, saving the old traps
	#define X(trap, StdName, args, procInfo) \
		their##StdName = GetToolTrapAddress(trap); \
		SetToolTrapAddress(&my##StdName##Desc, trap);
	PATCH_LIST
	#undef X

	lprintf("Installed QuickDraw patches\n");
}

static void myBeginUpdate(GrafPort *theWindow) {
	lprintf("BeginUpdate(%#x)\n", theWindow);
	CallUniversalProc(theirBeginUpdate, kBeginUpdateProcInfo, theWindow);
}

static void myEndUpdate(GrafPort *theWindow) {
	lprintf("EndUpdate(%#x)\n", theWindow);
	CallUniversalProc(theirEndUpdate, kEndUpdateProcInfo, theWindow);
}

static void myStdText(short count, const void *textAddr, Point numer, Point denom) {
	int t, l, b, r;

	GrafPort *port;
	GetPort(&port);

	l = port->pnLoc.h;
	CallUniversalProc(theirStdText, kStdTextProcInfo, count, textAddr, numer, denom);
	if (!isOnscreen(port)) return;
	r = port->pnLoc.h;

	if (numer.v <= denom.v) {
		t = port->pnLoc.v - port->txSize;
		b = port->pnLoc.v + port->txSize;
	} else {
		t = -0x8000;
		b = 0x7fff;
	}

	QUICKCLIP(port, t, l, b, r);
	LOCALTOGLOBAL(port, t, l, b, r);
	gCallback(t, l, b, r);
}

static void myStdLine(Point newPt) {
	int t, l, b, r;
	GrafPort *port;

	CallUniversalProc(theirStdLine, kStdLineProcInfo, newPt);

	GetPort(&port);
	if (!isOnscreen(port)) return;

	t = port->pnLoc.v;
	l = port->pnLoc.h;
	b = newPt.v;
	r = newPt.h;
	
	if (t > b) {
		int swap = t;
		t = b;
		b = swap;
	}

	if (l > r) {
		int swap = l;
		l = r;
		r = swap;
	}

	b += port->pnSize.v;
	l += port->pnSize.h;
	
	QUICKCLIP(port, t, l, b, r);
	LOCALTOGLOBAL(port, t, l, b, r);
	gCallback(t, l, b, r);
}

static void myStdRect(GrafVerb verb, const Rect *rect) {
	int t, l, b, r;
	GrafPort *port;

	CallUniversalProc(theirStdRect, kStdRectProcInfo, verb, rect);

	GetPort(&port);
	if (!isOnscreen(port)) return;

	t = rect->top;
	l = rect->left;
	b = rect->bottom;
	r = rect->right;

	QUICKCLIP(port, t, l, b, r);
	LOCALTOGLOBAL(port, t, l, b, r);
	gCallback(t, l, b, r);
}

static void myStdRRect(GrafVerb verb, const Rect *rect, short ovalWidth, short ovalHeight) {
	int t, l, b, r;
	GrafPort *port;

	CallUniversalProc(theirStdRRect, kStdRRectProcInfo, verb, rect, ovalWidth, ovalHeight);

	GetPort(&port);
	if (!isOnscreen(port)) return;

	t = rect->top;
	l = rect->left;
	b = rect->bottom;
	r = rect->right;

	QUICKCLIP(port, t, l, b, r);
	LOCALTOGLOBAL(port, t, l, b, r);
	gCallback(t, l, b, r);
}

static void myStdOval(GrafVerb verb, const Rect *rect) {
	int t, l, b, r;
	GrafPort *port;

	CallUniversalProc(theirStdOval, kStdOvalProcInfo, verb, rect);

	GetPort(&port);
	if (!isOnscreen(port)) return;

	t = rect->top;
	l = rect->left;
	b = rect->bottom;
	r = rect->right;

	QUICKCLIP(port, t, l, b, r);
	LOCALTOGLOBAL(port, t, l, b, r);
	gCallback(t, l, b, r);
}

static void myStdArc(GrafVerb verb, const Rect *rect, short startAngle, short arcAngle) {
	int t, l, b, r;
	GrafPort *port;

	CallUniversalProc(theirStdArc, kStdArcProcInfo, verb, rect, startAngle, arcAngle);

	GetPort(&port);
	if (!isOnscreen(port)) return;

	t = rect->top;
	l = rect->left;
	b = rect->bottom;
	r = rect->right;

	QUICKCLIP(port, t, l, b, r);
	LOCALTOGLOBAL(port, t, l, b, r);
	gCallback(t, l, b, r);
}

static void myStdPoly(GrafVerb verb, PolyHandle poly) {
	Rect *rect;
	int t, l, b, r;
	GrafPort *port;

	CallUniversalProc(theirStdPoly, kStdPolyProcInfo, verb, poly);

	GetPort(&port);
	if (!isOnscreen(port)) return;

	rect = &(**poly).polyBBox;
	t = rect->top;
	l = rect->left;
	b = rect->bottom + port->pnSize.v;
	r = rect->right + port->pnSize.h;

	QUICKCLIP(port, t, l, b, r);
	LOCALTOGLOBAL(port, t, l, b, r);
	gCallback(t, l, b, r);
}

static void myStdRgn(GrafVerb verb, RgnHandle rgn) {
	Rect *rect;
	int t, l, b, r;
	GrafPort *port;

	CallUniversalProc(theirStdRgn, kStdRgnProcInfo, verb, rgn);

	GetPort(&port);
	if (!isOnscreen(port)) return;

	rect = &(**rgn).rgnBBox;
	t = rect->top;
	l = rect->left;
	b = rect->bottom;
	r = rect->right;

	QUICKCLIP(port, t, l, b, r);
	LOCALTOGLOBAL(port, t, l, b, r);
	gCallback(t, l, b, r);
}

static void myStdBits(const BitMap *srcBits, const Rect *srcRect, const Rect *dstRect, short mode, RgnHandle maskRgn) {
	int t, l, b, r;
	GrafPort *port;

	CallUniversalProc(theirStdBits, kStdBitsProcInfo, srcBits, srcRect, dstRect, mode, maskRgn);

	GetPort(&port);
	if (!isOnscreen(port)) return;

	t = dstRect->top;
	l = dstRect->left;
	b = dstRect->bottom;
	r = dstRect->right;

	QUICKCLIP(port, t, l, b, r);
	LOCALTOGLOBAL(port, t, l, b, r);
	gCallback(t, l, b, r);
}

static int isOnscreen(GrafPort *thePort) {
	GDevice *mainDev;
	void *screenBits;
	void *portBits;

	if (thePort->picSave) return 0;

	mainDev = **(GDevice ***)0x8a4;
	screenBits = (*mainDev->gdPMap)->baseAddr;

	if ((thePort->portBits.rowBytes & 0xc000) == 0xc000) {
		portBits = (*((CGrafPort *)thePort)->portPixMap)->baseAddr;
	} else {
		portBits = thePort->portBits.baseAddr;
	}
	
 	return portBits == screenBits;
}

static void tintRect(long color, int t, int l, int b, int r) {
	PixMap *screen = *(**(GDevice ***)0x8a4)->gdPMap;

	long *ptr;
	int x, y;
	int odd = 0;

	//lprintf("tintRect(%d,%d,%d,%d,%d)\n", color, t, l, b, r);

	for (y = t; y < b; y++) {
		ptr = (long *)((char *)screen->baseAddr + y * (screen->rowBytes & 0x3fff) + 4 * l);

		*ptr = color;

		if (odd) ptr++;
		odd = !odd;

		for (x = l; x < r; x += 2) {
			*ptr = color;
			if (y == t)
				ptr++;
			else
				ptr += 2;
		}
	};
}

#include <LowMem.h>

static void delay(unsigned long ticks) {
	unsigned long t0 = LMGetTicks();

	while (LMGetTicks() - t0 < ticks) {}
}
