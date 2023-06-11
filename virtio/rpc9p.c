#include <string.h>

#include "allocator.h"
#include "lprintf.h"
#include "virtqueue.h"

#include "device.h" // implement DNotified and DConfigChange here instead
#include "rpc9p.h"

enum {
	Tversion = 100, // size[4] Tversion tag[2] msize[4] version[s]
	Rversion = 101, // size[4] Rversion tag[2] msize[4] version[s]
	Tauth = 102,    // size[4] Tauth tag[2] afid[4] uname[s] aname[s]
	Rauth = 103,    // size[4] Rauth tag[2] aqid[13]
	Tattach = 104,  // size[4] Tattach tag[2] fid[4] afid[4] uname[s] aname[s] ...UNIX n_uname[4]
	Rattach = 105,  // size[4] Rattach tag[2] qid[13]
	Terror = 106,   // illegal
	Rerror = 107,   // size[4] Rerror tag[2] ename[s]
	Tflush = 108,   // size[4] Tflush tag[2] oldtag[2]
	Rflush = 109,   // size[4] Rflush tag[2]
	Twalk = 110,    // size[4] Twalk tag[2] fid[4] newfid[4] nwname[2] nwname*(wname[s])
	Rwalk = 111,    // size[4] Rwalk tag[2] nwqid[2] nwqid*(wqid[13])
	Topen = 112,    // size[4] Topen tag[2] fid[4] mode[1]
	Ropen = 113,    // size[4] Ropen tag[2] qid[13] iounit[4]
	Tcreate = 114,  // size[4] Tcreate tag[2] fid[4] name[s] perm[4] mode[1] ...UNIX extension[s]
	Rcreate = 115,  // size[4] Rcreate tag[2] qid[13] iounit[4]
	Tread = 116,    // size[4] Tread tag[2] fid[4] offset[8] count[4]
	Rread = 117,    // size[4] Rread tag[2] count[4] data[count]
	Twrite = 118,   // size[4] Twrite tag[2] fid[4] offset[8] count[4] data[count]
	Rwrite = 119,   // size[4] Rwrite tag[2] count[4]
	Tclunk = 120,   // size[4] Tclunk tag[2] fid[4]
	Rclunk = 121,   // size[4] Rclunk tag[2]
	Tremove = 122,  // size[4] Tremove tag[2] fid[4]
	Rremove = 123,  // size[4] Rremove tag[2]
	Tstat = 124,    // size[4] Tstat tag[2] fid[4]
	Rstat = 125,    // size[4] Rstat tag[2] stat[n]
	Twstat = 126,   // size[4] Twstat tag[2] fid[4] stat[n]
	Rwstat = 127,   // size[4] Rwstat tag[2]
	NOTAG = 0,
	ONLYTAG = 1,
};

#define READ16LE(S) ((255 & (S)[1]) << 8 | (255 & (S)[0]))
#define READ32LE(S) \
  ((uint32_t)(255 & (S)[3]) << 24 | (uint32_t)(255 & (S)[2]) << 16 | \
   (uint32_t)(255 & (S)[1]) << 8 | (uint32_t)(255 & (S)[0]))
#define READ64LE(S)                                                    \
  ((uint64_t)(255 & (S)[7]) << 070 | (uint64_t)(255 & (S)[6]) << 060 | \
   (uint64_t)(255 & (S)[5]) << 050 | (uint64_t)(255 & (S)[4]) << 040 | \
   (uint64_t)(255 & (S)[3]) << 030 | (uint64_t)(255 & (S)[2]) << 020 | \
   (uint64_t)(255 & (S)[1]) << 010 | (uint64_t)(255 & (S)[0]) << 000)
#define WRITE16LE(P, V)                        \
  ((P)[0] = (0x000000FF & (V)), \
   (P)[1] = (0x0000FF00 & (V)) >> 8, (P) + 2)
#define WRITE32LE(P, V)                        \
  ((P)[0] = (0x000000FF & (V)), \
   (P)[1] = (0x0000FF00 & (V)) >> 8, \
   (P)[2] = (0x00FF0000 & (V)) >> 16, \
   (P)[3] = (0xFF000000 & (V)) >> 24, (P) + 4)
