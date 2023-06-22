#include <LowMem.h>
#include <MixedMode.h>

#include "extfs.h"

static RoutineDescriptor hookDesc = BUILD_ROUTINE_DESCRIPTOR(
	kCStackBased
	| STACK_ROUTINE_PARAMETER(1, kFourByteCode)
	| STACK_ROUTINE_PARAMETER(2, kFourByteCode)
	| RESULT_SIZE(kFourByteCode),
	ExtFS);

// D0 contains an HFSDispatch selector or extFSErr
// We must pass this D0 through to the next filesystem

static char patch68k[] = {
	0xa9, 0xff,                         // 00       _Debugger
	0x48, 0xe7, 0xc0, 0xe0,             // 02       movem.l d0-d1/a0-a2,-(sp)
	0x2f, 0x00,                         // 06       move.l  d0,-(sp)
	0x2f, 0x08,                         // 08       move.l  a0,-(sp)
	0x4e, 0xb9, 0x88, 0x88, 0x88, 0x88, // 0a       jsr     hook
	0x50, 0x4f,                         // 10       addq    #8,sp
	0x0c, 0x40, 0xff, 0xc6,             // 12       cmp.w   #extFSErr,d0
	0x67, 0x08,                         // 16       beq.s   punt
	0x58, 0x4f,                         // 18       addq    #4,sp
	0x4c, 0xdf, 0x07, 0x02,             // 1a       movem.l (sp)+,d1/a0-a2
	0x4e, 0x75,                         // 1e       rts
	0x4c, 0xdf, 0x07, 0x03,             // 20 punt: movem.l (sp)+,d0-d1/a0-a2
	0x4e, 0xf9, 0x88, 0x88, 0x88, 0x88, // 24       jmp     nexthook
};

void InstallExtFS(void) {
	*(void **)(patch68k + 0x0c) = &hookDesc;

	if ((long)LMGetToExtFS() <= 0) {
		*(short *)(patch68k + 0x24) = 0x4e75; // change jmp to rts
	} else {
		*(void **)(patch68k + 0x26) = LMGetToExtFS();
	}

	// Clear 68k emulator's instruction cache
	BlockMove(patch68k, patch68k, sizeof(patch68k));
	BlockMove(&hookDesc, &hookDesc, sizeof(hookDesc));

	LMSetToExtFS((UniversalProcPtr)(patch68k + 2)); // remove the +2 to break
}
