#pragma once

// Subtle: on PowerPC, waiting for a PowerPC interrupt will work
// even if interrupts are masked in the 68k emulator.
// So call me in a tight loop with VPoll.
void WaitForInterrupt(void);