#define WRITE64LE(P, V)                        \
  ((P)[0] = (0x00000000000000FF & (V)) >> 000, \
   (P)[1] = (0x000000000000FF00 & (V)) >> 010, \
   (P)[2] = (0x0000000000FF0000 & (V)) >> 020, \
   (P)[3] = (0x00000000FF000000 & (V)) >> 030, \
   (P)[4] = (0x000000FF00000000 & (V)) >> 040, \
   (P)[5] = (0x0000FF0000000000 & (V)) >> 050, \
   (P)[6] = (0x00FF000000000000 & (V)) >> 060, \
   (P)[7] = (0xFF00000000000000 & (V)) >> 070, (P) + 8)

char *Buf9;
uint32_t Max9;
char Err9[256];

char *smlBuf, *bigBuf;
uint32_t *smlBigAddrs, *bigSmlAddrs, *smlBigSizes, *bigSmlSizes;
uint16_t bigCnt;

volatile bool flag;

static bool Version9(uint32_t msize, char *version, uint32_t *retmsize, char *retversion);
static bool checkErr(char *msg);
static void putSmlGetBig(void);
static void putBigGetSml(void);
static bool allocBigBuffer(uint16_t maxbufs);
static int pageExtents(const uint32_t *pages, int npages, uint32_t *extbases, uint32_t *extsizes, int maxext);

bool Init9(uint16_t vioq, uint16_t viobuffers) {
	if (allocBigBuffer(viobuffers)) {
		return true;
	}

	char vers[128];
	if (Version9(Max9+11, "9P2000.u", &Max9, vers)) {
		return true;
	}
	Max9 -= 11; // subtract Rread/Twrite header from msize

	if (strcmp(vers, "9P2000.u")) {
		lprintf(".virtio9p: we offered 9P2000.u, server offered %s, cannot continue\n", vers);
		return true;
	}

	lprintf(".virtio9p: final msize = %d+11\n", Max9);

	return false;
}

// Keep this internal, as part of our initialization
static bool Version9(uint32_t msize, char *version, uint32_t *retmsize, char *retversion) {
	size_t slen = strlen(version);
	uint32_t size = 13 + slen;

	WRITE32LE(smlBuf, size);
	*(smlBuf+4) = Tversion;
	WRITE16LE(smlBuf+5, NOTAG);
	WRITE32LE(smlBuf+7, msize);
	WRITE16LE(smlBuf+11, slen);
	memcpy(smlBuf+13, version, slen);

	putSmlGetBig();

	if (checkErr(bigBuf)) {
		return true;
	}

	if (retmsize != NULL) {
		*retmsize = READ32LE(bigBuf+7);
	}

	if (retversion != NULL) {
		slen = READ16LE(bigBuf+11);
		memcpy(retversion, bigBuf+13, slen);
		retversion[slen] = 0;
	}

	return false;
}

bool Attach9(uint32_t fid, uint32_t afid, char *uname, char *aname, struct Qid9 *retqid) {
	size_t ulen = strlen(uname), alen = strlen(aname);
	uint32_t size = 4+1+2+4+4+2+ulen+2+alen+4;

	WRITE32LE(smlBuf, size);
	*(smlBuf+4) = Tattach;
	WRITE16LE(smlBuf+4+1, ONLYTAG);
	WRITE32LE(smlBuf+4+1+2, fid);
	WRITE32LE(smlBuf+4+1+2+4, afid);
	WRITE16LE(smlBuf+4+1+2+4+4, ulen);
	memcpy(smlBuf+4+1+2+4+4+2, uname, ulen);
	WRITE16LE(smlBuf+4+1+2+4+4+2+ulen, alen);
	memcpy(smlBuf+4+1+2+4+4+2+ulen+2, aname, alen);
	WRITE32LE(smlBuf+4+1+2+4+4+2+ulen+2+alen, 0);

	putSmlGetBig();

	if (checkErr(bigBuf)) {
		return true;
	}

	if (retqid != NULL) {
		retqid->type = bigBuf[7];
		retqid->version = READ32LE(bigBuf+7+1);
		retqid->path = READ64LE(bigBuf+7+1+4);
	}

	return false;
}

