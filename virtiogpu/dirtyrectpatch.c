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
	kProcInfoGetTrap = kRegisterBased
		| RESULT_SIZE(kFourByteCode)
		| REGISTER_RESULT_LOCATION(kRegisterA0)
		| REGISTER_ROUTINE_PARAMETER(1, kRegisterD1, kFourByteCode)
		| REGISTER_ROUTINE_PARAMETER(2, kRegisterD0, kTwoByteCode),

	kProcInfoSetTrap = kRegisterBased
		| REGISTER_ROUTINE_PARAMETER(1, kRegisterD1, kFourByteCode)
		| REGISTER_ROUTINE_PARAMETER(2, kRegisterA0, kFourByteCode)
		| REGISTER_ROUTINE_PARAMETER(3, kRegisterD0, kTwoByteCode),

	#define X(StdName, args, procInfo) StdName##ProcInfo = procInfo,
	PATCH_LIST
	#undef X
};

// Globals in which to store UPPs to old trap handlers
static UniversalProcPtr theirGet;
static UniversalProcPtr theirSet;

#define X(StdName, args, procInfo) UniversalProcPtr their##StdName;
PATCH_LIST
#undef X

// Prototypes for our new trap handlers
static UniversalProcPtr myGet(uint16_t thisTrapNum, uint16_t trapNum);
static void mySet(uint16_t thisTrapNum, UniversalProcPtr trapAddr, uint16_t trapNum);

#define X(StdName, args, procInfo) static void my##StdName args;
PATCH_LIST
#undef X

// MixedMode outine descriptors for our new trap handlers
static RoutineDescriptor myGetDesc = BUILD_ROUTINE_DESCRIPTOR(kProcInfoGetTrap, myGet);
static RoutineDescriptor mySetDesc = BUILD_ROUTINE_DESCRIPTOR(kProcInfoSetTrap, mySet);

#define X(StdName, args, procInfo) \
	static RoutineDescriptor my##StdName##Desc = BUILD_ROUTINE_DESCRIPTOR( \
		StdName##ProcInfo, my##StdName);
PATCH_LIST
#undef X

// Other globals and prototypes
static int finishedBooting(void);
static void (*gCallback)(short top, short left, short bottom, short right);
static void checkTraps(void);
static void dumpPort(void);

void InstallDirtyRectPatch(void (*callback)(short top, short left, short bottom, short right)) {
	// Install our patches, saving the old traps
	#define X(StdName, args, procInfo) \
		their##StdName = GetToolTrapAddress(_##StdName); \
		SetToolTrapAddress(&my##StdName##Desc, _##StdName);
	PATCH_LIST
	#undef X

	theirGet = GetOSTrapAddress(_GetToolTrapAddress);
	SetOSTrapAddress(&myGetDesc, _GetToolTrapAddress);

	theirSet = GetOSTrapAddress(_SetToolTrapAddress);
	SetOSTrapAddress(&mySetDesc, _SetToolTrapAddress);

	gCallback = callback;
}

static UniversalProcPtr myGet(uint16_t thisTrapNum, uint16_t trapNum) {
	if (!finishedBooting()) {
		switch (trapNum & 0x3ff) {
			#define X(StdName, args, procInfo) case _##StdName & 0x3ff: return their##StdName;
			PATCH_LIST
			#undef X
		}
	}

	return (UniversalProcPtr)CallOSTrapUniversalProc(theirGet, kProcInfoGetTrap, thisTrapNum, trapNum);
}

static void mySet(uint16_t thisTrapNum, UniversalProcPtr trapAddr, uint16_t trapNum) {
	if (!finishedBooting()) {
		switch (trapNum & 0x3ff) {
			#define X(StdName, args, procInfo) case _##StdName & 0x3ff: their##StdName = trapAddr; return;
			PATCH_LIST
			#undef X
		}
	}

	CallOSTrapUniversalProc(theirSet, kProcInfoSetTrap, thisTrapNum, trapAddr, trapNum);
}

static int finishedBooting(void) {
	static int finished = 0;
	if (!finished && *(signed char *)0x910 >= 0) finished = 1;
	return finished;
}

static void myStdText(short count, const void *textAddr, Point numer, Point denom) {
// 	int i;
// 	lprintf("StdText: ");
// 	for (i=0; i<count; i++) {
// 		lprintf("%c", ((char *)textAddr)[i]);
// 	}
// 	lprintf("\n");
	dumpPort();
	checkTraps();

	CallUniversalProc(theirStdText, StdTextProcInfo, count, textAddr, numer, denom);
}

static void myStdLine(Point newPt) {
	lprintf("StdLine(%#x)\n", newPt);
	CallUniversalProc(theirStdLine, StdLineProcInfo, newPt);
}

static void myStdRect(GrafVerb verb, const Rect *r) {
	lprintf("StdRect\n");
	dumpPort();
	CallUniversalProc(theirStdRect, StdRectProcInfo, verb, r);
}

static void myStdRRect(GrafVerb verb, const Rect *r, short ovalWidth, short ovalHeight) {
	lprintf("StdRRect\n");
	CallUniversalProc(theirStdRRect, StdRRectProcInfo, verb, r, ovalWidth, ovalHeight);
}

static void myStdOval(GrafVerb verb, const Rect *r) {
	lprintf("StdOval\n");
	CallUniversalProc(theirStdOval, StdOvalProcInfo, verb, r);
}

static void myStdArc(GrafVerb verb, const Rect *r, short startAngle, short arcAngle) {
	lprintf("StdArc\n");
	CallUniversalProc(theirStdArc, StdArcProcInfo, verb, r, startAngle, arcAngle);
}

static void myStdPoly(GrafVerb verb, PolyHandle poly) {
	lprintf("StdPoly\n");
	CallUniversalProc(theirStdPoly, StdPolyProcInfo, verb, poly);
}

static void myStdRgn(GrafVerb verb, RgnHandle rgn) {
	lprintf("StdRgn\n");
	CallUniversalProc(theirStdRgn, StdRgnProcInfo, verb, rgn);
}

static void myStdBits(const BitMap *srcBits, const Rect *srcRect, const Rect *dstRect, short mode, RgnHandle maskRgn) {
	lprintf("StdBits\n");
	dumpPort();
	CallUniversalProc(theirStdBits, StdBitsProcInfo, srcBits, srcRect, dstRect, mode, maskRgn);
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
