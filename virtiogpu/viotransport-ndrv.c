#include <stddef.h>
#include <stdint.h>
#include <Devices.h>
#include <DriverServices.h>
#include <NameRegistry.h>
#include <PCI.h>

#include "viotransport.h"
#include "viotransport-structs.h"

#define LPRINTF_ENABLE 1 // comment out for release

#if LPRINTF_ENABLE
#include "lprintf.h"
#else
#define lprintf
#endif

// Globals declared in viotransport.h, define here
uint16_t VTNumQueues;
void ***VTBuffers;
void *VTDeviceConfig;

// Internal structure to keep track of virtqueues
struct virtq {
	uint16_t count;
	uint16_t lastUsed;
	size_t isize, osize; // size of in
	struct virtq_desc *desc; // only one of these
	struct virtq_avail *avail; // "count" of these
	struct virtq_used *used; // "count" of these
};

// Internal globals
static struct virtq *gQueues;
static uint16_t *gNotify;
static uint32_t gNotifyMultiplier;
static uint8_t *gISRStatus;
static void (*gQueueRecv)(uint16_t queue, uint16_t buffer, size_t len);
static void (*gConfigChanged)(void);

static void panic(char *msg) {
	lprintf("panic: %s\n", msg);
	*(char *)0xdeadbeefUL = 0;
}

// Internal routines.
static void initQueues(struct virtio_pci_common_cfg *comConf,
	void (*queueSizer)(uint16_t queue, uint16_t *count, size_t *isize, size_t *osize));
static void readCapabilities(RegEntryID *dev,
	void **commonConf, void **notify, uint32_t *notifyMult, void **isrStatus, void **devConf);
static void findLogicalBARs(RegEntryID *pciDevice, void *barArray[6]);
static void allocPg(size_t count, void **logicals, uint32_t *physicals, int isIn, int isOut);
OSStatus enqueueBH(void *queueU16, void *bufferU16);
static InterruptMemberNumber interruptTH(InterruptSetMember ist, void *refCon, uint32_t intCount);
static OSStatus configInterruptBH(void *arg1, void *arg2);
static OSStatus queueInterruptBH(void *arg1, void *arg2);
static void readQueues(void);

