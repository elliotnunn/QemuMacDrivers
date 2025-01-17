// Load DRVRs over 64K because the Slot Manager can't

#include <Memory.h>
#include <Slots.h>
#include <ROMDefs.h>

#include <stdint.h>

void dbgStack(long) = {0xa9ff}; // for when MacsBug keyboard input is broken

struct driverRec {
	uint32_t size;
	char code[];
};

void exec(struct SEBlock *pb) {
	int err;

	struct SpBlock sp = {.spSlot=pb->seSlot, .spID=pb->sesRsrcId};
	err = SRsrcInfo(&sp); // sp.spsPointer = the sResource
	if (err) {
		pb->seStatus = err;
		return;
	}

	sp.spID = sRsrcDrvrDir;
	err = SFindStruct(&sp); // sp.spsPointer = driver directory
	if (err) {
		pb->seStatus = err;
		return;
	}

	sp.spID = sMacOS68020;
	err = SFindStruct(&sp); // sp.spsPointer = this driver
	if (err) {
		pb->seStatus = err;
		return;
	}

	// Access slot memory directly -- assumes all 4 byte lanes open
	struct driverRec *rec = (void *)sp.spsPointer;
	uint32_t size = rec->size - 4;

	ReserveMem(size);
	Handle hdl = NewHandleSys(size);
	if (hdl == NULL) {
		pb->seStatus = memFullErr;
		return;
	}
	BlockMove(rec->code, *hdl, size);

	pb->seResult = (long)hdl;
	pb->seFlags = 0;
	pb->seStatus = 1; // seSuccess
}
