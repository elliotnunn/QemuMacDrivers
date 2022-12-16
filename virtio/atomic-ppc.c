// On PowerPC, use CallSecondaryInterruptHandler2
// (must not be called from hardware interrupt!)

// We take liberties with casting function pointers,
// because we know PowerPC's register calling convention.

#include <DriverServices.h>

#include "atomic.h"

void Atomic(void (*func)(void)) {
	CallSecondaryInterruptHandler2((void *)func, NULL, NULL, NULL);
}

void Atomic1(void (*func)(void *), void *arg) {
	CallSecondaryInterruptHandler2((void *)func, NULL, arg, NULL);
}

void Atomic2(void (*func)(void *, void *), void *arg1, void *arg2) {
	CallSecondaryInterruptHandler2((void *)func, NULL, arg1, arg2);
}
