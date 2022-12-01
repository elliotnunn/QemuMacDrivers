// Implemented by transport-[ppc|drvr].c

#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The void pointer somehow identifies the PCI or MMIO device
// returns true for OK
bool VInit(void *dev);

// Sets these globals
extern void *VConfig;
extern uint16_t VMaxQueues;

// Negotiate features
bool VGetDevFeature(uint32_t number);
void VSetFeature(uint32_t number, bool val);
bool VFeaturesOK(void);
void VDriverOK(void);
void VFail(void);

// Tell the device where to find the three (split) virtqueue rings
uint16_t VQueueMaxSize(uint16_t q);
void VQueueSet(uint16_t q, uint32_t desc, uint32_t avail, uint32_t used);

// Tell the device about a change to the avail ring
void VNotify(uint16_t queue);

// Interrupts need to be explicitly reenabled after a notification
void VRearm(void);

#endif
