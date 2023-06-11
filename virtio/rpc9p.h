// A synchronous 9P2000.u interface backing onto Virtio.
// Functions return true on failure.
// Takes over the Virtio interface, implements DNotified and DConfigChange.

#pragma once

#include <stdint.h>
#include <stdbool.h>

struct Qid9 {
	uint8_t type;
	uint32_t version;
	uint64_t path;
};

struct Stat9 { // Abbreviated from the 9p2000.u spec
	struct Qid9 qid;
	uint32_t mode;
	uint32_t atime, mtime;
	uint64_t length;
	char name[512];
	char linktarget[512];
};

// Single large buffer used by Read9/write
// Clobbered by all calls
extern char *Buf9;
extern uint32_t Max9;

// Error string (check when a function returns true)
extern char Err9[256];

bool Init9(uint16_t vioq, uint16_t viobuffers);
bool Attach9(uint32_t fid, uint32_t afid, char *uname, char *aname, struct Qid9 *retqid);
bool Walk9(uint32_t fid, uint32_t newfid, uint16_t nwname, const char **name, uint16_t *retnwqid, struct Qid9 *retqid);
bool Clunk9(uint32_t fid);
bool Read9(uint32_t fid, uint64_t offset, uint32_t count, uint32_t *actual_count);