bool Walk9(uint32_t fid, uint32_t newfid, uint16_t nwname, const char **name, uint16_t *retnwqid, struct Qid9 *retqid) {
	*(bigBuf+4) = Twalk;
	WRITE16LE(bigBuf+4+1, ONLYTAG);
	WRITE32LE(bigBuf+4+1+2, fid);
	WRITE32LE(bigBuf+4+1+2+4, newfid);
	WRITE16LE(bigBuf+4+1+2+4+4, nwname);

	uint32_t size = 4+1+2+4+4+2;
	for (uint16_t i=0; i<nwname; i++) {
		uint16_t len = strlen(*name);
		WRITE16LE(bigBuf+size, len);
		size += 2;
		memcpy(bigBuf+size, *name, len);
		size += len;
		name++;
	}

	WRITE32LE(bigBuf, size);

	putBigGetSml();

	if (checkErr(smlBuf)) {
		if (retnwqid) *retnwqid=0;
		return true;
	}

	uint16_t got = READ16LE(smlBuf+7);

	if (retnwqid) *retnwqid=got;

	if (retqid != NULL) {
		for (uint16_t i=0; i<got; i++) {
			char *thisqid = smlBuf+9+13*i;
			retqid[i].type = *thisqid;
			retqid[i].version = READ32LE(thisqid+1);
			retqid[i].path = READ64LE(thisqid+1+4);
		}
	}

	return false;
}

bool Clunk9(uint32_t fid) {
	uint32_t size = 4+1+2+4;

	WRITE32LE(smlBuf, size);
	*(smlBuf+4) = Tclunk;
	WRITE16LE(smlBuf+4+1, ONLYTAG);
	WRITE32LE(smlBuf+4+1+2, fid);

	putSmlGetBig();

	return checkErr(bigBuf);
}

bool Stat9(uint32_t fid, struct Stat9 *retstat) {
	uint32_t size = 4+1+2+4;

	WRITE32LE(smlBuf, size);
	*(smlBuf+4) = Tstat;
	WRITE16LE(smlBuf+4+1, ONLYTAG);
	WRITE32LE(smlBuf+4+1+2, fid);

	putSmlGetBig();

	if (checkErr(bigBuf)) {
		return true;
	}

	// Debug only TODO
	{
		uint32_t len=READ32LE(bigBuf);
		for (int i=0; i<len; i++) {
			lprintf("%02x", (int)(unsigned char)bigBuf[i]);
		}
		lprintf("\n");
	}

	// Probably worth factoring out a Stat9 deserializer
	if (retstat != NULL) {
		char *ptr = bigBuf+17;
		retstat->qid.type = *ptr; ptr+=1;
		retstat->qid.version = READ32LE(ptr); ptr+=4;
		retstat->qid.path = READ64LE(ptr); ptr+=8;
		retstat->mode = READ32LE(ptr); ptr+=4;
		retstat->atime = READ32LE(ptr); ptr+=4;
		retstat->mtime = READ32LE(ptr); ptr+=4;
		retstat->length = READ64LE(ptr); ptr+=8;

		uint16_t slen, keep;
		slen = READ16LE(ptr); ptr+=2;
		keep = slen;
		if (keep>511) keep=511;
		memcpy(retstat->name, ptr, keep);
		retstat->name[keep] = 0;
		ptr += slen;

		ptr += 2 + READ16LE(ptr); // skip uid
		ptr += 2 + READ16LE(ptr); // skip gid
		ptr += 2 + READ16LE(ptr); // skip muid

		if (retstat->qid.type & 2) {
			slen = READ16LE(ptr); ptr+=2;
			keep = slen;
			if (keep>511) keep=511;
			memcpy(retstat->linktarget, ptr, keep);
			retstat->linktarget[keep] = 0;
			ptr += slen;
		} else {
			retstat->linktarget[0] = 0;
		}
	}

	return false;
}

