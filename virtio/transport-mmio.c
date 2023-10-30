#include "printf.h"

#include <DriverSynchronization.h>

#include "structs-mmio.h"

#include "transport.h"

// Globals declared in transport.h, define here
void *VConfig;
uint16_t VMaxQueues;

volatile struct virtioMMIO *device;

// returns true for OK
bool VInit(void *theDevice) {
	device = theDevice;

	SynchronizeIO();
	if (device->magicValue != 0x74726976) return false;
	SynchronizeIO();
	if (device->version != 2) return false;
	SynchronizeIO();

	// MUST read deviceID
	printf("device id %04x\n", device->deviceID);
	SynchronizeIO();

	printf("status is %04x\n", device->status);
	SynchronizeIO();

	device->status = 0;
	SynchronizeIO();
	while (device->status) {}

	// 1. Reset the device.
	device->status = 0;
	SynchronizeIO();
	while (device->status) {} // wait till 0

	// 2. Set the ACKNOWLEDGE status bit: the guest OS has noticed the device.
	device->status = 1;
	SynchronizeIO();

	// 3. Set the DRIVER status bit: the guest OS knows how to drive the device.
	device->status = 1 | 2;
	SynchronizeIO();

	for (int i=0; i<4; i++) {
		device->deviceFeaturesSel = i;
		SynchronizeIO();

		printf("%08x ", device->deviceFeatures);
	}
	printf("\n");

	// Absolutely require the version 1 "non-legacy" spec
	if (!VGetDevFeature(32)) {
		VFail();
		return false;
	}
	VSetFeature(32, true);


	// install an interrupt handler here???

	return true;
}

// Negotiate features
bool VGetDevFeature(uint32_t number) {
	SynchronizeIO();
	device->deviceFeaturesSel = number / 32;
	SynchronizeIO();
	return (device->deviceFeatures >> (number % 32)) & 1;
}

void VSetFeature(uint32_t number, bool val) {
	uint32_t mask = 1 << (number % 32);
	uint32_t bits;

	SynchronizeIO();
	device->driverFeaturesSel = number / 32;
	SynchronizeIO();

	bits = device->driverFeatures;
	bits = val ? (bits|mask) : (bits&~mask);
	device->driverFeatures = bits;
	SynchronizeIO();
}

bool VFeaturesOK(void) {
	SynchronizeIO();
	device->status = 1 | 2 | 8;
	SynchronizeIO();
	return (device->status & 8) != 0;
}

void VDriverOK(void) {
	SynchronizeIO();
	device->status = 1 | 2 | 4 | 8;
	SynchronizeIO();
}

void VFail(void) {
	SynchronizeIO();
	device->status = 0x80;
	SynchronizeIO();
}

// Tell the device where to find the three (split) virtqueue rings
uint16_t VQueueMaxSize(uint16_t q) {
	SynchronizeIO();
	device->queueSel = q;
	SynchronizeIO();
	return device->queueNumMax;
}

void VQueueSet(uint16_t q, uint16_t size, uint32_t desc, uint32_t avail, uint32_t used) {
	SynchronizeIO();
	device->queueSel = q;
	SynchronizeIO();
	device->queueNum = size;
	device->queueDesc = desc;
	device->queueDriver = avail;
	device->queueDevice = used;
	SynchronizeIO();
	device->queueReady = 1;
	SynchronizeIO();
}

// Tell the device about a change to the avail ring
void VNotify(uint16_t queue) {
	printf("VNotifying...\n");
	SynchronizeIO();
	device->queueNotify = queue;
	SynchronizeIO();
}

// Interrupts need to be explicitly reenabled after a notification
void VRearm(void) {
// not sure what to do here just yet...
}
