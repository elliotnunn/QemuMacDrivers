#include <Devices.h>
#include <DriverServices.h>
#include <LowMem.h>
#include <NameRegistry.h>
#include <PCI.h>
#include <Video.h>
#include <VideoServices.h>
#include <stdarg.h>
#include <string.h>

#include "transport.h"
#include "virtqueue.h"
#include "allocator.h"
#include "lprintf.h"

#include "device.h"

#include <stdbool.h>

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
};

enum {
	MY_MSIZE = 128*1024,
	NOTAG = 0,
	ONLYTAG = 1,
	NOFID = 0xffffffff,
};

struct qid {
	char type;
	uint32_t version;
	uint64_t path;
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


static OSStatus initialize(DriverInitInfo *info);
static OSStatus finalize(DriverFinalInfo *info);
static bool Version(uint32_t tx_msize, char *tx_version, uint32_t *rx_msize, char *rx_version);
static bool Auth(uint32_t afid, char *uname, char *aname, struct qid *qid);
static bool Attach(uint32_t tx_fid, uint32_t tx_afid, char *tx_uname, char *tx_aname, struct qid *rx_qid);
static bool Walk(uint32_t tx_fid, uint32_t tx_newfid, char *tx_name, struct qid *rx_qid);
static bool Open(uint32_t tx_fid, uint8_t tx_mode, struct qid *rx_qid, uint32_t *rx_iounit);
static bool C_reate(uint32_t tx_fid, char *tx_name, uint32_t tx_perm, uint8_t tx_mode, char *tx_extn, struct qid *rx_qid, uint32_t *rx_iounit);
static bool Read(uint32_t tx_fid, uint64_t tx_offset, uint32_t count, void *rx_data, uint32_t *done_count);
static void SetErr(char *msg);
static int entrySizes(int bytes, const uint32_t *ptrTab, uint32_t *sizeTab);

DriverDescription TheDriverDescription = {
	kTheDescriptionSignature,
	kInitialDriverDescriptor,
	{"\x0cpci1af4,1009", {0x00, 0x10, 0x80, 0x00}}, // v0.1
	{kDriverIsLoadedUponDiscovery |
		kDriverIsOpenedUponLoad,
		"\x09.virtio9p"},
	{1, // nServices
	{{kServiceCategoryNdrvDriver, kNdrvTypeIsGeneric, {0x00, 0x10, 0x80, 0x00}}}} //v0.1
};

uint16_t bufcnt;
uint32_t msize;
void *lpage;
uint32_t ppage;
char ErrStr[256];
volatile bool flag;

OSStatus DoDriverIO(AddressSpaceID spaceID, IOCommandID cmdID,
	IOCommandContents pb, IOCommandCode code, IOCommandKind kind) {
	OSStatus err;

	switch (code) {
	case kInitializeCommand:
	case kReplaceCommand:
		err = initialize(pb.initialInfo);
		break;
	case kFinalizeCommand:
	case kSupersededCommand:
		err = finalize(pb.finalInfo);
		break;
	case kControlCommand:
		err = controlErr;
		break;
	case kStatusCommand:
		err = statusErr;
		break;
	case kOpenCommand:
	case kCloseCommand:
		err = noErr;
		break;
	default:
		err = paramErr;
		break;
	}

	// Return directly from every call
	if (kind & kImmediateIOCommandKind) {
		return err;
	} else {
		return IOCommandIsComplete(cmdID, err);
	}
}

static OSStatus initialize(DriverInitInfo *info) {
	lprintf_enable = true;

	OSStatus err;

	lprintf("9p: starting\n");

	// No need to signal FAILED if cannot communicate with device
	if (!VInit(&info->deviceEntry)) {
		lprintf("9p: failed VInit()\n");
		return paramErr;
	};

	if (!VFeaturesOK()) {
		lprintf("9p: failed VFeaturesOK()\n");
		return paramErr;
	}

	// All our descriptors point into this wired-down page
	lpage = AllocPages(1, &ppage);
	if (lpage == NULL) {
		lprintf("9p: failed AllocPages()\n");
		return paramErr;
	}

	// Cannot go any further without touching virtqueues, which requires DRIVER_OK
	VDriverOK();

	bufcnt = QInit(0, 256);
	if (bufcnt <= 2) {
		lprintf("9p: failed QInit()\n");
		return paramErr;
	}
	QInterest(0, 1);

	char vers[128];
	if (Version(MY_MSIZE, "9P2000.u", &msize, vers)) {
		return paramErr;
	}

	if (strcmp(vers, "9P2000.u")) {
		lprintf("9p: we offered 9P2000.u, server offered %s, cannot continue\n", vers);
		return paramErr;
	}

	lprintf("9p: msize %d\n", (long)msize);

	struct qid root = {0};
	uint32_t rootfid = 99;

	if (Attach(rootfid, NOFID /*afid=NOFID*/, "", "", &root)) {
		return paramErr;
	}

	lprintf("qid %d %x %08x%08x\n", root.type, root.version, (uint32_t)(root.path >> 32), (uint32_t)root.path);


	Walk(rootfid, rootfid+1, "builds", &root);
	lprintf("builds: qid %d %x %08x%08x\n", root.type, root.version, (uint32_t)(root.path >> 32), (uint32_t)root.path);

	uint32_t iounit = 0;
	Open(rootfid, 0, &root, &iounit);
	lprintf("iounit %d\n", iounit);

	char big[1000] = {};
	uint32_t succeeded = 0;
	Read(rootfid, 0, sizeof(big), big, &succeeded);
	lprintf("in my buffer at %08x got %d bytes:\n", big, succeeded);

	for (int i=0; i<succeeded; i++) {
		lprintf("%c", big[i] ? big[i] : ',');
	}
	lprintf("\n");

	return noErr;
}

static OSStatus finalize(DriverFinalInfo *info) {
	return noErr;
}

// These can be synchronous because the File Manager is synchronous.
// Which also means the tag won't get any use.
static bool Version(uint32_t tx_msize, char *tx_version, uint32_t *rx_msize, char *rx_version) {
	char *b = lpage;
	size_t slen = strlen(tx_version);
	uint32_t size = 13 + slen;

	WRITE32LE(b, size);
	b[4] = Tversion;
	WRITE16LE(b + 5, NOTAG);
	WRITE32LE(b + 7, tx_msize);
	WRITE16LE(b + 11, slen);
	memcpy(b + 13, tx_version, slen);

	flag = false;
	QSend(0, 1, 1, (uint32_t[2]){ppage, ppage + 2048}, (uint32_t[2]){size, 2048}, NULL);
	QNotify(0);
	while (!flag) {} // Change to WaitForInterrupt

	b = lpage + 2048;

	if (b[4] == Rerror) {
		SetErr(b);
		return true;
	}

	if (rx_msize != NULL) {
		*rx_msize = READ32LE(b + 7);
	}

	if (rx_version != NULL) {
		slen = READ16LE(b + 11);
		memcpy(rx_version, b + 13, slen);
		rx_version[slen] = 0;
	}

	return false;
}

static bool Auth(uint32_t afid, char *uname, char *aname, struct qid *qid) {
	// actually, don't implement
}

static bool Attach(uint32_t tx_fid, uint32_t tx_afid, char *tx_uname, char *tx_aname, struct qid *rx_qid) {
	char *b = lpage;
	size_t ulen = strlen(tx_uname), alen = strlen(tx_aname);
	uint32_t size = 4+1+2+4+4+2+ulen+2+alen+4;

	WRITE32LE(b, size);
	b[4] = Tattach;
	WRITE16LE(b+4+1, ONLYTAG);
	WRITE32LE(b+4+1+2, tx_fid);
	WRITE32LE(b+4+1+2+4, tx_afid);
	WRITE16LE(b+4+1+2+4+4, ulen);
	memcpy(b+4+1+2+4+4+2, tx_uname, ulen);
	WRITE16LE(b+4+1+2+4+4+2+ulen, alen);
	memcpy(b+4+1+2+4+4+2+ulen+2, tx_aname, alen);
	WRITE32LE(b+4+1+2+4+4+2+ulen+2+alen, 0);

	flag = false;
	QSend(0, 1, 1, (uint32_t[2]){ppage, ppage + 2048}, (uint32_t[2]){size, 20}, NULL);
	QNotify(0);
	while (!flag) {} // Change to WaitForInterrupt

	b = lpage + 2048;

	if (b[4] == Rerror) {
		SetErr(b);
		return true;
	}

	if (rx_qid != NULL) {
		rx_qid->type = b[7];
		rx_qid->version = READ32LE(b+7+1);
		rx_qid->path = READ64LE(b+7+1+4);
	}

	return false;
}

// Convenient just to enforce "one path element"
static bool Walk(uint32_t tx_fid, uint32_t tx_newfid, char *tx_name, struct qid *rx_qid) {
	char *b = lpage;
	size_t nlen = strlen(tx_name);
	uint32_t size = 4+1+2+4+4+2+2+nlen;

	WRITE32LE(b, size);
	b[4] = Twalk;
	WRITE16LE(b+4+1, ONLYTAG);
	WRITE32LE(b+4+1+2, tx_fid);
	WRITE32LE(b+4+1+2+4, tx_newfid);
	WRITE16LE(b+4+1+2+4+4, 1); // nwname
	WRITE16LE(b+4+1+2+4+4+2, nlen);
	   memcpy(b+4+1+2+4+4+2+2, tx_name, nlen);

	flag = false;
	QSend(0, 1, 1, (uint32_t[2]){ppage, ppage + 2048}, (uint32_t[2]){size, 2048}, NULL);
	QNotify(0);
	while (!flag) {} // Change to WaitForInterrupt

	b = lpage + 2048;

	if (b[4] == Rerror) {
		SetErr(b);
		return true;
	}

	if (rx_qid != NULL) {
		rx_qid->type = b[9];
		rx_qid->version = READ32LE(b+9+1);
		rx_qid->path = READ64LE(b+9+1+4);
	}

	return false;
}

static bool Open(uint32_t tx_fid, uint8_t tx_mode, struct qid *rx_qid, uint32_t *rx_iounit) {
	char *b = lpage;
	uint32_t size = 4+1+2+4+1;

	WRITE32LE(b, size);
	*(b+4) = Topen;
	WRITE16LE(b+4+1, ONLYTAG);
	WRITE32LE(b+4+1+2, tx_fid);
	*(b+4+1+2+4) = tx_mode;

	flag = false;
	QSend(0, 1, 1, (uint32_t[2]){ppage, ppage + 2048}, (uint32_t[2]){size, 24}, NULL);
	QNotify(0);
	while (!flag) {} // Change to WaitForInterrupt

	b = lpage + 2048;

	if (b[4] == Rerror) {
		SetErr(b);
		return true;
	}

	if (rx_qid != NULL) {
		rx_qid->type = *(b+7);
		rx_qid->version = READ32LE(b+7+1);
		rx_qid->path = READ64LE(b+7+1+4);
	}

	if (rx_iounit != NULL) {
		*rx_iounit = READ32LE(b+20);
	}

	return false;
}

static bool C_reate(uint32_t tx_fid, char *tx_name, uint32_t tx_perm, uint8_t tx_mode, char *tx_extn, struct qid *rx_qid, uint32_t *rx_iounit) {
	char *b = lpage;
	size_t nlen = strlen(tx_name), elen = strlen(tx_extn);
	uint32_t size = 4+1+2+4+2+nlen+4+1+2+elen;

	WRITE32LE(b, size);
	*(b+4) = Tcreate;
	WRITE16LE(b+4+1, ONLYTAG);
	WRITE32LE(b+4+1+2, tx_fid);
	WRITE16LE(b+4+1+2+4, nlen);
	memcpy(b+4+1+2+4+2, tx_name, nlen);
	WRITE32LE(b+4+1+2+4+2+nlen, tx_perm);
	*(b+4+1+2+4+2+nlen+4) = tx_mode;
	WRITE16LE(b+4+1+2+4+2+nlen+4+1, elen);
	memcpy(b+4+1+2+4+2+nlen+4+1+2, tx_extn, elen);

	flag = false;
	QSend(0, 1, 1, (uint32_t[2]){ppage, ppage + 2048}, (uint32_t[2]){size, 24}, NULL);
	QNotify(0);
	while (!flag) {} // Change to WaitForInterrupt

	b = lpage + 2048;

	if (b[4] == Rerror) {
		SetErr(b);
		return true;
	}

	if (rx_qid != NULL) {
		rx_qid->type = *(b+7);
		rx_qid->version = READ32LE(b+7+1);
		rx_qid->path = READ64LE(b+7+1+4);
	}

	if (rx_iounit != NULL) {
		*rx_iounit = READ32LE(b+20);
	}

	return false;
}

// Ugh! Much complexity to work around here (at least we don't scatter-gather).
static bool Read(uint32_t tx_fid, uint64_t tx_offset, uint32_t count, void *rx_data, uint32_t *done_count) {
	*done_count = 0;

	int maxpages = bufcnt - 2;

	// actually, limit to the number of virtio buffers we command, bufcnt-2
	uint32_t physicals[256] = {ppage, ppage + 2048};
	uint32_t sizes[256] = {23, 11};

	struct IOPreparationTable prep = {
		.options = kIOLogicalRanges,
		.addressSpace = kCurrentAddressSpaceID,
		.granularity = 0,
		.firstPrepared = 0,
		.mappingEntryCount = maxpages,
		.logicalMapping = NULL,
		.physicalMapping = (void *)(physicals + 2),
		.rangeInfo = {.range = {.base = rx_data, .length = count}},
	};

	while (*done_count < count) {
		// Lock down as much of the receive buffer as possible
		// Bad arguments should never happen
		if (PrepareMemoryForIO(&prep) != noErr) {
			return true;
		}

		lprintf("Prepared %d of requested %d bytes\n", prep.lengthPrepared, count);
		lprintf("state = %d\n", prep.state);

		int bufcnt = entrySizes(prep.lengthPrepared, physicals + 2, sizes + 2) + 2;

		lprintf("buf sizes:\n");
		for (int i=0; i<bufcnt; i++) {
			lprintf("%08x/%d ", physicals[i], sizes[i]);
		}
		lprintf("\n");

		char *b = lpage;
		WRITE32LE(b, sizes[0]);
		*(b+4) = Tread;
		WRITE16LE(b+4+1, ONLYTAG);
		WRITE32LE(b+4+1+2, tx_fid);
		WRITE64LE(b+4+1+2+4, tx_offset);
		WRITE32LE(b+4+1+2+4+8, prep.lengthPrepared);

		lprintf("message:");
		for (int i=0; i<sizes[0]; i++) {
			lprintf(" %02x", b[i]);
		}
		lprintf("\n");

		flag = false;
		QSend(0, 1, bufcnt-1, physicals, sizes, NULL);
		QNotify(0);
		while (!flag) {} // Change to WaitForInterrupt

		b = lpage + 2048;

		if (b[4] == Rerror) {
			lprintf("%c%c%s\n", b[9], b[10], rx_data);
			// slightly tricky job here...
			SetErr(b);
			return true;
		}

		uint32_t got = READ32LE(b + 7);
		lprintf("Got %d bytes of the %d prepared\n", got, prep.lengthPrepared);

		*done_count += got;
		CheckpointIO(prep.preparationID, 0);
		break;
	}

	return false;
}

static void SetErr(char *msg) {
	uint16_t size = (uint16_t)(msg[7]) | ((uint16_t)(msg[8]) << 16);
	if (size > sizeof(ErrStr) - 1) {
		size = sizeof(ErrStr) - 1;
	}
	memcpy(ErrStr, msg + 9, size);
	ErrStr[size] = 0;
	lprintf("9p error: %s\n", ErrStr);
}

static int entrySizes(int bytes, const uint32_t *ptrTab, uint32_t *sizeTab) {
	int count = 0;

	while (bytes) {
		// The first entry might go from mid-page to end-page
		if (*ptrTab % 4096) {
			*sizeTab = 4096 - (*ptrTab % 4096);
		} else {
			*sizeTab = 4096;
		}

		// The last entry might go from start-page to mid-page
		if (*sizeTab > bytes) {
			*sizeTab = bytes;
		}

		bytes -= *sizeTab;
		count++;
		ptrTab++;
		sizeTab++;
	}

	return count;
}

void DNotified(uint16_t q, uint16_t buf, size_t len, void *tag) {
	QFree(q, buf);
	flag = true;
}

// Device-specific configuration struct has changed
void DConfigChange(void) {

}
