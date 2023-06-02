// A synchronous 9P2000.u interface backing onto Virtio.
// Functions return true on failure.
// Takes over the Virtio interface, implements DNotified and DConfigChange.

#pragma once

#include <stdint.h>
#include <stdbool.h>

enum {
	NOFID = 0xffffffff,
};

struct qid {
	uint8_t type;
	uint32_t version;
	uint64_t path;
};

// Single large buffer used by P9read/write
// Clobbered by all calls
extern char *P9buf;
extern uint32_t P9max;

// Error string (check when a function returns true)
extern char P9err[256];

bool P9init(uint16_t vioq, uint16_t viobuffers);
bool P9attach(uint32_t tx_fid, uint32_t tx_afid, char *tx_uname, char *tx_aname, struct qid *rx_qid);
bool P9walk(uint32_t tx_fid, uint32_t tx_newfid, char *tx_name, struct qid *rx_qid);
bool P9open(uint32_t tx_fid, uint8_t tx_mode, struct qid *rx_qid, uint32_t *rx_iounit);
bool P9create(uint32_t tx_fid, char *tx_name, uint32_t tx_perm, uint8_t tx_mode, char *tx_extn, struct qid *rx_qid, uint32_t *rx_iounit);
bool P9read(uint32_t tx_fid, uint64_t tx_offset, uint32_t count, uint32_t *actual_count);
