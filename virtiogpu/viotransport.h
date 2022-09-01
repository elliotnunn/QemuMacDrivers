// Header for viotransport-*.c
// Names prefixed VT

#ifndef _VIOTRANSPORT_H_
#define _VIOTRANSPORT_H_

#include <stdint.h>
#include <stddef.h>

extern void *VTDeviceConfig;

// Buffers alternate between output (even) and input (odd).
// Each output buffer n is already chained to the input buffer n+1.
extern uint16_t VTNumQueues;
extern void ***VTBuffers; // VTBuffers[queue][buffer]

// On PowerPC: pointer to RegEntryID of PCI device
// On 68k: TODO
// Return nonzero on failure
OSStatus VTInit(void *dev,
	void (*VTQueueSizer)(uint16_t queue, uint16_t *count, size_t *osize, size_t *isize),
	void (*VTRecv)(uint16_t queue, uint16_t buffer, size_t len),
	void (*VTConfigChanged)(void));

// Pass in the index of the output buffer (the input buffer is already chained).
void VTSend(uint16_t queue, uint16_t buffer);

#endif
