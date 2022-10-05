#include "dirtyrectpatch.h"

#include <stdint.h>
#include <MixedMode.h>
#include <Patches.h>
#include <QuickDraw.h>
#include <Traps.h>
#include <Types.h>

#include "lprintf.h"

// X(StdName, arguments, procInfo)
#define PATCH_LIST \
	X( \
		StdText, \
		(short count, const void *textAddr, Point numer, Point denom), \
		kPascalStackBased \
			| STACK_ROUTINE_PARAMETER(1, kTwoByteCode) \
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(3, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(4, kFourByteCode) \
	) \
	X( \
		StdLine, \
		(Point newPt), \
		kPascalStackBased \
			| STACK_ROUTINE_PARAMETER(1, kFourByteCode) \
	) \
	X( \
		StdRect, \
		(GrafVerb verb, const Rect *r), \
		kPascalStackBased \
			| STACK_ROUTINE_PARAMETER(1, kOneByteCode) \
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode) \
	) \
	X( \
		StdRRect, \
		(GrafVerb verb, const Rect *r, short ovalWidth, short ovalHeight), \
		kPascalStackBased \
			| STACK_ROUTINE_PARAMETER(1, kOneByteCode) \
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(3, kTwoByteCode) \
			| STACK_ROUTINE_PARAMETER(4, kTwoByteCode) \
	) \
	X( \
		StdOval, \
		(GrafVerb verb, const Rect *r), \
		kPascalStackBased \
			| STACK_ROUTINE_PARAMETER(1, kOneByteCode) \
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode) \
	) \
	X( \
		StdArc, \
		(GrafVerb verb, const Rect *r, short startAngle, short arcAngle), \
		kPascalStackBased \
			| STACK_ROUTINE_PARAMETER(1, kOneByteCode) \
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(3, kTwoByteCode) \
			| STACK_ROUTINE_PARAMETER(4, kTwoByteCode) \
	) \
	X( \
		StdPoly, \
		(GrafVerb verb, PolyHandle poly), \
		kPascalStackBased \
			| STACK_ROUTINE_PARAMETER(1, kOneByteCode) \
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode) \
	) \
	X( \
		StdRgn, \
		(GrafVerb verb, RgnHandle rgn), \
		kPascalStackBased \
			| STACK_ROUTINE_PARAMETER(1, kOneByteCode) \
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode) \
	) \
	X( \
		StdBits, \
		(const BitMap *srcBits, const Rect *srcRect, const Rect *dstRect, short mode, RgnHandle maskRgn), \
		kPascalStackBased \
			| STACK_ROUTINE_PARAMETER(1, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(3, kFourByteCode) \
			| STACK_ROUTINE_PARAMETER(4, kTwoByteCode) \
			| STACK_ROUTINE_PARAMETER(5, kFourByteCode) \
	) \

// Enum of MixedMode function signatures for the traps we patch
enum {
	#define X(StdName, args, procInfo) k##StdName##ProcInfo = procInfo,
	PATCH_LIST
	#undef X
};

// Globals in which to store UPPs to old trap handlers
#define X(StdName, args, procInfo) UniversalProcPtr their##StdName;
PATCH_LIST
#undef X

// Prototypes for our new trap handlers
#define X(StdName, args, procInfo) static void my##StdName args;
PATCH_LIST
#undef X

// MixedMode outine descriptors for our new trap handlers
#define X(StdName, args, procInfo) \
	static RoutineDescriptor my##StdName##Desc = BUILD_ROUTINE_DESCRIPTOR( \
		k##StdName##ProcInfo, my##StdName);
PATCH_LIST
#undef X

// Other globals and prototypes
static void secondStage(void);
//static int finishedBooting(void);
static void (*gCallback)(short top, short left, short bottom, short right);
static void checkTraps(void);
static void dumpPort(void);

void InstallDirtyRectPatch(void (*callback)(short top, short left, short bottom, short right)) {
	static char patch[] = {
		0x0c, 0x6f, 0xff, 0xda, 0x00, 0x06, //      cmp.w   #-38,6(sp) ; InitScripts?
		0xff, 0x0e,                         //      bne.s   old
		0x48, 0xe7, 0xe0, 0xe0,             //      movem.l d0-d2/a0-a2,-(sp)
		0x4e, 0xb9, 0xff, 0xff, 0xff, 0xff, //      jsr     <callback>
		0x4c, 0xdf, 0x07, 0x07,             //      movem.l (sp)+,d0-d2/a0-a2
		0x4e, 0xf9, 0xff, 0xff, 0xff, 0xff  // old: jmp     <original>
	};

	static RoutineDescriptor desc = BUILD_ROUTINE_DESCRIPTOR(0, secondStage);

	*(void **)(patch + 14) = &desc;
	*(void **)(patch + 24) = GetOSTrapAddress(_ScriptUtil);

	// Clear 68k emulator's instruction cache
	BlockMove(patch, patch, sizeof(patch));
	BlockMove(&desc, &desc, sizeof(desc));

	SetOSTrapAddress((void *)patch, _ScriptUtil);
	
	gCallback = callback;
}

static void secondStage(void) {
	lprintf("Second stage\n");

	// Install our patches, saving the old traps
	#define X(StdName, args, procInfo) \
		their##StdName = GetToolTrapAddress(_##StdName); \
		SetToolTrapAddress(&my##StdName##Desc, _##StdName);
	PATCH_LIST
	#undef X
}

// static int finishedBooting(void) {
// 	static int finished = 0;
// 	if (!finished && *(signed char *)0x910 >= 0) finished = 1;
// 	return finished;
// }

static void myStdText(short count, const void *textAddr, Point numer, Point denom) {
// 	int i;
// 	lprintf("StdText: ");
// 	for (i=0; i<count; i++) {
// 		lprintf("%c", ((char *)textAddr)[i]);
// 	}
// 	lprintf("\n");
	dumpPort();
	checkTraps();

	CallUniversalProc(theirStdText, kStdTextProcInfo, count, textAddr, numer, denom);
}

static void myStdLine(Point newPt) {
	lprintf("StdLine(%#x)\n", newPt);
	CallUniversalProc(theirStdLine, kStdLineProcInfo, newPt);
}

static void myStdRect(GrafVerb verb, const Rect *r) {
	lprintf("StdRect\n");
	dumpPort();
	CallUniversalProc(theirStdRect, kStdRectProcInfo, verb, r);
}

static void myStdRRect(GrafVerb verb, const Rect *r, short ovalWidth, short ovalHeight) {
	lprintf("StdRRect\n");
	CallUniversalProc(theirStdRRect, kStdRRectProcInfo, verb, r, ovalWidth, ovalHeight);
}

static void myStdOval(GrafVerb verb, const Rect *r) {
	lprintf("StdOval\n");
	CallUniversalProc(theirStdOval, kStdOvalProcInfo, verb, r);
}

static void myStdArc(GrafVerb verb, const Rect *r, short startAngle, short arcAngle) {
	lprintf("StdArc\n");
	CallUniversalProc(theirStdArc, kStdArcProcInfo, verb, r, startAngle, arcAngle);
}

static void myStdPoly(GrafVerb verb, PolyHandle poly) {
	lprintf("StdPoly\n");
	CallUniversalProc(theirStdPoly, kStdPolyProcInfo, verb, poly);
}

static void myStdRgn(GrafVerb verb, RgnHandle rgn) {
	lprintf("StdRgn\n");
	CallUniversalProc(theirStdRgn, kStdRgnProcInfo, verb, rgn);
}

static void myStdBits(const BitMap *srcBits, const Rect *srcRect, const Rect *dstRect, short mode, RgnHandle maskRgn) {
	lprintf("StdBits\n");
	dumpPort();
	CallUniversalProc(theirStdBits, kStdBitsProcInfo, srcBits, srcRect, dstRect, mode, maskRgn);
}

static void checkTraps(void) {
	int bad = 0;

	lprintf("traps:");

	#define X(StdName, args, procInfo) \
		if(*(void **)(0xe00 + 4 * (_##StdName & 0x3ff)) != &my##StdName##Desc) { \
			lprintf(" %s=%x", #StdName, *(long *)(0xe00 + 4 * (_##StdName & 0x3ff))); \
			bad = 1; \
		}
	PATCH_LIST
	#undef X

	if (bad)
		lprintf("\n");
	else
		lprintf(" ok\n");
}

static void dumpPort(void) {
	GrafPtr qd;
	GetPort(&qd);

	lprintf(" text=");
	if (qd->grafProcs->textProc == &myStdTextDesc)
		lprintf("me");
	else
		lprintf("%x", qd->grafProcs->textProc);

	lprintf(" line=");
	if (qd->grafProcs->lineProc == &myStdLineDesc)
		lprintf("me");
	else
		lprintf("%x", qd->grafProcs->lineProc);

	lprintf(" rect=");
	if (qd->grafProcs->rectProc == &myStdRectDesc)
		lprintf("me");
	else
		lprintf("%x", qd->grafProcs->rectProc);

	lprintf(" rRect=");
	if (qd->grafProcs->rRectProc == &myStdRRectDesc)
		lprintf("me");
	else
		lprintf("%x", qd->grafProcs->rRectProc);

	lprintf(" oval=");
	if (qd->grafProcs->ovalProc == &myStdOvalDesc)
		lprintf("me");
	else
		lprintf("%x", qd->grafProcs->ovalProc);

	lprintf(" arc=");
	if (qd->grafProcs->arcProc == &myStdArcDesc)
		lprintf("me");
	else
		lprintf("%x", qd->grafProcs->arcProc);

	lprintf(" poly=");
	if (qd->grafProcs->polyProc == &myStdPolyDesc)
		lprintf("me");
	else
		lprintf("%x", qd->grafProcs->polyProc);

	lprintf(" rgn=");
	if (qd->grafProcs->rgnProc == &myStdRgnDesc)
		lprintf("me");
	else
		lprintf("%x", qd->grafProcs->rgnProc);

	lprintf(" bits=");
	if (qd->grafProcs->bitsProc == &myStdBitsDesc)
		lprintf("me");
	else
		lprintf("%x", qd->grafProcs->bitsProc);
	
	lprintf("\n");
}
