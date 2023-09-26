/*
Enough of the 9P2000.L protocol to support the Mac OS File Manager

Why the dot-L variant?

TODO:
- Reentrancy (for Virtual Memory, not for the single-threaded File Manager)
- Yield back to the File Manager while idle (rather than spinning)
- Range-based IO (rather than a virtio buffer for each page)
*/

#include <DriverServices.h>

#include <stdalign.h>
#include <stdarg.h>
#include <string.h>

#include "allocator.h"
#include "lprintf.h"
#include "panic.h"
#include "virtqueue.h"

#include "device.h" // implement DNotified and DConfigChange here instead
#include "9p.h"

enum {
	NOTAG = 0,
	ONLYTAG = 1,
	STRMAX = 127, // not including the null
};

#define READ16LE(S) ((255 & ((char *)S)[1]) << 8 | (255 & ((char *)S)[0]))
#define READ32LE(S) \
  ((uint32_t)(255 & ((char *)S)[3]) << 24 | (uint32_t)(255 & ((char *)S)[2]) << 16 | \
   (uint32_t)(255 & ((char *)S)[1]) << 8 | (uint32_t)(255 & ((char *)S)[0]))
#define READ64LE(S)                                                    \
  ((uint64_t)(255 & ((char *)S)[7]) << 070 | (uint64_t)(255 & ((char *)S)[6]) << 060 | \
   (uint64_t)(255 & ((char *)S)[5]) << 050 | (uint64_t)(255 & ((char *)S)[4]) << 040 | \
   (uint64_t)(255 & ((char *)S)[3]) << 030 | (uint64_t)(255 & ((char *)S)[2]) << 020 | \
   (uint64_t)(255 & ((char *)S)[1]) << 010 | (uint64_t)(255 & ((char *)S)[0]) << 000)
#define WRITE16LE(P, V)                        \
  (((char *)P)[0] = (0x000000FF & (V)), \
   ((char *)P)[1] = (0x0000FF00 & (V)) >> 8, ((char *)P) + 2)
#define WRITE32LE(P, V)                        \
  (((char *)P)[0] = (0x000000FF & (V)), \
   ((char *)P)[1] = (0x0000FF00 & (V)) >> 8, \
   ((char *)P)[2] = (0x00FF0000 & (V)) >> 16, \
   ((char *)P)[3] = (0xFF000000 & (V)) >> 24, ((char *)P) + 4)
#define WRITE64LE(P, V)                        \
  (((char *)P)[0] = (0x00000000000000FF & (V)) >> 000, \
   ((char *)P)[1] = (0x000000000000FF00 & (V)) >> 010, \
   ((char *)P)[2] = (0x0000000000FF0000 & (V)) >> 020, \
   ((char *)P)[3] = (0x00000000FF000000 & (V)) >> 030, \
   ((char *)P)[4] = (0x000000FF00000000 & (V)) >> 040, \
   ((char *)P)[5] = (0x0000FF0000000000 & (V)) >> 050, \
   ((char *)P)[6] = (0x00FF000000000000 & (V)) >> 060, \
   ((char *)P)[7] = (0xFF00000000000000 & (V)) >> 070, ((char *)P) + 8)

uint32_t Max9;

static uint32_t openfids;

static volatile bool flag;

static void **physicals; // newptr allocated block

int bufcnt;

#define QIDF "0x%02x.%x.%x"
#define QIDA(qid) qid.type, qid.version, (uint32_t)qid.path
#define READQID(ptr) (struct Qid9){*(char *)(ptr), READ32LE((char *)(ptr)+1), READ64LE((char *)(ptr)+5)}

static int transact(uint8_t cmd, const char *tfmt, const char *rfmt, ...);

int Init9(int bufs) {
	enum {Tversion = 100}; // size[4] Tversion tag[2] msize[4] version[s]
	enum {Rversion = 101}; // size[4] Rversion tag[2] msize[4] version[s]

	if (bufs > 256) bufs = 256;
	bufcnt = bufs;

	Max9 = 4096 * (bufs - 4);

	int err;
	char proto[128];
	err = transact(Tversion, "ds", "ds",
		Max9, "9P2000.L",
		&Max9, proto);
	if (err) return err;

	if (strcmp(proto, "9P2000.L")) {
		return EPROTONOSUPPORT;
	}

	return 0;
}

