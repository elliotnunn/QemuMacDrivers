#include <stdint.h>

#include <DriverSynchronization.h>

#include <stdbool.h>

#include "allocator.h"
#include "atomic.h"
#include "allocator.h"
#include "device.h"
#include "transport.h"
#include "structs-virtqueue.h"

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
	if (q > MAX_VQ) return 0;

	uint16_t size = max_size;
	if (size > MAX_RING) size = MAX_RING;
	if (size > VQueueMaxSize(q)) size = VQueueMaxSize(q);

	uint32_t phys[3];
	void *pages = AllocPages(3, phys);
	if (pages == NULL) return 0;

	// Underlying transport needs the physical addresses of the rings
	VQueueSet(q, size, phys[0], phys[1], phys[2]);

	// But we only need to keep the logical pointers
	queues[q].desc = pages;
	queues[q].avail = (void *)((char *)pages + 0x1000);
	queues[q].used = (void *)((char *)pages + 0x2000);

	queues[q].size = size;

	// Disable notifications until QInterest
	queues[q].avail->flags = 1;

	return size;
}

bool QSend(uint16_t q, uint16_t n_out, uint16_t n_in, uint32_t *addrs, uint32_t *sizes, void *tag) {
	uint16_t buffers[MAX_RING];

	// Find enough free buffer descriptors
	uint16_t count = 0;
	for (uint16_t try=0; try<queues[q].size; try++) {
		if (!TestAndSet(try%8, queues[q].bitmap + try/8)) {
			buffers[count++] = try;
			if (count == n_out+n_in) break;
		}
	}

	// Not enough
	if (count < n_out+n_in) {
		for (uint16_t i=0; i<count; i++) {
			TestAndClear(i%8, queues[q].bitmap + i/8);
		}
		return false;
	}

	queues[q].tag[buffers[0]] = tag;

	// Populate the descriptors
	for (uint16_t i=0; i<n_out+n_in; i++) {
		uint16_t buf = buffers[i];
		uint16_t next = (i<n_out+n_in-1) ? buffers[i+1] : 0;
		uint16_t flags =
			((i<n_out+n_in-1) ? VIRTQ_DESC_F_NEXT : 0) |
			((i>=n_out) ? VIRTQ_DESC_F_WRITE : 0);

		queues[q].desc[buf].addr = addrs[i];
		queues[q].desc[buf].len = sizes[i];
		queues[q].desc[buf].flags = flags;
		queues[q].desc[buf].next = next;
	}

	// Put a pointer to the "head" descriptor in the avail queue
	Atomic2(QSendAtomicPart, (void *)(uint32_t)q, (void *)(uint32_t)buffers[0]);

	return true;
}

static void QSendAtomicPart(void *q_voidptr, void *buf_voidptr) {
	uint16_t q = (uint16_t)(uint32_t)q_voidptr;
	uint16_t buf = (uint16_t)(uint32_t)buf_voidptr;

	uint16_t idx = queues[q].avail->idx;
	queues[q].avail->ring[idx & (queues[q].size - 1)] = buf;
	SynchronizeIO();
	queues[q].avail->idx = idx + 1;
	SynchronizeIO();
}

void QNotify(uint16_t q) {
	if (queues[q].used->flags == 0) VNotify(q);
}

void QInterest(uint16_t q, int32_t delta) {
	AddAtomic(delta, &queues[q].interest);
	queues[q].avail->flags = (queues[q].interest == 0);
}

// Called by transport hardware interrupt to reduce chance of redundant interrupts
void QDisarm(void) {
	for (uint16_t q=0; queues[q].size != 0; q++) {
		queues[q].avail->flags = 1;
	}
	SynchronizeIO();
}

// Called by transport at "deferred" or "secondary" interrupt time
void QNotified(void) {
	for (uint16_t q=0; queues[q].size != 0; q++) {
		QPoll(q);
	}

	VRearm();
	for (uint16_t q=0; queues[q].size != 0; q++) {
		queues[q].avail->flags = queues[q].interest == 0;
	}
	SynchronizeIO();

	for (uint16_t q=0; queues[q].size != 0; q++) {
		QPoll(q);
	}
}

// Call DNotified for each buffer in the used ring
void QPoll(uint16_t q) {
	uint16_t i = queues[q].used_ctr;
	uint16_t mask = queues[q].size - 1;
	uint16_t end = queues[q].used->idx;
	queues[q].used_ctr = end;

	for (; i != end; i++) {
		uint16_t desc = queues[q].used->ring[i&mask].id;
		size_t len = queues[q].used->ring[i&mask].len;

		DNotified(q, desc, len, queues[q].tag[desc]);
	}
}

// Called by DNotified to return descriptors to the pool usable by QSend
void QFree(uint16_t q, uint16_t buf) {
	for (;;) {
		TestAndClear(buf%8, queues[q].bitmap + buf/8);
		if (((queues[q].desc[buf].flags) & VIRTQ_DESC_F_NEXT) == 0)
			break;
		buf = queues[q].desc[buf].next;
	}
}
