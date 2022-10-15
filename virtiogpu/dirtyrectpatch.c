#include "dirtyrectpatch.h"

#include <stdint.h>
#include <string.h>
#include <CodeFragments.h>
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

// The system LocalToGlobal ignores portRect!
#define LOCALTOGLOBAL(port, tp, lt, bt, rt) { \
	Rect *MACROBOUNDS; \
	short dx, dy; \
	if ((port->portBits.rowBytes & 0xc000) == 0xc000) \
		MACROBOUNDS = &(*((CGrafPort *)port)->portPixMap)->bounds; \
	else \
		MACROBOUNDS = &port->portBits.bounds; \
	dy = MACROBOUNDS->top - port->portRect.top; \
	dx = MACROBOUNDS->left - port->portRect.left; \
	tp += dy; \
	bt += dy; \
	lt += dx; \
	rt += dx; \
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
static void flashRect(int t, int l, int b, int r);
static void tintRect(long color, int t, int l, int b, int r);
static void delay(unsigned long ticks);
static void myDevLoop(void (*drawProc)(void *), void *arg, const Rect *rect); // prototype
static void (*theirDevLoop)(void (*drawProc)(void *), void *arg, const Rect *rect); // global
static long tvector[2];

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
	OSErr err;
	CFragConnectionID conn;
	void *mainAddr;
	void *symAddr;
	CFragSymbolClass symClass;
	Str255 errMessage;

	static int done = 0;
	if (done) return;

	// If CurApName is set then we missed our chance
	if (*(signed char *)0x910 >= 0) {
		done = 1;
		return;
	}

	err = GetSharedLibrary("\pNQD", 'pwpc', kFindCFrag, &conn, (Ptr *)&mainAddr, errMessage);
	lprintf("GetSharedLibrary = %d\n", err);
	if (err) return;

	// If this symbol exists then we have a good enough version of NQD
	err = FindSymbol(conn, "\pQDNativeDeviceLoop", (Ptr *)&symAddr, &symClass);
	lprintf("FindSymbol QDNativeDeviceLoop = %d\n", err);
	if (err) return;

	err = FindSymbol(conn, "\pNQDStdRect", (Ptr *)&symAddr, &symClass);
	lprintf("FindSymbol NQDStdRect = %d\n", err);
	if (err) return;

	// The immediately preceding TVector is always DevLoop,
	// knowledge obtained through paintstaking disassembly
	symAddr = (char *)symAddr - 8;

	memcpy(tvector, symAddr, 8);
	theirDevLoop = (void *)tvector;

	// Override their DevLoop routine with ours
	memcpy(symAddr, &myDevLoop, 8);

// 	#define X(trap, StdName, args, procInfo) \
// 		if (*(unsigned short *)GetToolTrapAddress(trap) != 0xaafe) return;
// 	PATCH_LIST
// 	#undef X
// 
// 	done = 1;
// 
// 	// Install our patches, saving the old traps
// 	#define X(trap, StdName, args, procInfo) \
// 		their##StdName = GetToolTrapAddress(trap); \
// 		SetToolTrapAddress(&my##StdName##Desc, trap);
// 	PATCH_LIST
// 	#undef X
// 
// 	lprintf("Installed QuickDraw patches\n");
}

static void myDevLoop(void (*drawProc)(void *), void *arg, const Rect *rect) {
	int t, l, b, r;
	GrafPort *port;

	GetPort(&port);
	theirDevLoop(drawProc, arg, rect);
	t = rect->top;
	l = rect->left;
	b = rect->bottom;
	r = rect->right;
	//drawProc(arg);
	LOCALTOGLOBAL(port, t, l, b, r);
	flashRect(t, l, b, r);

	// This would call the original routine:
	// theirDevLoop(drawProc, arg, rect);
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
// 	int t, l, b, r;
// 
// 	GrafPort *port;
// 	GetPort(&port);
// 
// 	if (!isOnscreen(port)) {
// 		CallUniversalProc(theirStdText, kStdTextProcInfo, count, textAddr, numer, denom);
// 		return;
// 	}
// 
// 	l = port->pnLoc.h;
	CallUniversalProc(theirStdText, kStdTextProcInfo, count, textAddr, numer, denom);
// 	r = port->pnLoc.h;
// 
// 	if (numer.v <= denom.v) {
// 		t = port->pnLoc.v - port->txSize;
// 		b = port->pnLoc.v + port->txSize;
// 	} else {
// 		t = -0x8000;
// 		b = 0x7fff;
// 	}
// 
// 	QUICKCLIP(port, t, l, b, r);
// 	LOCALTOGLOBAL(port, t, l, b, r);

	//tintRect(0xff0000, t, l, b, r);
}

