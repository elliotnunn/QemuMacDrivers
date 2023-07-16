#include <FSM.h>
#include <MixedMode.h>

#include "universalfcb.h"

// Is the Mac OS 9 FCB format in use?
// This can change at early boot, and is cheap to determine anyhow.
static int fcbFormat9(void) {
	short fcbLen = *(short *)0x3f6;
	return fcbLen > 0 && fcbLen != 94;
}

static void *fcbBase(void) {
	unsigned short hi = *(unsigned short *)0x34e;
	unsigned short lo = *(unsigned short *)0x350;
	return (void *)(((unsigned long)hi << 16) | lo);
}

OSErr UnivAllocateFCB(short *fileRefNum, FCBRecPtr *fileCtrlBlockPtr) {
	if (fcbFormat9()) {
		return CallUniversalProc(*(UniversalProcPtr *)0xe90, 0xfe8, 0,
			fileRefNum, fileCtrlBlockPtr);
	} else {
		void *base = fcbBase();
		short len = *(short *)base;
		for (short refnum=2; refnum<len; refnum+=94) {
			if (*(long *)(base + refnum) == 0) {
				*fileRefNum = refnum;
				*fileCtrlBlockPtr = base + refnum;
				return noErr;
			}
		}
		return tmfoErr;
	}
}

OSErr UnivResolveFCB(short fileRefNum, FCBRecPtr *fileCtrlBlockPtr) {
	if (fcbFormat9()) {
		return CallUniversalProc(*(UniversalProcPtr *)0xe90, 0xee8, 5,
			fileRefNum, fileCtrlBlockPtr);
	} else {
		void *base = fcbBase();
		short len = *(short *)base;

		if (fileRefNum < 2 || fileRefNum > len || fileRefNum % 94 != 2)
			return paramErr;

		*fileCtrlBlockPtr = base + fileRefNum;

		return noErr;
	}
}
