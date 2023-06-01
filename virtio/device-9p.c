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
	Tattach = 104,  // size[4] Tattach tag[2] fid[4] afid[4] uname[s] aname[s]
	Rattach = 105,  // size[4] Rattach tag[2] qid[13]
	Terror = 106,   // illegal
	Rerror = 107,   // size[4] Rerror tag[2] ename[s]
	Tflush = 108,   // size[4] Tflush tag[2] oldtag[2]
	Rflush = 109,   // size[4] Rflush tag[2]
	Twalk = 110,    // size[4] Twalk tag[2] fid[4] newfid[4] nwname[2] nwname*(wname[s])
	Rwalk = 111,    // size[4] Rwalk tag[2] nwqid[2] nwqid*(wqid[13])
	Topen = 112,    // size[4] Topen tag[2] fid[4] mode[1]
	Ropen = 113,    // size[4] Ropen tag[2] qid[13] iounit[4]
	Tcreate = 114,  // size[4] Tcreate tag[2] fid[4] name[s] perm[4] mode[1]
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
	ONLYTAG = 1,
};


#define READ16LE(S) ((255 & (S)[1]) << 8 | (255 & (S)[0]))
#define READ32LE(S) \
  ((uint32_t)(255 & (S)[3]) << 24 | (uint32_t)(255 & (S)[2]) << 16 | \
   (uint32_t)(255 & (S)[1]) << 8 | (uint32_t)(255 & (S)[0]))
#define WRITE16LE(P, V)                        \
  ((P)[0] = (0x000000FF & (V)), \
   (P)[1] = (0x0000FF00 & (V)) >> 8, (P) + 2)
#define WRITE32LE(P, V)                        \
  ((P)[0] = (0x000000FF & (V)), \
   (P)[1] = (0x0000FF00 & (V)) >> 8, \
   (P)[2] = (0x00FF0000 & (V)) >> 16, \
   (P)[3] = (0xFF000000 & (V)) >> 24, (P) + 4)


static OSStatus initialize(DriverInitInfo *info);
static OSStatus finalize(DriverFinalInfo *info);
static bool Version(uint32_t tx_msize, char *tx_version, uint32_t *rx_msize, char *rx_version);
static void SetErr(char *msg);

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

	char chosenVers[128];

	if (Version(MY_MSIZE, "9P2000.u", &msize, chosenVers)) {
		return paramErr;
	}

	lprintf("version returned %s\n", chosenVers);

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
	uint32_t size = slen + 11;

	WRITE32LE(b, size);
	b[4] = Tversion;
	WRITE16LE(b + 5, ONLYTAG);
	WRITE32LE(b + 7, tx_msize);
	WRITE16LE(b + 11, slen);
	memcpy(b + 13, tx_version, slen);

	flag = false;
	QSend(0, 1, 1, (uint32_t[2]){ppage, ppage + 2048}, (uint32_t[2]){2048, 2048}, NULL);
	QNotify(0);
	while (!flag) {}

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

static void SetErr(char *msg) {
	uint16_t size = (uint16_t)(msg[7]) | ((uint16_t)(msg[8]) << 16);
	if (size > sizeof(ErrStr) - 1) {
		size = sizeof(ErrStr) - 1;
	}
	memcpy(ErrStr, msg + 9, size);
	ErrStr[size] = 0;
	lprintf("9p error: %s\n", ErrStr);
}

void DNotified(uint16_t q, uint16_t buf, size_t len, void *tag) {
	QFree(q, buf);
	flag = true;
}

// Device-specific configuration struct has changed
void DConfigChange(void) {

}
