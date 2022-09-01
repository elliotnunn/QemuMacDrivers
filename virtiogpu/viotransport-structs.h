#ifndef _VIOTRANSPORT_STRUCTS_H_
#define _VIOTRANSPORT_STRUCTS_H_

struct virtio_pci_common_cfg { 
	// About the whole device
	uint32_t le_device_feature_select;	// read-write
	uint32_t le_device_feature;         // read-only for driver
	uint32_t le_driver_feature_select;  // read-write
	uint32_t le_driver_feature;         // read-write
	uint16_t le_msix_config;            // read-write
	uint16_t le_num_queues;             // read-only for driver
	uint8_t device_status;              // read-write
	uint8_t config_generation;          // read-only for driver

	// About a specific virtqueue
	uint16_t le_queue_select;           // read-write
	uint16_t le_queue_size;             // read-write
	uint16_t le_queue_msix_vector;      // read-write
	uint16_t le_queue_enable;           // read-write
	uint16_t le_queue_notify_off;       // read-only for driver
	uint32_t le_queue_desc;             // read-write
	uint32_t le_queue_desc_hi;
	uint32_t le_queue_driver;           // read-write
	uint32_t le_queue_driver_hi;
	uint32_t le_queue_device;           // read-write
	uint32_t le_queue_device_hi;
};

struct virtq_desc { // all little-endian
	uint32_t le_addr; // guest-physical
	uint32_t le_addr_hi;
	uint32_t le_len;
	uint16_t le_flags;
	uint16_t le_next;
};

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
