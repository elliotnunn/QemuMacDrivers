#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <stddef.h>
#include <stdint.h>

// Return the page-aligned logical address
// Populate the array of physical page addresses
void *AllocPages(size_t count, uint32_t *physicalPageAddresses);

// Pass in the logical address
void FreePages(void *addr);

#endif