OSStatus VTInit(void *dev,
	void (*queueSizer)(uint16_t queue, uint16_t *count, size_t *isize, size_t *osize),
	void (*queueRecv)(uint16_t queue, uint16_t buffer, size_t len),
	void (*configChanged)(void)) {
	struct virtio_pci_common_cfg *comConf = NULL;
	OSStatus err;

	// Get logical pointers into the BARs
	readCapabilities(dev,
		(void **)&comConf,
		(void **)&gNotify, &gNotifyMultiplier,
		(void **)&gISRStatus,
		&VTDeviceConfig);

	if (!comConf || !gNotify || !gISRStatus || !VTDeviceConfig) return paramErr;

	// Enable access to PCI memory space
	{
		uint16_t status = 0;
		err = ExpMgrConfigReadWord(dev, (LogicalAddress)4, &status);
		if (err) return err;
		status |= 2;
		err = ExpMgrConfigWriteWord(dev, (LogicalAddress)4, status);
		if (err) return err;
	}

	// 1. Reset the device.
	SynchronizeIO();

	comConf->device_status = 0;
	SynchronizeIO();

	// 2. Set the ACKNOWLEDGE status bit: the guest OS has noticed the device.
	comConf->device_status = 1;
	SynchronizeIO();

	// 3. Set the DRIVER status bit: the guest OS knows how to drive the device.
	comConf->device_status = 1 | 2;
	SynchronizeIO();

	// 4. Read device feature bits, and write the subset of feature bits understood by
	//    the OS and driver to the device. During this step the driver MAY read (but
	//    MUST NOT write) the device-specific configuration fields to check that it can
	//    support the device before accepting it.

	// Absolutely require VIRTIO_F_VERSION_1
	comConf->le_device_feature_select = EndianSwap32Bit(1);
	SynchronizeIO();
	if ((comConf->le_device_feature & EndianSwap32Bit(1)) == 0) return paramErr;

	// Declare support for VIRTIO_F_VERSION_1
	comConf->le_driver_feature_select = EndianSwap32Bit(1);
	SynchronizeIO();
	comConf->le_driver_feature = EndianSwap32Bit(1);
	SynchronizeIO();

	// But no other device-specific features
	comConf->le_driver_feature_select = 0; // EASY -- no features supported yet!
	SynchronizeIO();
	comConf->le_driver_feature = 0;
	SynchronizeIO();

	// 5. Set the FEATURES_OK status bit. The driver MUST NOT accept new feature bits
	//    after this step.
	comConf->device_status = 1 | 2 | 8;
	SynchronizeIO();

	// 6. Re-read device status to ensure the FEATURES_OK bit is still set: otherwise,
	//    the device does not support our subset of features and the device is unusable.
	if ((comConf->device_status & 8) == 0) return paramErr;

	// 7. Perform device-specific setup, including discovery of virtqueues for the
	//    device, optional per-bus setup, reading and possibly writing the device's
	//    virtio configuration space, and population of virtqueues.
	initQueues(comConf, queueSizer);

	// Install interrupt handler ("The device MUST NOT consume buffers or
	// send any used buffer notifications to the driver before DRIVER_OK)"
	{
		void *oldRefCon;
		InterruptHandler oldHandler;
		InterruptEnabler enabler;
		InterruptDisabler disabler;

		struct InterruptSetMember ist = {0};
		RegPropertyValueSize size = sizeof(ist);

		err = RegistryPropertyGet(dev, "driver-ist", (void *)ist, &size);
		if (err) return err;

		err = GetInterruptFunctions(ist.setID, ist.member,
			&oldRefCon, &oldHandler, &enabler, &disabler);
		if (err) return err;

		err = InstallInterruptFunctions(ist.setID, ist.member,
			NULL, interruptTH, NULL, NULL);
		if (err) return err;

		enabler(ist, oldRefCon);

		gQueueRecv = queueRecv;
		gConfigChanged = configChanged;
	}

	// 8. Set the DRIVER_OK status bit. At this point the device is "live".
	comConf->device_status = 1 | 2 | 8 | 4;
	SynchronizeIO();

	return noErr;
}

