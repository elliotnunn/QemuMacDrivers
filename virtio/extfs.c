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
// Disable the stack sniffer before we switch the stack

static char patch68k[] = {
	0xa9, 0xff,                         // 00       _Debugger
	0x2f, 0x38, 0x01, 0x10,             // 02       move.l  StkLowPt,-(sp)
	0x42, 0xb8, 0x01, 0x10,             // 06       clr.l   StkLowPt
	0x23, 0xcf, 0x88, 0x88, 0x88, 0x88, // 0a       move.l  sp,stack-4
	0x2e, 0x7c, 0x88, 0x88, 0x88, 0x88, // 10       move.l  #stack-4,sp
	0x2f, 0x00,                         // 16       move.l  d0,-(sp)          ; restore d0 only if hook fails
	0x48, 0xe7, 0x60, 0xe0,             // 18       movem.l d1-d2/a0-a2,-(sp)
	0x2f, 0x00,                         // 1c       move.l  d0,-(sp)
	0x2f, 0x08,                         // 1e       move.l  a0,-(sp)
	0x4e, 0xb9, 0x88, 0x88, 0x88, 0x88, // 20       jsr     hook
	0x50, 0x4f,                         // 26       addq    #8,sp
	0x4c, 0xdf, 0x07, 0x06,             // 28       movem.l (sp)+,d1-d2/a0-a2
	0x0c, 0x40, 0xff, 0xc6,             // 2c       cmp.w   #extFSErr,d0
	0x67, 0x0a,                         // 30       beq.s   punt
	0x58, 0x4f,                         // 32       addq    #4,sp             ; hook success: don't restore d0
	0x2e, 0x57,                         // 34       move.l  (sp),sp           ; unswitch stack
	0x21, 0xdf, 0x01, 0x10,             // 36       move.l  (sp)+,StkLowPt
	0x4e, 0x75,                         // 3a       rts
	0x20, 0x1f,                         // 3c punt: move.l  (sp)+,d0          ; hook failure: restore d0 for next FS
	0x2e, 0x57,                         // 3e       move.l  (sp),sp           ; unswitch stack
	0x21, 0xdf, 0x01, 0x10,             // 40       move.l  (sp)+,StkLowPt
	0x4e, 0xf9, 0x88, 0x88, 0x88, 0x88, // 44       jmp     nexthook          ; next FS
};

char stack[12*1024];

void InstallExtFS(void) {
	*(char **)(patch68k + 0x0c) = stack + sizeof(stack) - 4;
	*(char **)(patch68k + 0x12) = stack + sizeof(stack) - 4;

	*(void **)(patch68k + 0x22) = &hookDesc;

	if ((long)LMGetToExtFS() <= 0) {
		*(short *)(patch68k + 0x44) = 0x4e75; // change jmp to rts
	} else {
		*(void **)(patch68k + 0x46) = LMGetToExtFS();
	}

	// Clear 68k emulator's instruction cache
	BlockMove(patch68k, patch68k, sizeof(patch68k));
	BlockMove(&hookDesc, &hookDesc, sizeof(hookDesc));

	LMSetToExtFS((UniversalProcPtr)(patch68k + 2)); // remove the +2 to break
}

long ExtFSStackMax(void) {
	long i;
	for (i=0; i<sizeof(stack); i++) {
		if (stack[i]) break;
	}
	return sizeof(stack) - i;
}
