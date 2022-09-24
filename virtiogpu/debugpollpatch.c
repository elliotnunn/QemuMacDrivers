// Get a callback when MacsBug attempts to poll the keyboard

#include "debugpollpatch.h"

#include <Memory.h>
#include <MixedMode.h>
#include <Patches.h>
#include <Types.h>

#include "lprintf.h"

// If changing this code, also change the edit-offsets
static unsigned char patch[] = {
	0x0c, 0x80, 0x00, 0x00, 0x00, 0x04, //      cmp.l   #4,d0
	0xff, 0x0e,                         //      bne.s   old
	0x48, 0xe7, 0xe0, 0xe0,             //      movem.l d0-d2/a0-a2,-(sp)
	0x4e, 0xb9, 0xff, 0xff, 0xff, 0xff, //      jsr     <callback>
	0x4c, 0xdf, 0x07, 0x07,             //      movem.l (sp)+,d0-d2/a0-a2
	0x4e, 0xf9, 0xff, 0xff, 0xff, 0xff  // old: jmp     <original DebugUtil>
};

static RoutineDescriptor callbackDesc = BUILD_ROUTINE_DESCRIPTOR(0, NULL);

void InstallDebugPollPatch(void (*callback)(void)) {
	callbackDesc.routineRecords[0].procDescriptor = (void *)callback;

	*(void **)(patch + 14) = &callbackDesc;
	*(void **)(patch + 24) = GetOSTrapAddress(0xa08d);

	// Clear 68k emulator's instruction cache
	BlockMove(patch, patch, sizeof(patch));
	BlockMove(&callbackDesc, &callbackDesc, sizeof(callbackDesc));

	SetOSTrapAddress((void *)patch, 0xa08d);
}
