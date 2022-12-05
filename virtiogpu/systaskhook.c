// An alternative to accRun time that works with ndrv

#include <MixedMode.h>
#include <Patches.h>
#include <Traps.h>

#include "systaskhook.h"

#define TRAPNUM _SystemTask
#define GETTRAP() GetToolTrapAddress(TRAPNUM)
#define SETTRAP(addr) SetToolTrapAddress(addr, TRAPNUM)

static void hook(void);

static RoutineDescriptor hookDesc = BUILD_ROUTINE_DESCRIPTOR(0, hook);
static UniversalProcPtr oldTrap;

void InstallSysTaskHook(void) {
	oldTrap = GETTRAP();
	SETTRAP(&hookDesc);
}

static void hook(void) {
	SysTaskHook();
	CallUniversalProc(oldTrap, 0);
}
