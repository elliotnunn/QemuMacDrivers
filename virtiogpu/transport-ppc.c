#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <Devices.h>
#include <DriverServices.h>
#include <NameRegistry.h>
#include <PCI.h>

#include "allocator.h"
#include "byteswap.h"
#include "device.h"
#include "pci-structs.h"
#include "virtqueue.h"

#include "transport.h"

// Globals declared in transport.h, define here
void *VConfig;
uint16_t VMaxQueues;

// Internal globals
static struct virtio_pci_common_cfg *gCommonConfig;
static uint16_t *gNotify;
static uint32_t gNotifyMultiplier;
static uint8_t *gISRStatus;
static bool gSuppressNotification;

// Internal routines
static InterruptMemberNumber interruptTopHalf(InterruptSetMember ist, void *refCon, uint32_t intCount);
static OSStatus queueIntBottomHalf(void *arg1, void *arg2);
static OSStatus configIntBottomHalf(void *arg1, void *arg2);
static void findLogicalBARs(RegEntryID *pciDevice, void *barArray[6]);

// For PCI devices, the void pointer is a RegEntryIDPtr.
// Leave the device in DRIVER status.
bool VInit(void *dev) {
	void *bars[6];
	uint8_t cap_offset;
	uint16_t pci_status = 0;

	findLogicalBARs(dev, bars);

	// PCI configuration structures point to addresses we need within the BARs
	for (ExpMgrConfigReadByte(dev, (LogicalAddress)0x34, &cap_offset);
		cap_offset != 0;
		ExpMgrConfigReadByte(dev, (LogicalAddress)(cap_offset+1), &cap_offset)) {

		uint8_t cap_vndr, cfg_type, bar;
		uint32_t offset;
		void *address;

		// vendor-specific capability struct, i.e. a "VIRTIO_*" one
		ExpMgrConfigReadByte(dev, (LogicalAddress)cap_offset, &cap_vndr);
		if (cap_vndr != 9) continue;

		ExpMgrConfigReadByte(dev, (LogicalAddress)(cap_offset+3), &cfg_type);
		ExpMgrConfigReadByte(dev, (LogicalAddress)(cap_offset+4), &bar);
		ExpMgrConfigReadLong(dev, (LogicalAddress)(cap_offset+8), &offset);
		address = (char *)bars[bar] + offset;

		if (cfg_type == 1 && !gCommonConfig) {
			gCommonConfig = address;
		} else if (cfg_type == 2 && !gNotify) {
			gNotify = address;
			ExpMgrConfigReadLong(dev, (LogicalAddress)(cap_offset+16), &gNotifyMultiplier);
		} else if (cfg_type == 3 && !gISRStatus) {
			gISRStatus = address;
		} else if (cfg_type == 4 && !VConfig) {
			VConfig = address;
		}
	}

	if (!gCommonConfig || !gNotify || !gISRStatus || !VConfig) return false;

	// Incantation to enable memory-mapped access
	ExpMgrConfigReadWord(dev, (LogicalAddress)4, &pci_status);
	pci_status |= 2;
	ExpMgrConfigWriteWord(dev, (LogicalAddress)4, pci_status);

	VMaxQueues = GETLE16(gCommonConfig->le_num_queues);

	// 1. Reset the device.
	gCommonConfig->device_status = 0;
	SynchronizeIO();
	while (*(volatile char *)&gCommonConfig->device_status) {} // wait till 0

	// 2. Set the ACKNOWLEDGE status bit: the guest OS has noticed the device.
	gCommonConfig->device_status = 1;
	SynchronizeIO();

	// 3. Set the DRIVER status bit: the guest OS knows how to drive the device.
	gCommonConfig->device_status = 1 | 2;
	SynchronizeIO();

	// Install interrupt handler
	{
		void *oldRefCon;
		InterruptHandler oldHandler;
		InterruptEnabler enabler;
		InterruptDisabler disabler;
		struct InterruptSetMember ist = {0};
		RegPropertyValueSize size = sizeof(ist);

		RegistryPropertyGet(dev, "driver-ist", (void *)ist, &size);
		GetInterruptFunctions(ist.setID, ist.member, &oldRefCon, &oldHandler, &enabler, &disabler);
		InstallInterruptFunctions(ist.setID, ist.member, NULL, interruptTopHalf, NULL, NULL);

		enabler(ist, oldRefCon);
	}

	return true;
}

