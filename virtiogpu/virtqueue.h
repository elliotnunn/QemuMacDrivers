#ifndef VIRTQUEUE_H
#define VIRTQUEUE_H

#include <stdbool.h>
#include <stdint.h>

// Create a descriptor ring for this virtqueue, return actual size
uint16_t QInit(uint16_t q, uint16_t max_size);

// Never blocks, just returns false if not enough descriptors available
bool QSend(
	uint16_t q,
	uint16_t n_out, uint16_t n_in,
	uint32_t *phys_addrs, uint32_t *sizes,
	void *tag);

// Call after one or more QSend()s
void QNotify(uint16_t q);

// Called by transport to reduce chance of redundant interrupts
void QDisarm(void);

// Called by transport about a change to the used ring
void QNotified(void);

#endif