bool Open9(uint32_t fid, uint8_t mode, struct Qid9 *retqid, uint32_t *retiounit) {
	uint32_t size = 4+1+2+4+1;

	WRITE32LE(smlBuf, size);
	*(smlBuf+4) = Topen;
	WRITE16LE(smlBuf+4+1, ONLYTAG);
	WRITE32LE(smlBuf+4+1+2, fid);
	*(smlBuf+4+1+2+4) = mode;

	putSmlGetBig();

	if (checkErr(bigBuf)) {
		return true;
	}

	if (retqid != NULL) {
		retqid->type = *(bigBuf+7);
		retqid->version = READ32LE(bigBuf+7+1);
		retqid->path = READ64LE(bigBuf+7+1+4);
	}

	if (retiounit != NULL) {
		*retiounit = READ32LE(bigBuf+20);
	}

	return false;
}

bool Create9(uint32_t fid, char *name, uint32_t perm, uint8_t mode, char *extn, struct Qid9 *retqid, uint32_t *retiounit) {
	size_t nlen = strlen(name), elen = strlen(extn);
	uint32_t size = 4+1+2+4+2+nlen+4+1+2+elen;

	WRITE32LE(smlBuf, size);
	*(smlBuf+4) = Tcreate;
	WRITE16LE(smlBuf+4+1, ONLYTAG);
	WRITE32LE(smlBuf+4+1+2, fid);
	WRITE16LE(smlBuf+4+1+2+4, nlen);
	memcpy(smlBuf+4+1+2+4+2, name, nlen);
	WRITE32LE(smlBuf+4+1+2+4+2+nlen, perm);
	*(smlBuf+4+1+2+4+2+nlen+4) = mode;
	WRITE16LE(smlBuf+4+1+2+4+2+nlen+4+1, elen);
	memcpy(smlBuf+4+1+2+4+2+nlen+4+1+2, extn, elen);

	putSmlGetBig();

	if (checkErr(bigBuf)) {
		return true;
	}

	if (retqid != NULL) {
		retqid->type = *(bigBuf+7);
		retqid->version = READ32LE(bigBuf+7+1);
		retqid->path = READ64LE(bigBuf+7+1+4);
	}

	if (retiounit != NULL) {
		*retiounit = READ32LE(bigBuf+20);
	}

	return false;
}

// You can leave the data pointer empty, and peruse bigBuf, if you prefer
bool Read9(uint32_t fid, uint64_t offset, uint32_t count, uint32_t *actual_count) {
	if (actual_count != NULL) {
		*actual_count = 0;
	}

	uint32_t size = 4+1+2+4+8+4;

	WRITE32LE(smlBuf, size);
	*(smlBuf+4) = Tread;
	WRITE16LE(smlBuf+4+1, ONLYTAG);
	WRITE32LE(smlBuf+4+1+2, fid);
	WRITE64LE(smlBuf+4+1+2+4, offset);
	WRITE32LE(smlBuf+4+1+2+4+8, count);

	putSmlGetBig();

	if (checkErr(bigBuf)) {
		return true;
	}

	uint32_t got = READ32LE(bigBuf+4+1+2);

	if (actual_count != NULL) {
		*actual_count = got;
	}

	return false;
}

static bool checkErr(char *msg) {
	if (*(msg + 4) != Rerror) {
		return false;
	}

	uint16_t size = READ16LE(msg + 7);
	if (size > sizeof(Err9) - 1) {
		size = sizeof(Err9) - 1;
	}
	memcpy(Err9, msg + 9, size);
	Err9[size] = 0;
	lprintf(".virtio9p error: %s\n", Err9);
	return true;
}

static void putSmlGetBig(void) {
	flag = false;
	QSend(0, 1, bigCnt, smlBigAddrs, smlBigSizes, NULL);
	QNotify(0);
	while (!flag) {} // Change to WaitForInterrupt
}

