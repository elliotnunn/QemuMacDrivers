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

// Single large buffer used by Read9/write
// Clobbered by all calls
extern char *Buf9;
extern uint32_t Max9;

// Error string (check when a function returns true)
extern char Err9[256];

bool Init9(uint16_t vioq, uint16_t viobuffers);
bool Attach9(uint32_t tx_fid, uint32_t tx_afid, char *tx_uname, char *tx_aname, struct Qid9 *rx_qid);
bool Walk9(uint32_t tx_fid, uint32_t tx_newfid, uint16_t tx_nwname, const char **tx_name, uint16_t *rx_nwqid, struct Qid9 *rx_qid);
bool Clunk9(uint32_t tx_fid);
bool Open9(uint32_t tx_fid, uint8_t tx_mode, struct Qid9 *rx_qid, uint32_t *rx_iounit);
bool Create9(uint32_t tx_fid, char *tx_name, uint32_t tx_perm, uint8_t tx_mode, char *tx_extn, struct Qid9 *rx_qid, uint32_t *rx_iounit);
bool Read9(uint32_t tx_fid, uint64_t tx_offset, uint32_t count, uint32_t *actual_count);