bool VGetDevFeature(uint32_t number) {
	SETLE32(gCommonConfig->le_device_feature_select, number / 32);
	SynchronizeIO();

	return (GETLE32(gCommonConfig->le_device_feature) >> (number % 32)) & 1;
}

void VSetFeature(uint32_t number, bool val) {
	uint32_t mask = 1 << (number % 32);
	uint32_t bits;

	SETLE32(gCommonConfig->le_driver_feature_select, number / 32);
	SynchronizeIO();

	bits = GETLE32(gCommonConfig->le_driver_feature);
	bits = val ? (bits|mask) : (bits&~mask);
	SETLE32(gCommonConfig->le_driver_feature, bits);

	SynchronizeIO();
}

bool VFeaturesOK(void) {
	// Absolutely require the version 1 "non-legacy" spec
	VSetFeature(32, true);

	gCommonConfig->device_status = 1 | 2 | 8;
	SynchronizeIO();

	return (gCommonConfig->device_status & 8) != 0;
}

void VFail(void) {
	gCommonConfig->device_status = 0x80;
	SynchronizeIO();
}

uint16_t VQueueMaxSize(uint16_t q) {
	SETLE16(gCommonConfig->le_queue_select, q);
	SynchronizeIO();
	return GETLE16(gCommonConfig->le_queue_size);
}

void VQueueSet(uint16_t q, uint16_t size, uint32_t desc, uint32_t avail, uint32_t used) {
	SETLE16(gCommonConfig->le_queue_select, q);
	SynchronizeIO();
	SETLE16(gCommonConfig->le_queue_size, size);
	SETLE32(gCommonConfig->le_queue_desc, desc);
	SETLE32(gCommonConfig->le_queue_driver, avail);
	SETLE32(gCommonConfig->le_queue_device, used);
	SynchronizeIO();
	SETLE16(gCommonConfig->le_queue_enable, 1);
	SynchronizeIO();
}

void VNotify(uint16_t queue) {
	SETLE16(gNotify[gNotifyMultiplier * queue / 2], queue);
	SynchronizeIO();
}

void VRearm(void) {
	gSuppressNotification = false;
}

static InterruptMemberNumber interruptTopHalf(InterruptSetMember ist, void *refCon, uint32_t intCount) {
	uint8_t flags = *gISRStatus; // read flags and also deassert the interrupt

	(void)ist; (void)refCon; (void)intCount;

	if ((flags & 1) && !gSuppressNotification) {
		QDisarm();
		gSuppressNotification = true;
		QueueSecondaryInterruptHandler(queueIntBottomHalf, NULL, NULL, NULL);
	}

	if (flags & 2) {
		QueueSecondaryInterruptHandler(configIntBottomHalf, NULL, NULL, NULL);
	}

	return noErr;
}

static OSStatus queueIntBottomHalf(void *arg1, void *arg2) {
	(void)arg1; (void)arg2;
	QNotified();
	return noErr;
}

static OSStatus configIntBottomHalf(void *arg1, void *arg2) {
	(void)arg1; (void)arg2;
	DConfigChange();
	return noErr;
}

// Open Firmware and Mac OS have already assigned and mapped the BARs
// Just need to find where
static void findLogicalBARs(RegEntryID *pciDevice, void *barArray[6]) {
	#define MAXADDRS 10
	uint32_t assignAddrs[5*MAXADDRS] = {0};
	void *applAddrs[MAXADDRS] = {0};
	RegPropertyValueSize size;
	int i;

	for (i=0; i<6; i++) barArray[i] = NULL;

	size = sizeof(assignAddrs);
	RegistryPropertyGet(pciDevice, "assigned-addresses", (void *)assignAddrs, &size);

	size = sizeof(applAddrs);
	RegistryPropertyGet(pciDevice, "AAPL,address", applAddrs, &size);

	for (i=0; i<MAXADDRS; i++) {
		uint8_t bar;

		// Only interested in PCI 32 or 64-bit memory space
		if (((assignAddrs[i*5] >> 24) & 3) < 2) continue;

		// This is the offset of a BAR within PCI config space (0x10, 0x14...)
		bar = assignAddrs[i*5];

		// Convert to a BAR number (0-5)
		if (bar % 4) continue;
		bar = (bar - 0x10) / 4;
		if (bar > 5) continue;

		// The base logical address is the i'th element of AAPL,address
		barArray[bar] = applAddrs[i];
	}
}