int Attach9(uint32_t fid, uint32_t afid, const char *uname, const char *aname, uint32_t n_uname, struct Qid9 *retqid) {
	enum {Tattach = 104}; // size[4] Tattach tag[2] fid[4] afid[4] uname[s] aname[s] n_uname[4]
	enum {Rattach = 105}; // size[4] Rattach tag[2] qid[13]

	return transact(Tattach, "ddssd", "Q",
		fid, afid, uname, aname, n_uname,
		retqid);
}

// Respects the protocol's 16-component maximum
// call with nwname 0 to duplicate a fid
int Walk9(uint32_t fid, uint32_t newfid, uint16_t nwname, const char *const *name, uint16_t *retnwqid, struct Qid9 *retqid) {
	enum {Twalk = 110}; // size[4] Twalk tag[2] fid[4] newfid[4] nwname[2] nwname*(wname[s])
	enum {Rwalk = 111}; // size[4] Rwalk tag[2] nwqid[2] nwqid*(wqid[13])

	if (newfid < 32 && fid != newfid && (openfids & (1<<newfid))) Clunk9(newfid);

	if (retnwqid) *retnwqid = 0;

	int done = 0;
	do {
		char path[1024];
		int willdo = 0, pathbytes = 0;

		// Pack the names into a buffer, and increment willdo
		while (done+willdo < nwname && willdo < 16) {
			int slen = strlen(name[done+willdo]);

			// buffer getting too big for us?
			if (pathbytes+2+slen >= sizeof path) break;

			WRITE16LE(path+pathbytes, slen);
			memcpy(path+pathbytes+2, name[done+willdo], slen);

			pathbytes += 2+slen;
			willdo++;
		}

		// Failed to pack even one name into the buffer?
		// (except for the nwname 0 case, to duplicate a fid)
		if (willdo == 0 && nwname != 0) return ENOMEM;

		char qids[16*13];
		uint16_t ok = 0;

		int err = transact(Twalk, "ddwB", "wB",
			fid, newfid, willdo, path, pathbytes,
			&ok, qids, sizeof qids);

		if (err) return err;

		if (retnwqid) *retnwqid += ok;
		if (retqid) {
			for (int i=0; i<ok; i++) {
				char *rawqid = qids + 13*i;
				retqid[done+i] =
					(struct Qid9){*rawqid, READ32LE(rawqid+1), READ64LE(rawqid+5)};
			}
		}

		done += ok;

		if (ok < willdo) return ENOENT;
	} while (done < nwname);

	if (newfid < 32) openfids |= 1<<newfid;

	return 0;
}

int Lopen9(uint32_t fid, uint32_t flags, struct Qid9 *retqid, uint32_t *retiounit) {
	enum {Tlopen = 12}; // size[4] Tlopen tag[2] fid[4] flags[4]
	enum {Rlopen = 13}; // size[4] Rlopen tag[2] qid[13] iounit[4]

	return transact(Tlopen, "dd", "Qd",
		fid, flags,
		retqid, retiounit);
}

int Lcreate9(uint32_t fid, uint32_t flags, uint32_t mode, uint32_t gid, const char *name, struct Qid9 *retqid, uint32_t *retiounit) {
	enum {Tlcreate = 14}; // size[4] Tlcreate tag[2] fid[4] name[s] flags[4] mode[4] gid[4]
	enum {Rlcreate = 15}; // size[4] Rlcreate tag[2] qid[13] iounit[4]

	return transact(Tlcreate, "dsddd", "Qd",
		fid, name, flags, mode, gid,
		retqid, retiounit);
}

int Remove9(uint32_t fid) {
	enum {Tremove = 122}; // size[4] Tremove tag[2] fid[4]
	enum {Rremove = 123}; // size[4] Rremove tag[2]

	return transact(Tremove, "d", "",
		fid);
}

int Mkdir9(uint32_t dfid, uint32_t mode, uint32_t gid, const char *name, struct Qid9 *retqid) {
	enum {Tmkdir = 72}; // size[4] Tmkdir tag[2] dfid[4] name[s] mode[4] gid[4]
	enum {Rmkdir = 73}; // size[4] Rmkdir tag[2] qid[13]

	return transact(Tmkdir, "dsdd", "Q",
		dfid, name, mode, gid,
		retqid);
}

struct rdbuf {
	uint32_t fid;
	uint64_t nextRequest;
	uint32_t size, recvd, used;
	char data[];
};

#define RDBUFALIGN(voidptr) ((struct rdbuf *) \
		(((uintptr_t)buf + alignof (struct rdbuf) - 1) & -alignof (struct rdbuf)))