static void putBigGetSml(void) {
	flag = false;
	QSend(0, bigCnt, 1, bigSmlAddrs, bigSmlSizes, NULL);
	QNotify(0);
	while (!flag) {} // Change to WaitForInterrupt
}

// Set these globals or return false: msize [tr]buf [trbufcnt] physbufs bufsizes
// Allocate a large system heap buffer laid out in logical space like this:
//        4085 bytes : fixed-size stuff                     <-- smlBuf
//          11 bytes : fits 11-byte header of Twrite/Rread  <-- bigBuf
//  (2^n)*4096 bytes : for payload of Twrite/Rread
// But the physical layout is a bit more exotic, suitable for QSend to use.
static bool allocBigBuffer(uint16_t maxbufs) {
	if (maxbufs > 256) maxbufs = 256;

	// An extra page for the header, so transfers are power of 2
	size_t fileDataPages = 4096;
	uint32_t pages[fileDataPages+1]; // regrettably long array
	char *logical;

	int extcnt;
	static uint32_t extents[256], extsizes[256];

	for (;; fileDataPages /= 2) {
		if (fileDataPages == 0) {
			lprintf(".virtio9p: Not enough memory to start\n");
			return true;
		}

		lprintf(".virtio9p: Allocating %d+1 pages: ", fileDataPages);
		logical = AllocPages(fileDataPages+1, pages);

		if (logical == NULL) {
			lprintf("not enough logical memory\n");
			continue;
		}

		// Consolidate pages into extents
		extcnt = pageExtents(pages, fileDataPages+1, extents+1, extsizes+1, maxbufs-2);
		if (extcnt == -1) {
			FreePages(logical);
			lprintf("too physically fragmented\n");
			continue;
		}

		break;
	}

	lprintf("ok\n");

	Max9 = 4096*fileDataPages; // Largest message we will tx/rx

	smlBuf = logical;
	bigBuf = logical + 4096 - 11;
	Buf9 = bigBuf + 11; // reads/writes go, page-aligned

	// Tweak the extent list so it can be viewed differently via diff offsets
	extents[extcnt+1] = extents[0] = extents[1];
	extents[1] += 4096 - 11;

	extsizes[extcnt+1] = extsizes[0] = 4096 - 11;
	extsizes[1] -= 4096 - 11;

	bigCnt = extcnt;
	smlBigAddrs = extents;
	bigSmlAddrs = extents + 1;
	smlBigSizes = extsizes;
	bigSmlSizes = extsizes + 1;

	lprintf("  msize = %d+11\n", Max9);
	lprintf("  smlBuf = %#08x, bigBuf = %#08x\n", smlBuf, bigBuf);

	lprintf("  smlBigAddrs/Sizes =");
	for (int i=0; i<bigCnt+1; i++) {
		lprintf(" %#08x/%#x", smlBigAddrs[i], smlBigSizes[i]);
	}
	lprintf("\n");

	lprintf("  bigSmlAddrs/Sizes =");
	for (int i=0; i<bigCnt+1; i++) {
		lprintf(" %#08x/%#x", bigSmlAddrs[i], bigSmlSizes[i]);
	}
	lprintf("\n");

	return false;
}

// Shrink a list of 4096-byte pages to a list of n-byte extents
// -1 = bad, otherwise a number of extents
static int pageExtents(const uint32_t *pages, int npages, uint32_t *extbases, uint32_t *extsizes, int maxext) {
	int extcnt = 0;
	for (int i=0; i<npages; i++) {
		if (i>0 && pages[i-1]+4096==pages[i]) {
			extsizes[extcnt-1] += 4096;
		} else {
			if (extcnt == maxext) {
				return -1; // failure
			}

			extbases[extcnt] = pages[i];
			extsizes[extcnt] = 4096;
			extcnt++;
		}
	}
	return extcnt;
}

void DNotified(uint16_t q, uint16_t buf, size_t len, void *tag) {
	QFree(q, buf);
	flag = true;
}

void DConfigChange(void) {
}
