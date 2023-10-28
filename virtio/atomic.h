// Header-only library

#pragma once

#if GENERATINGCFM

// In the NDRV runtime use CallSecondaryInterruptHandler2
// (must not be called from hardware interrupt!)

// We take liberties with casting function pointers,
// because we know PowerPC's register calling convention.

#include <DriverServices.h>

#define ATOMIC(func) CallSecondaryInterruptHandler2((void *)(func), NULL, NULL, NULL)
#define ATOMIC1(func, a1) CallSecondaryInterruptHandler2((void *)(func), NULL, (void *)(long)(a1), NULL)
#define ATOMIC2(func, a1, a2) CallSecondaryInterruptHandler2((void *)(func), NULL, (void *)(long)(a1), (void *)(long)(a2))

#else

// In the DRVR runtime fiddle the SR interrupt mask and call the function directly

// Better to turn these into modern GCC inline assembly
#pragma parameter __D0 IntsOff
short IntsOff(void) = {0x40c0, 0x007c, 0x0700}; // save sr and mask

#pragma parameter IntsOn(__D0)
void IntsOn(short) = {0x46c0};

#define ATOMIC(func) {short _sr_ = IntsOff(); func(); IntsOn(_sr_);}
#define ATOMIC1(func, a1) {short _sr_ = IntsOff(); func(a1); IntsOn(_sr_);}
#define ATOMIC2(func, a1, a2) {short _sr_ = IntsOff(); func(a1, a2); IntsOn(_sr_);}

#endif
