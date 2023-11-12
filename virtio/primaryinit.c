// No global variables allowed and very few Toolbox calls

#include <DriverSynchronization.h>
#include <Slots.h>

#include <stdint.h>

#include "structs-mmio.h"

void primaryinit(struct SEBlock *pb) {
	void *base = (void *)0xf0000000 + ((long)pb->seSlot << 24);

	// Get the device type for each of the 32 virtio devices
	int devtypes[32] = {};
	for (int i=0; i<32; i++) {
		struct virtioMMIO *device = base + 0x200 + 0x200*i;

		if (device->magicValue != 0x74726976) continue;
		SynchronizeIO();
		if (device->version != 2) continue;
		SynchronizeIO();

		devtypes[i] = device->deviceID;
	}

	// For each functional sResource, find a device
	for (int i=128; i<255; i++) {
		struct SpBlock sp = {.spSlot=pb->seSlot, .spID=i};
		if (SGetSRsrc(&sp)) continue; // not an sResource
		if ((sp.spDrvrHW & 0xff00) != 0x5600) continue; // not a virtio sResource

		int whichdev = -1;
		for (int j=0; j<32; j++) {
			if ((sp.spDrvrHW & 0xff) == devtypes[j]) {
				whichdev = j;
				break;
			}
		}

		// Delete an sResource without a device to drive
		if (whichdev == -1) {
			SDeleteSRTRec(&sp);
			continue;
		}

		// Tell the driver which device it will drive
		sp.spIOReserved = whichdev;
		if (SUpdateSRT(&sp)) Debugger();

		// Single sResource per device
		devtypes[whichdev] = 0;
	}

	pb->seStatus = noErr;
}
