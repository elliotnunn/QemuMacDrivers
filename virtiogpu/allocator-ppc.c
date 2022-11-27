/*
Bugs:
- Memory not cache-inhibited. We assume it is coherent.
- Can't free allocations: doesn't matter if device is not hot-swappable.
*/

#include <DriverServices.h>
#include <stddef.h>
#include <stdint.h>
#include "allocator.h"

void *AllocPages(size_t count, uint32_t *physicalPageAddresses) {
	char *unaligned, *aligned;
	OSStatus err;

	struct IOPreparationTable prep = {0};

	unaligned = PoolAllocateResident((count + 1) * 0x1000, true);
	if (unaligned == NULL) return NULL;

	aligned = (char *)(((unsigned long)unaligned + 0xfff) & ~0xfff);

	prep.options = kIOLogicalRanges;
	prep.state = 0;
	prep.preparationID = 0;
	prep.addressSpace = kCurrentAddressSpaceID;
	prep.granularity = 0x1000 * count; // partial preparation unacceptable
	prep.firstPrepared = 0;
	prep.lengthPrepared = 0;
	prep.mappingEntryCount = count;
	prep.logicalMapping = NULL;
	prep.physicalMapping = (void *)physicalPageAddresses;
	prep.rangeInfo.range.base = aligned;
	prep.rangeInfo.range.length = 0x1000 * count;

	err = PrepareMemoryForIO(&prep);

	if (prep.state != kIOStateDone) {
		PoolDeallocate(unaligned);
		return NULL;
	}

	return aligned;
}
