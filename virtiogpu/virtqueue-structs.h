#ifndef VIRTQUEUE_STRUCTS_H
#define VIRTQUEUE_STRUCTS_H

struct virtq_desc { // all little-endian
	uint32_t le_addr; // guest-physical
	uint32_t le_addr_hi;
	uint32_t le_len;
	uint16_t le_flags;
	uint16_t le_next;
};

/* This marks a buffer as continuing via the next field. */
#define VIRTQ_DESC_F_NEXT 1
/* This marks a buffer as device write-only (otherwise device read-only). */
#define VIRTQ_DESC_F_WRITE 2
/* This means the buffer contains a list of buffer descriptors. */
#define VIRTQ_DESC_F_INDIRECT 4

struct virtq_avail {
	uint16_t le_flags;
	uint16_t le_idx;
	uint16_t le_ring[999];
};

struct virtq_used_elem {
	uint16_t le_id; // of descriptor chain
	uint16_t le_pad;
	uint32_t le_len; // in bytes of descriptor chain
};

struct virtq_used {
	uint16_t le_flags;
	uint16_t le_idx;
	struct virtq_used_elem ring[999]; // 8 bytes each
};

#endif
