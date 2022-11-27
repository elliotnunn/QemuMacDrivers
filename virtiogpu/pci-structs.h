#ifndef PCI_STRUCTS_H
#define PCI_STRUCTS_H

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

#endif