static void initQueues(struct virtio_pci_common_cfg *comConf,
	void (*queueSizer)(uint16_t queue, uint16_t *count, size_t *osize, size_t *isize)) {
	uint16_t q;

	VTNumQueues = EndianSwap16Bit(comConf->le_num_queues);

	// Need to keep track of the logical address of each buffer
	gQueues = PoolAllocateResident(VTNumQueues * sizeof(*gQueues), true);
	VTBuffers = PoolAllocateResident(VTNumQueues * sizeof(*VTBuffers), false);

	for (q=0; q<VTNumQueues; q++) {
		// Output pages: descriptor ring x 1, avail ring x 1, buffers x (n/2)
		// Input pages: used ring x 1, buffers x (n/2)
		// Maximum n=256 buffers so that descriptor ring fits in one page

		size_t opagecnt, ipagecnt;
		void *opages[1+1+128], *ipages[1+128];
		uint32_t opagesp[1+1+128], ipagesp[1+128];
		uint16_t buf;

		comConf->le_queue_select = EndianSwap16Bit(q);
		SynchronizeIO();

		// Negotiate queue count and buffer sizes
		gQueues[q].osize = gQueues[q].isize = 4096;
		gQueues[q].count = EndianSwap16Bit(comConf->le_queue_size);
		if (gQueues[q].count > 256) gQueues[q].count = 256; // fit desc ring in a page
		queueSizer(q, &gQueues[q].count, &gQueues[q].osize, &gQueues[q].isize);
		if (gQueues[q].count == 0) continue;
		comConf->le_queue_size = EndianSwap16Bit(gQueues[q].count);

		// Allocate the memory with minimal waste
		opagecnt = 1 + 1 + (gQueues[q].osize * gQueues[q].count / 2 + 0xfff) / 0x1000;
		ipagecnt = 1 + (gQueues[q].isize * gQueues[q].count / 2 + 0xfff) / 0x1000;
		lprintf("Allocating pages for virtq %d: %d output, %d input\n", q, opagecnt, ipagecnt);
		allocPg(opagecnt, opages, opagesp, 0, 1);
		allocPg(ipagecnt, ipages, ipagesp, 1, 0);

		// Save pointers to our allocations
		comConf->le_queue_desc = EndianSwap32Bit(opagesp[0]);
		gQueues[q].desc = opages[0];
		comConf->le_queue_driver = EndianSwap32Bit(opagesp[1]);
		gQueues[q].avail = opages[1];
		comConf->le_queue_device = EndianSwap32Bit(ipagesp[0]);
		gQueues[q].used = ipages[0];

		// Populate our own array of logical buffer ptrs
		VTBuffers[q] = PoolAllocateResident(gQueues[q].count * sizeof(*VTBuffers[q]), false);

		// Populate the descriptor ring
		for (buf=0; buf<gQueues[q].count; buf++) {
			void *logical;
			uint32_t physical;
			uint16_t flags, next;
			if (buf % 2 == 0) { // output page
				int pageIndex = 1 + 1 + gQueues[q].osize * (buf / 2) / 0x1000;
				int pageOffset = gQueues[q].osize * (buf / 2) % 0x1000;
				logical = (char *)opages[pageIndex] + pageOffset;
				physical = (uint32_t)((char *)opagesp[pageIndex] + pageOffset);

				flags = 1; // VIRTQ_DESC_F_NEXT
				next = buf + 1;
			} else { // input page
				int pageIndex = 1 + gQueues[q].isize * (buf / 2) / 0x1000;
				int pageOffset = gQueues[q].isize * (buf / 2) % 0x1000;
				logical = (char *)ipages[pageIndex] + pageOffset;
				physical = (uint32_t)((char *)ipagesp[pageIndex] + pageOffset);

				flags = 2; // VIRTQ_DESC_F_WRITE i.e. device write-only
				next = 0;
			}

 			VTBuffers[q][buf] = logical;
 			gQueues[q].desc[buf].le_addr = EndianSwap32Bit(physical);
 			gQueues[q].desc[buf].le_len = EndianSwap32Bit((buf % 2 == 0) ? gQueues[q].osize : gQueues[q].isize);
			gQueues[q].desc[buf].le_flags = EndianSwap16Bit(flags);
			gQueues[q].desc[buf].le_next = EndianSwap16Bit(next);
		}

		SynchronizeIO();
		comConf->le_queue_enable = EndianSwap16Bit(1);
		SynchronizeIO();
	}

#ifdef LPRINTF_ENABLE
	{
		uint16_t i, j;
		for (i=0; i<VTNumQueues; i++) {
			comConf->le_queue_select = EndianSwap16Bit(i);
			SynchronizeIO();

			lprintf("virtq %d:\n", i);

			lprintf("  %d buffers (half x %d bytes for output, half x %d bytes for input)\n",
				gQueues[i].count, gQueues[i].osize, gQueues[i].isize);

			lprintf("  Avail ring at %#010x(log) %#010x(phys)\n",
				gQueues[i].avail, EndianSwap32Bit(comConf->le_queue_driver));
			lprintf("    {flags:%#06x idx:%#06x}\n",
				EndianSwap16Bit(gQueues[i].avail->le_flags), EndianSwap16Bit(gQueues[i].avail->le_idx));

			lprintf("  Used ring at %#010x(log) %#010x(phys)\n",
				gQueues[i].used, EndianSwap32Bit(comConf->le_queue_device));
			lprintf("    {flags:%#06x idx:%#06x}\n",
				EndianSwap16Bit(gQueues[i].used->le_flags), EndianSwap16Bit(gQueues[i].used->le_idx));

			lprintf("  Descriptor ring at %#010x(log) %#010x(phys)\n",
				gQueues[i].desc, EndianSwap32Bit(comConf->le_queue_desc));

			for (j=0; j<gQueues[i].count; j++) {
				lprintf("    [%#06x] logical:%#010x {addr:%#010x len:%#06x flags:%#06x next:%#06x}\n",
					j,
					VTBuffers[i][j],
					EndianSwap32Bit(gQueues[i].desc[j].le_addr),
					EndianSwap32Bit(gQueues[i].desc[j].le_len),
					EndianSwap16Bit(gQueues[i].desc[j].le_flags),
					EndianSwap16Bit(gQueues[i].desc[j].le_next));
			}

			lprintf("\n");
		}
	}
#endif
}

