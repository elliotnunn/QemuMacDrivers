#include <LowMem.h>
#include <MixedMode.h>

#include "extfs.h"

static RoutineDescriptor hookDesc = BUILD_ROUTINE_DESCRIPTOR(
	kRegisterBased
	| REGISTER_ROUTINE_PARAMETER(1, kRegisterA0, kFourByteCode)
	| REGISTER_ROUTINE_PARAMETER(2, kRegisterD0, kFourByteCode)
	| REGISTER_RESULT_LOCATION(kRegisterD0),
	ExtFS);

// D0 contains an HFSDispatch selector or extFSErr
// We must pass this D0 through to the next filesystem

static char patch68k[] = {
	0x48, 0xe7, 0xc0, 0xe0,             // 00       movem.l d0-d1/a0-a2,-(sp)
	0x4e, 0xb9, 0x88, 0x88, 0x88, 0x88, // 04       jsr     hookDesc
	0x0c, 0x40, 0xff, 0xc6,             // 0a       cmp.w   #extFSErr,d0
	0x67, 0x08,                         // 0e       beq.s   next
	0x58, 0x4f,                         // 10       addq    #4,sp
	0x4c, 0xdf, 0x07, 0x02,             // 12       movem.l (sp)+,d1/a0-a2
	0x4e, 0x75,                         // 16       rts
	0x4c, 0xdf, 0x07, 0x03,             // 18 next: movem.l (sp)+,d0-d1/a0-a2
	0x4e, 0xf9, 0x88, 0x88, 0x88, 0x88, // 1c       jmp     next
};

void InstallExtFS(void) {
	*(void **)(patch68k + 6) = &hookDesc;

	if ((long)LMGetToExtFS() <= 0) {
		*(short *)(patch68k + 0x1c) = 0x4e75; // change jmp to rts
	} else {
		*(void **)(patch68k + 0x1d) = LMGetToExtFS();
	}

	// Clear 68k emulator's instruction cache
	BlockMove(patch68k, patch68k, sizeof(patch68k));
	BlockMove(&hookDesc, &hookDesc, sizeof(hookDesc));

	LMSetToExtFS((UniversalProcPtr)patch68k);
}
