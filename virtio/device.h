// Implemented by device-[gpu|9p|etc].c

#ifndef DEVICE_H
#define DEVICE_H

#include <stdint.h>

// Device has finished with a buffer
void DNotified(uint16_t q, uint16_t buf, size_t len, void *tag);

// Device-specific configuration struct has changed
void DConfigChange(void);

#endif