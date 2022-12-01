#include <stdbool.h>
#include <stdint.h>

#include <DriverSynchronization.h>
#include <PCI.h>

#include "allocator.h"
#include "atomic.h"
#include "allocator.h"
#include "device.h"
#include "transport.h"
#include "virtqueue-structs.h"

#include "virtqueue.h"

enum {
	MAX_VQ = 2,
	MAX_RING = 256,
};

struct virtq {
	uint16_t size;
	uint16_t used_ctr;
	int32_t interest;
	struct virtq_desc *desc;
	struct virtq_avail *avail;
	struct virtq_used *used;
	uint8_t bitmap[MAX_RING/8];
	void *tag[MAX_RING];
};

static struct virtq queues[MAX_VQ];
static void QSendAtomicPart(void *q, void *buf);

uint16_t QInit(uint16_t q, uint16_t max_size) {
	void *pages;
	uint32_t phys[3];
	uint16_t size = max_size;

	if (q > MAX_VQ) return 0;

	if (size > MAX_RING) size = MAX_RING;
	if (size > VQueueMaxSize(q)) size = VQueueMaxSize(q);

	pages = AllocPages(3, phys);
	if (pages == NULL) return 0;

	// Underlying transport needs the physical addresses of the rings
	VQueueSet(q, size, phys[0], phys[1], phys[2]);

	// But we only need to keep the logical pointers
	queues[q].desc = pages;
	queues[q].avail = (void *)((char *)pages + 0x1000);
	queues[q].used = (void *)((char *)pages + 0x2000);

	queues[q].size = size;

	// Disable notifications until QInterest
	queues[q].avail->le_flags = 0x0100;

	return size;
}

bool QSend(uint16_t q, uint16_t n_out, uint16_t n_in, uint32_t *addrs, uint32_t *sizes, void *tag) {
	uint16_t count, try, i;
	uint16_t buffers[MAX_RING];

	// Find enough free buffer descriptors
	count = 0;
	try = 0;
	for (try=0; try<queues[q].size; try++) {
		if (!TestAndSet(try%8, queues[q].bitmap + try/8)) {
			buffers[count++] = try;
			if (count == n_out+n_in) break;
		}
	}

	// Not enough
	if (count < n_out+n_in) {
		for (i=0; i<count; i++) TestAndClear(i%8, queues[q].bitmap + i/8);
		return false;
	}

	queues[q].tag[buffers[0]] = tag;

	// Populate the descriptors
	for (i=0; i<n_out+n_in; i++) {
		uint16_t buf = buffers[i];
		uint16_t next = (i<n_out+n_in-1) ? buffers[i+1] : 0;
		uint16_t flags =
			((i<n_out+n_in-1) ? VIRTQ_DESC_F_NEXT : 0) |
			((i>=n_out) ? VIRTQ_DESC_F_WRITE : 0);

		queues[q].desc[buf].le_addr = EndianSwap32Bit(addrs[i]);
		queues[q].desc[buf].le_len = EndianSwap32Bit(sizes[i]);
		queues[q].desc[buf].le_flags = EndianSwap16Bit(flags);
		queues[q].desc[buf].le_next = EndianSwap16Bit(next);
	}

	// Put a pointer to the "head" descriptor in the avail queue
	Atomic2(QSendAtomicPart, (void *)q, (void *)buffers[0]);

	return true;
}

static void QSendAtomicPart(void *q_voidptr, void *buf_voidptr) {
	uint16_t q = (uint16_t)q_voidptr;
	uint16_t buf = (uint16_t)buf_voidptr;
	uint16_t idx;

	idx = EndianSwap16Bit(queues[q].avail->le_idx);
	queues[q].avail->le_ring[idx & (queues[q].size - 1)] = EndianSwap16Bit(buf);
	SynchronizeIO();
	queues[q].avail->le_idx = EndianSwap16Bit(idx + 1);
	SynchronizeIO();
}

void QNotify(uint16_t q) {
	if (queues[q].used->le_flags == 0) VNotify(q);
}

void QInterest(uint16_t q, int32_t delta) {
	int32_t interest = AddAtomic(delta, &queues[q].interest);
	queues[q].avail->le_flags = (interest+delta > 0) ? 0 : 0x0100;
}

// Called by transport hardware interrupt to reduce chance of redundant interrupts
void QDisarm(void) {
	uint16_t q;
	for (q=0; queues[q].size != 0; q++) {
		queues[q].avail->le_flags = 0x0100; // EndianSwap16Bit(1);
	}
	SynchronizeIO();
}

// Called by transport at "deferred" or "secondary" interrupt time
void QNotified(void) {
	uint16_t q;

	for (q=0; queues[q].size != 0; q++) {
		QPoll(q);
	}

	VRearm();
	for (q=0; queues[q].size != 0; q++) {
		queues[q].avail->le_flags = (queues[q].interest > 0) ? 0 : 0x0100;
	}
	SynchronizeIO();

	for (q=0; queues[q].size != 0; q++) {
		QPoll(q);
	}
}

static void QPoll(uint16_t q) {
	while (queues[q].used_ctr != EndianSwap16Bit(queues[q].used->le_idx)) {
		uint16_t used_idx, desc_idx;
		size_t len;

		used_idx = queues[q].used_ctr++ & (queues[q].size - 1);
		desc_idx = EndianSwap16Bit(queues[q].used->ring[used_idx].le_id);
		len = EndianSwap32Bit(queues[q].used->ring[used_idx].le_len);

		DNotified(q, len, queues[q].tag[desc_idx]);

		// Free the descriptors (clear their bits in the bitmap)
		for (;;) {
			TestAndClear(desc_idx%8, queues[q].bitmap + desc_idx/8);
			if ((queues[q].desc[desc_idx].le_flags & EndianSwap16Bit(VIRTQ_DESC_F_NEXT)) == 0)
				break;
			desc_idx = EndianSwap16Bit(queues[q].desc[desc_idx].le_next);
		}
	}
}