static void myStdLine(Point newPt) {
// 	int t, l, b, r;
// 	GrafPort *port;
// 
// 	GetPort(&port);
// 
// 	t = port->pnLoc.v;
// 	l = port->pnLoc.h;
// 	b = newPt.v;
// 	r = newPt.h;
// 	
// 	if (t > b) {
// 		int swap = t;
// 		t = b;
// 		b = swap;
// 	}
// 
// 	if (l > r) {
// 		int swap = l;
// 		l = r;
// 		r = swap;
// 	}
// 
// 	b += port->pnSize.v;
// 	l += port->pnSize.h;
// 	
// 	QUICKCLIP(port, t, l, b, r);
// 	LOCALTOGLOBAL(port, t, l, b, r);

	CallUniversalProc(theirStdLine, kStdLineProcInfo, newPt);

	//tintRect(0x00ff00, t, l, b, r);
}

static void myStdRect(GrafVerb verb, const Rect *rect) {
	int t, l, b, r;
	GrafPort *port;

	GetPort(&port);

	if (!isOnscreen(port)) {
		CallUniversalProc(theirStdRect, kStdRectProcInfo, verb, rect);
		return;
	}

	t = rect->top;
	l = rect->left;
	b = rect->bottom;
	r = rect->right;

	QUICKCLIP(port, t, l, b, r);
	LOCALTOGLOBAL(port, t, l, b, r);

	CallUniversalProc(theirStdRect, kStdRectProcInfo, verb, rect);

	//tintRect(0x0000ff, t, l, b, r);
}

static void myStdRRect(GrafVerb verb, const Rect *rect, short ovalWidth, short ovalHeight) {
// 	int t, l, b, r;
// 	GrafPort *port;
// 
// 	GetPort(&port);
// 
// 	t = rect->top;
// 	l = rect->left;
// 	b = rect->bottom;
// 	r = rect->right;
// 	
// 	QUICKCLIP(port, t, l, b, r);
// 	LOCALTOGLOBAL(port, t, l, b, r);

	CallUniversalProc(theirStdRRect, kStdRRectProcInfo, verb, rect, ovalWidth, ovalHeight);

	//tintRect(0x000000, t, l, b, r);
}

static void myStdOval(GrafVerb verb, const Rect *r) {
	//lprintf("StdOval\n");
	CallUniversalProc(theirStdOval, kStdOvalProcInfo, verb, r);
}

static void myStdArc(GrafVerb verb, const Rect *r, short startAngle, short arcAngle) {
	//lprintf("StdArc\n");
	CallUniversalProc(theirStdArc, kStdArcProcInfo, verb, r, startAngle, arcAngle);
}

static void myStdPoly(GrafVerb verb, PolyHandle poly) {
	//lprintf("StdPoly\n");
	CallUniversalProc(theirStdPoly, kStdPolyProcInfo, verb, poly);
}

static void myStdRgn(GrafVerb verb, RgnHandle rgn) {
	//lprintf("StdRgn\n");
	CallUniversalProc(theirStdRgn, kStdRgnProcInfo, verb, rgn);
}

static void myStdBits(const BitMap *srcBits, const Rect *srcRect, const Rect *dstRect, short mode, RgnHandle maskRgn) {
	//lprintf("StdBits\n");
	CallUniversalProc(theirStdBits, kStdBitsProcInfo, srcBits, srcRect, dstRect, mode, maskRgn);
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

static void flashRect(int t, int l, int b, int r) {
	PixMap *screen = *(**(GDevice ***)0x8a4)->gdPMap;

	long *ptr;
	int x, y;

	if (t < 0) t = 0;
	if (l < 0) l = 0;
	if (b > screen->bounds.bottom) b = screen->bounds.bottom;
	if (r > screen->bounds.right) r = screen->bounds.right;

	//lprintf("tintRect(%d,%d,%d,%d,%d)\n", color, t, l, b, r);

	for (y = t; y < b; y++) {
		ptr = (long *)((char *)screen->baseAddr + y * (screen->rowBytes & 0x3fff) + 4 * l);
		for (x = l; x < r; x += 2) {
			*ptr++ ^= 0x00ffffff;
		}
	}

	delay(1);

	for (y = t; y < b; y++) {
		ptr = (long *)((char *)screen->baseAddr + y * (screen->rowBytes & 0x3fff) + 4 * l);
		for (x = l; x < r; x += 2) {
			*ptr++ ^= 0x00ffffff;
		}
	}
}

#include <LowMem.h>

static void delay(unsigned long ticks) {
	unsigned long t0 = LMGetTicks();

	while (LMGetTicks() - t0 < ticks) {}
}