void InitReaddir9(uint32_t fid, void *buf, size_t bufsize) {
	struct rdbuf *rdbuf = RDBUFALIGN(buf);

	rdbuf->fid = fid;
	rdbuf->nextRequest = 0;
	rdbuf->size = (char *)buf + bufsize - rdbuf->data;
	rdbuf->recvd = 0;
	rdbuf->used = 0;
}

// 0 = ok, negative = eof, positive = linux errno
int Readdir9(void *buf, struct Qid9 *retqid, char *rettype, char retname[512]) {
	enum {Treaddir = 40}; // size[4] Treaddir tag[2] fid[4] offset[8] count[4]
	enum {Rreaddir = 41}; // size[4] Rreaddir tag[2] count[4] data[count]
	                      // "data" = qid[13] offset[8] type[1] name[s]

	struct rdbuf *rdbuf = RDBUFALIGN(buf);

	if (rdbuf->used >= rdbuf->recvd) {
		int err = transact(Treaddir, "dqd", "dB",
			rdbuf->fid, rdbuf->nextRequest, rdbuf->size,
			&rdbuf->recvd, rdbuf->data, rdbuf->size);

		if (err) return err;

		rdbuf->used = 0;

		if (rdbuf->recvd == 0) return -1;
	}

	// qid field at +0
	if (retqid) {
		*retqid = READQID(rdbuf->data + rdbuf->used);
	}

	// offset field at +13
	rdbuf->nextRequest = READ64LE(rdbuf->data + rdbuf->used + 13);

	// type field at +21
	if (rettype) {
		*rettype = *(rdbuf->data + rdbuf->used + 21);
	}

	// name field at +22
	uint16_t nlen = READ16LE(rdbuf->data + rdbuf->used + 22);

	if (retname) {
		uint16_t copylen = nlen;
		if (copylen > 511) copylen = 511;
		memcpy(retname, rdbuf->data + rdbuf->used + 24, copylen);
		retname[copylen] = 0;
	}

	rdbuf->used += 24 + nlen;

	return 0;
}

int Getattr9(uint32_t fid, uint64_t request_mask, struct Stat9 *ret) {
	enum {Tgetattr = 24}; // size[4] Tgetattr tag[2] fid[4] request_mask[8]
	enum {Rgetattr = 25}; // size[4] Rgetattr tag[2] valid[8] qid[13]
	                      // mode[4] uid[4] gid[4] nlink[8] rdev[8]
	                      // size[8] blksize[8] blocks[8] atime_sec[8]
	                      // atime_nsec[8] mtime_sec[8] mtime_nsec[8]
	                      // ctime_sec[8] ctime_nsec[8] btime_sec[8]
	                      // btime_nsec[8] gen[8] data_version[8]

	transact(Tgetattr, "dq", "qQdddqqqqqqqqqqqqqqq",
		fid, request_mask,

		// very many return fields
		&ret->valid, &ret->qid, &ret->mode, &ret->uid, &ret->gid,
		&ret->nlink, &ret->rdev, &ret->size, &ret->blksize, &ret->blocks,
		&ret->atime_sec, &ret->atime_nsec,
		&ret->mtime_sec, &ret->mtime_nsec,
		&ret->ctime_sec, &ret->ctime_nsec,
		NULL, NULL, NULL, NULL); // discard btime, gen and data_version fields
}

int Clunk9(uint32_t fid) {
	enum {Tclunk = 120}; // size[4] Tclunk tag[2] fid[4]
	enum {Rclunk = 121}; // size[4] Rclunk tag[2]

	if (fid < 32) openfids &= ~(1<<fid);

	return transact(Tclunk, "d", "",
		fid);
}

int Read9(uint32_t fid, void *buf, uint64_t offset, uint32_t count, uint32_t *actual_count) {
	enum {Tread = 116}; // size[4] Tread tag[2] fid[4] offset[8] count[4]
	enum {Rread = 117}; // size[4] Rread tag[2] count[4] data[count]

	// In event of failure, emphasise that no bytes were read
	if (actual_count) {
		*actual_count = 0;
	}

	return transact(Tread, "dqd", "dB",
		fid, offset, count,
		actual_count, buf, count);
}

int Write9(uint32_t fid, void *buf, uint64_t offset, uint32_t count, uint32_t *actual_count) {
	enum {Twrite = 118}; // size[4] Twrite tag[2] fid[4] offset[8] count[4] data[count]
	enum {Rwrite = 119}; // size[4] Rwrite tag[2] count[4]

	// In event of failure, emphasise that no bytes were read
	if (actual_count) {
		*actual_count = 0;
	}

	return transact(Twrite, "dqdB", "d",
		fid, offset, count, buf, count,
		actual_count);
}

