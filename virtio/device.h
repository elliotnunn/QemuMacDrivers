// Implemented by device-[gpu|9p|etc].c

#pragma once

#include <stdint.h>

// Device has finished with a buffer
void DNotified(uint16_t q, size_t len, void *tag);

// Device-specific configuration struct has changed
void DConfigChange(void);
