/*
Calls TODO:
write
statfs
lopen
lcreate
getattr
setattr
readdir
fsync
mkdir
renameat
unlinkat

remove (wontfix use unlinkat instead)
rename (wontfix use renameat instead)
flush (wontfix)
auth (wontfix)
xattrwalk (wontfix)
xattrcreate (wontfix)
mknod (wontfix)
link (wontfix)
symlink (?wontfix)
readlink (?wontfix)
lock (?wontfix)
getlock (?wontfix)

Tstatfs = 8
Tlcreate = 14
Tsymlink = 16
Tmknod = 18
Trename = 20
Treadlink = 22
Tsetattr = 26
Txattrwalk = 30
Txattrcreate = 32
Tfsync = 50
Tlock = 52
Tgetlock = 54
Tlink = 70
Tmkdir = 72
Trenameat = 74
Tunlinkat = 76
Tauth = 102
Tflush = 108
Topen = 112
Tcreate = 114
Twrite = 118
Tremove = 122
Tstat = 124
Twstat = 126
*/

#include <string.h>

#include "allocator.h"
#include "lprintf.h"
#include "virtqueue.h"

#include "device.h" // implement DNotified and DConfigChange here instead
#include "rpc9p.h"

enum {
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
uint32_t Err9;

char *smlBuf, *bigBuf;
uint32_t *smlBigAddrs, *bigSmlAddrs, *smlBigSizes, *bigSmlSizes;
uint16_t bigCnt;

static uint32_t openfids;

volatile bool flag;

static uint32_t curreadfid = (uint32_t)NOFID;
#define INVAL_READDIR() {curreadfid = (uint32_t)NOFID;}

#define QIDF "0x%02x.%x.%x"
#define QIDA(qid) qid.type, qid.version, (uint32_t)qid.path
#define READQID(ptr) (struct Qid9){*(char *)(ptr), READ32LE((char *)(ptr)+1), READ64LE((char *)(ptr)+5)}

static bool Version9(uint32_t msize, char *version, uint32_t *retmsize, char *retversion);
static bool checkErr(char *msg);
static void putSmlGetBig(void);
static void putBigGetSml(void);
static bool allocBigBuffer(uint16_t maxbufs);
static int pageExtents(const uint32_t *pages, int npages, uint32_t *extbases, uint32_t *extsizes, int maxext);
static const char *errname(uint32_t errno);

bool Init9(uint16_t vioq, uint16_t viobuffers) {
	if (allocBigBuffer(viobuffers)) {
		return true;
	}

	char vers[128];
	if (Version9(Max9+11, "9P2000.L", &Max9, vers)) {
		return true;
	}
	Max9 -= 11; // subtract Rread/Twrite header from msize

	if (strcmp(vers, "9P2000.L")) {
		lprintf(".virtio9p: we offered 9P2000.L, server offered %s, cannot continue\n", vers);
		return true;
	}

	lprintf(".virtio9p: final msize = %d+11\n", Max9);

	return false;
}

// Keep this internal, as part of our initialization
static bool Version9(uint32_t msize, char *version, uint32_t *retmsize, char *retversion) {
	enum {Tversion = 100}; // size[4] Tversion tag[2] msize[4] version[s]
	enum {Rversion = 101}; // size[4] Rversion tag[2] msize[4] version[s]

	size_t slen = strlen(version);
	uint32_t size = 13 + slen;

	WRITE32LE(smlBuf, size);
	*(smlBuf+4) = Tversion;
	WRITE16LE(smlBuf+5, NOTAG);
	WRITE32LE(smlBuf+7, msize);
	WRITE16LE(smlBuf+11, slen);
	memcpy(smlBuf+13, version, slen);

	putSmlGetBig();

	lprintf("Tversion(msize=%lu ver=%s)", msize, version);

	if (checkErr(bigBuf)) {
		return true;
	}

	lprintf(" -> (msize=%lu ver=%.*s)\n",
		READ32LE(bigBuf+7), READ16LE(bigBuf+11), bigBuf+13);

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
	enum {Tattach = 104}; // size[4] Tattach tag[2] fid[4] afid[4] uname[s] aname[s] !UNIX n_uname[4]
	enum {Rattach = 105}; // size[4] Rattach tag[2] qid[13]

	INVAL_READDIR();

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
	WRITE32LE(smlBuf+4+1+2+4+4+2+ulen+2+alen, 0); // numeric n_uname

	lprintf("Tattach(fid=%lu afid=%lu uname=%s aname=%s)",
		fid, afid, uname, aname);

	putSmlGetBig();

	if (checkErr(bigBuf)) {
		return true;
	}

	struct Qid9 qid = READQID(bigBuf+7);
	lprintf(" -> (qid="QIDF")\n", QIDA(qid));

	if (retqid != NULL) *retqid = qid;

	return false;
}

bool Walk9(uint32_t fid, uint32_t newfid, uint16_t nwname, const char **name, uint16_t *retnwqid, struct Qid9 *retqid) {
	enum {Twalk = 110}; // size[4] Twalk tag[2] fid[4] newfid[4] nwname[2] nwname*(wname[s])
	enum {Rwalk = 111}; // size[4] Rwalk tag[2] nwqid[2] nwqid*(wqid[13])

	INVAL_READDIR();

	if (newfid < 32 && fid != newfid && (openfids & (1<<newfid))) Clunk9(newfid);

	lprintf("Twalk(fid=%lu nfid=%lu wname=\"", fid, newfid);

	*(bigBuf+4) = Twalk;
	WRITE16LE(bigBuf+4+1, ONLYTAG);
	WRITE32LE(bigBuf+4+1+2, fid);
	WRITE32LE(bigBuf+4+1+2+4, newfid);
	WRITE16LE(bigBuf+4+1+2+4+4, nwname);

	uint32_t size = 4+1+2+4+4+2;
	for (uint16_t i=0; i<nwname; i++) {
		lprintf("/%s", *name);
		uint16_t len = strlen(*name);
		WRITE16LE(bigBuf+size, len);
		size += 2;
		memcpy(bigBuf+size, *name, len);
		size += len;
		name++;
	}

	WRITE32LE(bigBuf, size);

	lprintf("\")");

	putBigGetSml();

	if (checkErr(smlBuf)) {
		if (retnwqid) *retnwqid=0;
		return true;
	}

	uint16_t got = READ16LE(smlBuf+7);

	lprintf(" -> (%u/%u)\n", got, nwname);

	if (retnwqid) *retnwqid=got;

	if (retqid != NULL) {
		for (uint16_t i=0; i<got; i++) {
			retqid[i] = READQID(smlBuf+9+13*i);
		}
	}

	if (newfid < 32) openfids |= 1<<newfid;

	return false;
}

bool Lopen9(uint32_t fid, uint32_t flags, struct Qid9 *retqid, uint32_t *retiounit) {
	enum {Tlopen = 12}; // size[4] Tlopen tag[2] fid[4] flags[4]
	enum {Rlopen = 13}; // size[4] Rlopen tag[2] qid[13] iounit[4]

	INVAL_READDIR();

	uint32_t size = 4+1+2+4+4;

	WRITE32LE(smlBuf, size);
	*(smlBuf+4) = Tlopen;
	WRITE16LE(smlBuf+4+1, ONLYTAG);
	WRITE32LE(smlBuf+4+1+2, fid);
	WRITE32LE(smlBuf+4+1+2+4, flags);

	lprintf("Tlopen(fid=%lu flags=%lu)", fid, flags);

	putSmlGetBig();

	if (checkErr(bigBuf)) {
		return true;
	}

	struct Qid9 qid = READQID(bigBuf+4+1+2);
	uint32_t iounit = READ32LE(bigBuf+4+1+2+13);

	lprintf(" -> (qid="QIDF" iounit=%lu)\n", QIDA(qid), iounit);

	if (retqid != NULL) *retqid = qid;
	if (retiounit != NULL) *retiounit = iounit;

	return false;
}

// -1=err, 0=ok, 1=eof
// global storage invalidated by INVAL_READDIR()
char Readdir9(uint32_t fid, struct Qid9 *retqid, char *rettype, char retname[512]) {
	enum {Treaddir = 40}; // size[4] Treaddir tag[2] fid[4] offset[8] count[4]
	enum {Rreaddir = 41}; // size[4] Rreaddir tag[2] count[4] data[count]
	// data consists of: qid[13] offset[8] type[1] name[s]

	static uint64_t requestoffset;
	static uint32_t cursor, count;

	if (fid != curreadfid) {
		curreadfid = fid;
		requestoffset = 0;
		count = 0;
	}

	if (cursor>=count) {
		uint32_t size = 4+1+2+4+8+4;

		WRITE32LE(smlBuf, size);
		*(smlBuf+4) = Treaddir;
		WRITE16LE(smlBuf+4+1, ONLYTAG);
		WRITE32LE(smlBuf+4+1+2, curreadfid);
		WRITE64LE(smlBuf+4+1+2+4, requestoffset);
		WRITE32LE(smlBuf+4+1+2+4+8, Max9);

		lprintf("Treaddir(fid=%lu)", fid);

		putSmlGetBig();

		if (checkErr(bigBuf)) {
			return -1;
		}

		cursor = 0;
		count = READ32LE(bigBuf+7);

		lprintf(" -> (count=%lu)\n", count);

		if (count==0) {
			return 1; // eof
		}
	}

	char *entry = bigBuf+11+cursor;

	struct Qid9 qid = READQID(entry);
	requestoffset = READ64LE(entry+13);
	unsigned char type = *(entry+13+8);
	uint16_t nlen = READ16LE(entry+13+8+1);

	cursor += 13+8+1+2+nlen;

	if (retqid != NULL) *retqid = qid;
	if (rettype != NULL) *rettype = type;

	if (retname != NULL) {
		if (nlen > 511) nlen = 511;
		memcpy(retname, entry+13+8+1+2, nlen);
		retname[nlen] = 0;
	}

	return 0;
}

// Vastly simplify to qid, size, ctime_sec
bool Getattr9(uint32_t fid, struct Qid9 *retqid, uint64_t *retsize, uint64_t *rettime) {
	enum {Tgetattr = 24}; // size[4] Tgetattr tag[2] fid[4] request_mask[8]
	enum {Rgetattr = 25}; // size[4] Rgetattr tag[2] valid[8] qid[13]
	                      // mode[4] uid[4] gid[4] nlink[8] rdev[8]
	                      // size[8] blksize[8] blocks[8] atime_sec[8]
	                      // atime_nsec[8] mtime_sec[8] mtime_nsec[8]
	                      // ctime_sec[8] ctime_nsec[8] btime_sec[8]
	                      // btime_nsec[8] gen[8] data_version[8]

	INVAL_READDIR();

	uint32_t size = 4+1+2+4+8;

	WRITE32LE(smlBuf, size);
	*(smlBuf+4) = Tgetattr;
	WRITE16LE(smlBuf+4+1, ONLYTAG);
	WRITE32LE(smlBuf+4+1+2, fid);
	WRITE64LE(smlBuf+4+1+2+4, 0x200 /*size*/ | 0x80 /*ctime*/);

	lprintf("Tgetattr(fid=%lu)", fid);

	putSmlGetBig();

	if (checkErr(bigBuf)) {
		return true;
	}

	// Skip checking the valid-mask

	struct Qid9 qid = READQID(bigBuf+15);
	uint64_t filesize = READ64LE(bigBuf+56);
	uint64_t time = READ64LE(bigBuf+112);

	lprintf(" -> (qid="QIDF" size=%lu time=%lu)\n", QIDA(qid), (uint32_t)filesize, (uint32_t)time);

	if (retqid != NULL) *retqid = qid;
	if (retsize != NULL) *retsize = filesize;
	if (rettime != NULL) *rettime = time;

	return false;
}

bool Clunk9(uint32_t fid) {
	enum {Tclunk = 120}; // size[4] Tclunk tag[2] fid[4]
	enum {Rclunk = 121}; // size[4] Rclunk tag[2]

	INVAL_READDIR();

	if (fid < 32) openfids &= ~(1<<fid);

	uint32_t size = 4+1+2+4;

	WRITE32LE(smlBuf, size);
	*(smlBuf+4) = Tclunk;
	WRITE16LE(smlBuf+4+1, ONLYTAG);
	WRITE32LE(smlBuf+4+1+2, fid);

	lprintf("Tclunk(fid=%lu)", fid);

	putSmlGetBig();

	if (checkErr(bigBuf)) return true;

	lprintf(" -> (ok)\n");
}

// You can leave the data pointer empty, and peruse bigBuf, if you prefer
bool Read9(uint32_t fid, uint64_t offset, uint32_t count, uint32_t *actual_count) {
	enum {Tread = 116}; // size[4] Tread tag[2] fid[4] offset[8] count[4]
	enum {Rread = 117}; // size[4] Rread tag[2] count[4] data[count]

	INVAL_READDIR();

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

	lprintf("Tread(fid=%lu ofs=%lu cnt=%lu)", fid, (uint32_t)offset, count);

	putSmlGetBig();

	if (checkErr(bigBuf)) {
		lprintf(" -> (fail)\n");
		return true;
	}

	uint32_t got = READ32LE(bigBuf+4+1+2);

	if (actual_count != NULL) {
		*actual_count = got;
	}

	lprintf(" -> (ok)\n");

	return false;
}

static bool checkErr(char *msg) {
	enum {Rlerror = 7}; // size[4] Rlerror tag[2] ecode[4]

	if (*(msg + 4) != Rlerror) {
		return false;
	}

	Err9 = READ32LE(msg+4+1+2);

	lprintf(" -> Rerror(%lu %s)\n", Err9, errname(Err9));
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

static const char *errname(uint32_t errno) {
	switch (errno) {
		case 1: return "EPERM";
		case 2: return "ENOENT";
		case 3: return "ESRCH";
		case 4: return "EINTR";
		case 5: return "EIO";
		case 6: return "ENXIO";
		case 7: return "E2BIG";
		case 8: return "ENOEXEC";
		case 9: return "EBADF";
		case 10: return "ECHILD";
		case 11: return "EAGAIN";
		case 12: return "ENOMEM";
		case 13: return "EACCES";
		case 14: return "EFAULT";
		case 15: return "ENOTBLK";
		case 16: return "EBUSY";
		case 17: return "EEXIST";
		case 18: return "EXDEV";
		case 19: return "ENODEV";
		case 20: return "ENOTDIR";
		case 21: return "EISDIR";
		case 22: return "EINVAL";
		case 23: return "ENFILE";
		case 24: return "EMFILE";
		case 25: return "ENOTTY";
		case 26: return "ETXTBSY";
		case 27: return "EFBIG";
		case 28: return "ENOSPC";
		case 29: return "ESPIPE";
		case 30: return "EROFS";
		case 31: return "EMLINK";
		case 32: return "EPIPE";
		case 33: return "EDOM";
		case 34: return "ERANGE";
		case 35: return "EDEADLK";
		case 36: return "ENAMETOOLONG";
		case 37: return "ENOLCK";
		case 38: return "ENOSYS";
		case 39: return "ENOTEMPTY";
		case 40: return "ELOOP";
		case 41: return "EWOULDBLOCK";
		case 42: return "ENOMSG";
		case 43: return "EIDRM";
		case 44: return "ECHRNG";
		case 45: return "EL2NSYNC";
		case 46: return "EL3HLT";
		case 47: return "EL3RST";
		case 48: return "ELNRNG";
		case 49: return "EUNATCH";
		case 50: return "ENOCSI";
		case 51: return "EL2HLT";
		case 52: return "EBADE";
		case 53: return "EBADR";
		case 54: return "EXFULL";
		case 55: return "ENOANO";
		case 56: return "EBADRQC";
		case 57: return "EBADSLT";
		case 58: return "EDEADLOCK";
		case 59: return "EBFONT";
		case 60: return "ENOSTR";
		case 61: return "ENODATA";
		case 62: return "ETIME";
		case 63: return "ENOSR";
		case 64: return "ENONET";
		case 65: return "ENOPKG";
		case 66: return "EREMOTE";
		case 67: return "ENOLINK";
		case 68: return "EADV";
		case 69: return "ESRMNT";
		case 70: return "ECOMM";
		case 71: return "EPROTO";
		case 72: return "EMULTIHOP";
		case 73: return "EDOTDOT";
		case 74: return "EBADMSG";
		case 75: return "EOVERFLOW";
		case 76: return "ENOTUNIQ";
		case 77: return "EBADFD";
		case 78: return "EREMCHG";
		case 79: return "ELIBACC";
		case 80: return "ELIBBAD";
		case 81: return "ELIBSCN";
		case 82: return "ELIBMAX";
		case 83: return "ELIBEXEC";
		case 84: return "EILSEQ";
		case 85: return "ERESTART";
		case 86: return "ESTRPIPE";
		case 87: return "EUSERS";
		case 88: return "ENOTSOCK";
		case 89: return "EDESTADDRREQ";
		case 90: return "EMSGSIZE";
		case 91: return "EPROTOTYPE";
		case 92: return "ENOPROTOOPT";
		case 93: return "EPROTONOSUPPORT";
		case 94: return "ESOCKTNOSUPPORT";
		case 95: return "EOPNOTSUPP";
		case 96: return "EPFNOSUPPORT";
		case 97: return "EAFNOSUPPORT";
		case 98: return "EADDRINUSE";
		case 99: return "EADDRNOTAVAIL";
		case 100: return "ENETDOWN";
		case 101: return "ENETUNREACH";
		case 102: return "ENETRESET";
		case 103: return "ECONNABORTED";
		case 104: return "ECONNRESET";
		case 105: return "ENOBUFS";
		case 106: return "EISCONN";
		case 107: return "ENOTCONN";
		case 108: return "ESHUTDOWN";
		case 109: return "ETOOMANYREFS";
		case 110: return "ETIMEDOUT";
		case 111: return "ECONNREFUSED";
		case 112: return "EHOSTDOWN";
		case 113: return "EHOSTUNREACH";
		case 114: return "EALREADY";
		case 115: return "EINPROGRESS";
		case 116: return "ESTALE";
		case 117: return "EUCLEAN";
		case 118: return "ENOTNAM";
		case 119: return "ENAVAIL";
		case 120: return "EISNAM";
		case 121: return "EREMOTEIO";
		default: return "(unknown)";
	}
}

void DNotified(uint16_t q, uint16_t buf, size_t len, void *tag) {
	QFree(q, buf);
	flag = true;
}

void DConfigChange(void) {
}
