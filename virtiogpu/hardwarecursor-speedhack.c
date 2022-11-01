// How quickly can the screen respond to a cursor move?
// Secondary goal: figure out whether JCrsrTask is our caller

#include <DriverServices.h>
#include <PCI.h>
#include <Types.h>
#include <Video.h>
#include <VideoServices.h>

#include <stdint.h>
#include <string.h>

#include "hardwarecursor.h"

#include "lprintf.h"

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

static int cursorX, cursorY;
static int cursorVisible;
static int cursorSet;

// Borrowed from gpu.c
extern int W, H;
extern void *frontbuf;
void gammaRect(short top, short left, short bottom, short right);
void uploadRect(short top, short left, short bottom, short right);

void InitHardwareCursor(void) {
}

// Graphics drivers that support hardware cursors must return true.
// <-- csSupportsHardwareCursor  true if hardware cursor is supported
OSStatus SupportsHardwareCursor(VDSupportsHardwareCursorRec *rec) {
	rec->csSupportsHardwareCursor = 1;
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
OSStatus SetHardwareCursor(VDSetHardwareCursorRec *rec) {
	cursorSet = 1;
	return noErr;
}

// Sets the cursor’s x and y coordinates and visible state. If the cursor
// was successfully set by a previous call to SetHardwareCursor, the driver
// must program the hardware with the given x, y, and visible parameters
// and then return noErr. If the cursor was not successfully set by the
// last SetHardwareCursor call, the driver must return controlErr.
// --> csCursorX           X coordinate
// --> csCursorY           Y coordinate
// --> csCursorVisible     true if the cursor must be visible
OSStatus DrawHardwareCursor(VDDrawHardwareCursorRec *rec) {
	int top, bottom, left, right;

	//lprintf("DrawHardwareCursor vis=%d loc=%d,%d\n", rec->csCursorVisible, rec->csCursorX, rec->csCursorY);

	if (cursorVisible) {
		top = cursorY;
		bottom = cursorY + 16;
		left = cursorX;
		right = cursorX + 16;
		
		top = MAX(top, 0);
		bottom = MIN(bottom, H);
		left = MAX(left, 0);
		right = MIN(right, W);

		gammaRect(top, left, bottom, right);
		uploadRect(top, left, bottom, right);
	}

	cursorX = rec->csCursorX;
	cursorY = rec->csCursorY;
	cursorVisible = rec->csCursorVisible;

	if (rec->csCursorVisible) {
		int x, y;

		top = cursorY;
		bottom = cursorY + 16;
		left = cursorX;
		right = cursorX + 16;
		
		top = MAX(top, 0);
		bottom = MIN(bottom, H);
		left = MAX(left, 0);
		right = MIN(right, W);

		for (y=top; y<bottom; y++) {
			uint32_t *dest = (void *)((char *)frontbuf + y * W * 4 + left * 4);
			for (x=left; x<right; x++) {
				*dest++ = 0;
			}
		}

		uploadRect(top, left, bottom, right);
	}

	return noErr;
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
OSStatus GetHardwareCursorDrawState(VDHardwareCursorDrawStateRec *rec) {
	rec->csCursorX = cursorX;
	rec->csCursorY = cursorY;
	rec->csCursorVisible = cursorVisible;
	rec->csCursorSet = cursorSet;
	return noErr;
}
