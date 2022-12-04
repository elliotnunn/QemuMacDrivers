// Wait for NewGestalt of the 'os  ' selector,
// which the Process Manager calls very late in the boot process.

#include <Memory.h>
#include <MixedMode.h>
#include <Patches.h>
#include <Traps.h>

#include "lateboothook.h"

#define TRAPNUM 0xa0ad // Gestalt traps
#define GETTRAP() GetOSTrapAddress(TRAPNUM)
#define SETTRAP(addr) SetOSTrapAddress(addr, TRAPNUM)

static void hook(unsigned short trap, unsigned long ostype);

static void *oldTrap;
static RoutineDescriptor hookDesc = BUILD_ROUTINE_DESCRIPTOR(kRegisterBased
	| REGISTER_ROUTINE_PARAMETER(1, kRegisterD1, kTwoByteCode)
	| REGISTER_ROUTINE_PARAMETER(2, kRegisterD0, kFourByteCode), hook);

static char patch68k[] = {
	0x48, 0xe7, 0xe0, 0xe0,             // movem.l d0-d2/a0-a2,-(sp)
	0x4e, 0xb9, 0xff, 0xff, 0xff, 0xff, // jsr     <callback>
	0x4c, 0xdf, 0x07, 0x07,             // movem.l (sp)+,d0-d2/a0-a2
	0x4e, 0xf9, 0xff, 0xff, 0xff, 0xff  // jmp     <original>
};

void InstallLateBootHook(void) {
	*(void **)(patch68k + 6) = &hookDesc;
	*(void **)(patch68k + 16) = oldTrap = GETTRAP();

	// Clear 68k emulator's instruction cache
	BlockMove(patch68k, patch68k, sizeof(patch68k));
	BlockMove(&hookDesc, &hookDesc, sizeof(hookDesc));

	SETTRAP((void *)patch68k);
}

static void hook(unsigned short trap, unsigned long ostype) {
	static int already;

	if (already) return;

	// The 0x600 flag bits distinguish Gestalt, NewGestalt etc
	if ((trap & 0x600) == 0x200 && ostype == 'os  ') {
		// Disable this patch
		already = 1;

		// Try even harder to disable this patch
		if ((void *)GETTRAP() == (void *)patch68k) {
			SETTRAP(oldTrap);
		}

		LateBootHook();
	}
}
