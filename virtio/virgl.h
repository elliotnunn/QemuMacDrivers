// For now, this file is a terrible hack to tell device-gpu.c and virgl.c about each other

#pragma once

#include <stddef.h>
#include <stdint.h>

// Implement in virgl.c
void VirglTest(void);

// Implement in device-gpu.c
// Take a buffer and return the "type" (error value)
uint32_t VirglSend(void *req, size_t req_size);