// This will be called (possibly by an interrupt) while transact() spins
void DNotified(uint16_t q, uint16_t buf, size_t len, void *tag) {
	QFree(q, buf);
	flag = true;
}

void DConfigChange(void) {
}

/*
letter |  Tx  |  Rx  | Tx args      | Rx args       | comment
b         ok     ok    uint8_t        uint8_t *       byte
w         ok     ok    uint16_t       uint16_t *      word(16)
d         ok     ok    uint32_t       uint32_t *      dword(32)
q         ok     ok    uint64_t       uint64_t *      qword(64)
s         ok    @end   const char *   char *          string(16-prefix)
Q                ok                   struct Qid9 *   qid
B        @end   @end   const void *   void *          large trailing buffer
                        + uint32_t      + uint32_t
*/

static int transact(uint8_t cmd, const char *tfmt, const char *rfmt, ...) {
	char t[256] = {}, r[256] = {}; // enough to store just about anything (not page aligned sadly)
	int ts=7, rs=7;

	void *tbig = NULL, *rbig = NULL;
	uint32_t tbigsize = 0, rbigsize = 0;

    va_list va;
    va_start(va, rfmt);

	for (const char *f=tfmt; *f!=0; f++) {
		if (*f == 'b') {
			uint8_t val = va_arg(va, unsigned int); // promoted
			t[ts++] = val;
		} else if (*f == 'w') {
			uint16_t val = va_arg(va, unsigned int); // maybe promoted
			WRITE16LE(t+ts, val);
			ts += 2;
		} else if (*f == 'd') {
			uint32_t val = va_arg(va, uint32_t);
			WRITE32LE(t+ts, val);
			ts += 4;
		} else if (*f == 'q') {
			uint64_t val = va_arg(va, uint64_t);
			WRITE64LE(t+ts, val);
			ts += 8;
		} else if (*f == 's') {
			const char *s = va_arg(va, const char *);
			uint16_t slen = s ? strlen(s) : 0;
			WRITE16LE(t+ts, slen);
			memcpy(t+ts+2, s, slen);
			ts += 2 + slen;
		} else if (*f == 'B') {
			tbig = va_arg(va, void *);
			tbigsize = va_arg(va, size_t);
		}
	}

	WRITE32LE(t, ts + tbigsize); // size field
	*(t+4) = cmd; // T-command number
	WRITE16LE(t+5, 0); // zero is our only tag number (for now...)

// 	lprintf("> ");
// 	for (int i=0; i<ts; i++) {
// 		lprintf("%02x", 255 & t[i]);
// 	}
// 	lprintf(" ");
// 	for (int i=0; i<tbigsize; i++) {
// 		lprintf("%02x", 255 & ((char *)tbig)[i]);
// 	}
// 	lprintf("\n");

	// add up rx buffer size
	// (unfortunately need to iterate the VA list just to get the "big" buffer)
    va_list tmpva;
    va_copy(tmpva, va);
	for (const char *f=rfmt; *f!=0; f++) {
		if (*f == 'b') {
			va_arg(tmpva, unsigned int); // promoted
			rs += 1;
		} else if (*f == 'w') {
			va_arg(tmpva, unsigned int); // maybe promoted
			rs += 2;
		} else if (*f == 'd') {
			va_arg(tmpva, uint32_t);
			rs += 4;
		} else if (*f == 'q') {
			va_arg(tmpva, uint64_t);
			rs += 8;
		} else if (*f == 's') { // receiving arbitrary-length strings is yuck!
			va_arg(tmpva, char *);
			rs += 2+STRMAX;
		} else if (*f == 'Q') {
			va_arg(tmpva, struct Qid9 *);
			rs += 13;
		} else if (*f == 'B') {
			rbig = va_arg(tmpva, void *);
			rbigsize = va_arg(tmpva, size_t); // this is problematic... the argument is actually hard to find!
		}
	}
	va_end(tmpva);

	// Make room for an Rlerror response to any request (Tclunk doesn't leave enough)
	// (Assume that if a "B" trailer is supplied, it is large enough)
	if (rs < 11 && rbigsize == 0) rs = 11;

	// this awful API requires an array of pages!
	PhysicalAddress pa[bufcnt];
	uint32_t sz[bufcnt];

	struct AddressRange ranges[] = { // keep the tx before the rx ranges
		{.base=t, .length=ts},
		{.base=tbig, .length=tbigsize},
		{.base=r, .length=rs}, // this won't work!!
		{.base=rbig, .length=rbigsize},
	};

	// A zero-length range is not allowed, so remove those
	int nrange = 0;
	for (int i=0; i<4; i++) {
		if (ranges[i].length != 0) {
			ranges[nrange++] = ranges[i];
		}
	}

	struct IOPreparationTable prep = {
		.options = kIOLogicalRanges|kIOMultipleRanges|kIOCoherentDataPath,
		.addressSpace = kCurrentAddressSpaceID,
		.physicalMapping = pa,
		.mappingEntryCount = sizeof pa/sizeof *pa,
		.rangeInfo = {.multipleRanges = {.entryCount = nrange, .rangeTable = ranges}}
	};

	// Should never return paramErr
	if (PrepareMemoryForIO(&prep)) panic("PrepareMemoryForIO failure");

	// Abandon a partial preparation
	if (prep.lengthPrepared != ts+tbigsize+rs+rbigsize) {
		CheckpointIO(prep.preparationID, 0);
		return ENOMEM;
	}

	// Associate a length with each physical address
	// Don't bother chunking the addresses together
	long txn=0, rxn=0;
	int i=0, totalbytes=0;
	for (int r=0; r<nrange; r++) {
		long rngbytes=0;
		while (rngbytes < ranges[r].length && totalbytes < prep.lengthPrepared) {
			long size = 0x1000 - ((long)pa[i] & 0xfff); // 1 to 4096 bytes
			if (size > ranges[r].length - rngbytes) size = ranges[r].length - rngbytes;

			totalbytes += size;
			rngbytes += size;

			sz[i++] = size;

			if (ranges[r].base == t || ranges[r].base == tbig) {
				txn++;
			} else {
				rxn++;
			}
		}
	}

	flag = false;
	QSend(0, txn, rxn, (void *)pa, sz, NULL);
	QNotify(0);
	while (!flag) QPoll(0); // spin -- unfortunate

	CheckpointIO(prep.preparationID, 0);

// 	lprintf("< ");
// 	for (int i=0; i<rs; i++) {
// 		lprintf("%02x", 255 & r[i]);
// 	}
// 	lprintf(" ");
// 	for (int i=0; i<rbigsize; i++) {
// 		lprintf("%02x", 255 & ((char *)rbig)[i]);
// 	}
// 	lprintf("\n");

	if (r[4] == 7 /*Rlerror*/) {
		// The errno field might be split between a header ("bwd" etc in
		// the format string) and a trailer (the "B" in the format string).
		uint32_t err = 0;
		char *errbyte = r + 7;
		for (int i=0; i<4; i++) {
			if (errbyte == r + rs) errbyte = rbig;
			err = (uint32_t)(255 & *errbyte) << 24 | err >> 8;
			errbyte++;
		}
		return err; // linux E code
	}

	rs = 7; // rewind to just after the tag field
	// and notice that we pick up where "va" left off
	for (const char *f=rfmt; *f!=0; f++) {
		if (*f == 'b') {
			uint8_t *ptr = va_arg(va, uint8_t *);
			if (ptr) *ptr = *(r+rs);
			rs += 1;
		} else if (*f == 'w') {
			uint16_t *ptr = va_arg(va, uint16_t *);
			if (ptr) *ptr = READ16LE(r+rs);
			rs += 2;
		} else if (*f == 'd') {
			uint32_t *ptr = va_arg(va, uint32_t *);
			if (ptr) *ptr = READ32LE(r+rs);
			rs += 4;
		} else if (*f == 'q') {
			uint64_t *ptr = va_arg(va, uint64_t *);
			if (ptr) *ptr = READ64LE(r+rs);
			rs += 8;
		} else if (*f == 's') { // receiving arbitrary-length strings is yuck!
			char *ptr = va_arg(va, char *);
			uint16_t slen = READ16LE(r+rs);
			if (ptr) {
				memcpy(ptr, r+rs+2, slen);
				*(ptr+slen) = 0; // null terminator
			}
			rs += 2 + slen;
		} else if (*f == 'Q') {
			struct Qid9 *ptr = va_arg(va, struct Qid9 *);
			if (ptr) *ptr =
				(struct Qid9){*(r+rs), READ32LE(r+rs+1), READ64LE(r+rs+5)};
			rs += 13;
		} else if (*f == 'B') {
			va_arg(va, void *);
			va_arg(va, size_t); // nothing actually to do
		}
	}

    va_end(va);

    return 0;
}