// Iterate over the PCI "capability" structures in config space
static void readCapabilities(RegEntryID *dev,
	void **commonConf, void **notify, uint32_t *notifyMult, void **isrStatus, void **devConf) {
	void *bars[6];
	uint8_t capptr = 0;
	int want = 0x1e; // want 1,2,3,4

	findLogicalBARs(dev, bars);
	ExpMgrConfigReadByte(dev, (void *)0x34, &capptr);

	while (capptr != 0) {
		uint32_t cap;
		uint8_t cap_vndr, cap_next, cap_len, cfg_type;

		ExpMgrConfigReadLong(dev, (void *)capptr, &cap);
		cap_vndr = cap;
		cap_next = cap >> 8;
		cap_len = cap >> 16;
		cfg_type = cap >> 24;

		// Vendor-specific capability? Specifically one named VIRTIO_PCI_CAP_*?
		if (cap_vndr == 9 && cap_len >= 16 && (want & (1 << cfg_type))) {
			uint8_t bar;
			uint32_t offset;
			void *address;

			want &= ~(1 << cfg_type);

			ExpMgrConfigReadByte(dev, (void *)(capptr + 4), &bar);
			ExpMgrConfigReadLong(dev, (void *)(capptr + 8), &offset);
			address = (char *)bars[bar] + offset;

			if (cfg_type == 1) { // VIRTIO_PCI_CAP_COMMON_CFG
				*commonConf = address;
			} else if (cfg_type == 2) { // VIRTIO_PCI_CAP_NOTIFY_CFG
				*notify = address;
				ExpMgrConfigReadLong(dev, (void *)(capptr + 16), notifyMult);
			} else if (cfg_type == 3) { // VIRTIO_PCI_CAP_ISR_CFG
				*isrStatus = address;
			} else if (cfg_type == 4) { // VIRTIO_PCI_CAP_DEVICE_CFG
				*devConf = address;
			}
		}

		capptr = cap_next;
	}
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

// Allocate and cache-inhibit n pages (0x1000 bytes each)
// Contiguous logical memory is easy to find, but MacOS is reluctant to
// promise contiguous physical memory (although it seems to deliver?)
static void allocPg(size_t count, void **logicals, uint32_t *physicals, int isIn, int isOut) {
	struct IOPreparationTable prep = {
		kIOLogicalRanges, // options
		0, // state
		0, // preparationID
		kCurrentAddressSpaceID, // addressSpace
		0x1000, // granularity
		0 // firstPrepared
	};

	// Allocate entirely within our own pages
	char *base = (char *)((
		(size_t)PoolAllocateResident((0x1000 * count + 0x1fff) & ~0xfff, true)
	 	+ 0xfff) & ~0xfff);

	prep.options |= (kIOIsInput * !!isIn) | (kIOIsOutput * !!isOut);
	prep.mappingEntryCount = count;
	prep.logicalMapping = (void *)logicals;
	prep.physicalMapping = (void *)physicals;
	prep.rangeInfo.range.base = base;
	prep.rangeInfo.range.length = 0x1000 * count;

	PrepareMemoryForIO(&prep);

	if (prep.lengthPrepared != 0x1000 * count) {
		lprintf("Fatal: failed preparation of=%08x pages=%d got=%d\n",
			base, count, prep.lengthPrepared);
		*logicals = NULL;
		*physicals = 0;
	};
}

void VTSend(uint16_t queue, uint16_t buffer) {
	// Call the critical section as a secondary interrupt
	CallSecondaryInterruptHandler2(enqueueBH, NULL, (void *)queue, (void *)buffer);

	// "If flags is 0, the driver MUST send a notification"
	if (gQueues[queue].used->le_flags == 0) {
		gNotify[queue * gNotifyMultiplier / 2] = EndianSwap16Bit(queue);
		SynchronizeIO();
	}
}

int VTDone(uint16_t queue) {
	return gQueues[queue].used->le_idx == gQueues[queue].avail->le_idx;
}

void VTSilence(uint16_t queue) {
	gQueues[queue].avail->le_flags = 0x0100; // EndianSwap16Bit(1);
}

// Run as a secondary interrupt to serialize
OSStatus enqueueBH(void *queueU16, void *bufferU16) {
	uint16_t q = (uint16_t)queueU16;
	uint16_t b = (uint16_t)bufferU16;
	uint16_t idx;

	idx = EndianSwap16Bit(gQueues[q].avail->le_idx);
	gQueues[q].avail->le_ring[idx & (gQueues[q].count - 1)] = EndianSwap16Bit(b);
	SynchronizeIO();
	gQueues[q].avail->le_idx = EndianSwap16Bit(idx + 1);
	SynchronizeIO();

	return noErr;
}

// Deduplicate attempts to run the secondary interrupt handler
static int queueIntQueued;

static InterruptMemberNumber interruptTH(InterruptSetMember ist, void *refCon, uint32_t intCount) {
	uint8_t isr;

	isr = *gISRStatus; // read flags and also deassert the interrupt

	if ((isr & 1) && !queueIntQueued) {
		uint16_t i;

		QueueSecondaryInterruptHandler(queueInterruptBH, NULL, NULL, NULL);
		queueIntQueued = 1;

		// Suppress further interrupts until the secondary handler is done
		for (i=0; i<VTNumQueues; i++) {
			gQueues[i].avail->le_flags = 0x0100; // EndianSwap16Bit(1);
		}
		SynchronizeIO();
	}

	if (isr & 2) {
		QueueSecondaryInterruptHandler(configInterruptBH, NULL, NULL, NULL);
	}

	return noErr;
}

static OSStatus configInterruptBH(void *arg1, void *arg2) {
	if (gConfigChanged) gConfigChanged();
	return noErr;
}

// Secondary interrupt routines (like this one) are serialised
static OSStatus queueInterruptBH(void *arg1, void *arg2) {
	uint16_t q;

	// Check for new buffers in the queue
	readQueues();

	// Re-enable interrupts
	queueIntQueued = 0;
	for (q=0; q<VTNumQueues; q++) {
		gQueues[q].avail->le_flags = EndianSwap16Bit(0);
	}
	SynchronizeIO();

	// Check one more time, because interrupts are asynchronous
	readQueues();

	return noErr;
}

static void readQueues(void) {
	uint16_t q;
	for (q=0; q<VTNumQueues; q++) {
		while (gQueues[q].lastUsed != EndianSwap16Bit(gQueues[q].used->le_idx)) {
			uint16_t idx = gQueues[q].lastUsed & (gQueues[q].count - 1);
			uint16_t buf = EndianSwap16Bit(gQueues[q].used->ring[idx].le_id);
			size_t len = EndianSwap32Bit(gQueues[q].used->ring[idx].le_len);

			// Call back to the device layer
			if (gQueueRecv) gQueueRecv(q, buf, len);

			gQueues[q].lastUsed++;
		}
	}
}
