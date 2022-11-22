#include <Devices.h>
#include <DriverServices.h>
#include <LowMem.h>
#include <NameRegistry.h>
#include <PCI.h>
#include <Video.h>
#include <VideoServices.h>
#include <stdarg.h>
#include <string.h>

#include "viotransport.h"
#include "lprintf.h"

OSStatus DoDriverIO(AddressSpaceID spaceID, IOCommandID cmdID,
	IOCommandContents pb, IOCommandCode code, IOCommandKind kind);

static OSStatus initialize(DriverInitInfo *);
static OSStatus finalize(DriverFinalInfo *);
static void queueSizer(uint16_t queue, uint16_t *count, size_t *osize, size_t *isize);
static void setFields(void *dest, const char *fields, ...);
static void getFields(const void *src, const char *fields, ...);

#define MSIZE (128*1024)

enum {
	Tversion = 100, // size[4] Tversion tag[2] msize[4] version[s]
	Rversion = 101, // size[4] Rversion tag[2] msize[4] version[s]
	Tauth = 102,    // size[4] Tauth tag[2] afid[4] uname[s] aname[s]
	Rauth = 103,    // size[4] Rauth tag[2] aqid[13]
	Tattach = 104,  // illegal
	Rattach = 105,  // size[4] Rerror tag[2] ename[s]
	Terror = 106,   // size[4] Tflush tag[2] oldtag[2]
	Rerror = 107,   // size[4] Rflush tag[2]
	Tflush = 108,   // size[4] Tattach tag[2] fid[4] afid[4] uname[s] aname[s]
	Rflush = 109,   // size[4] Rattach tag[2] qid[13]
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

DriverDescription TheDriverDescription = {
	kTheDescriptionSignature,
	kInitialDriverDescriptor,
	"\ppci1af4,1009",
	0x00, 0x10, 0x80, 0x00, // v0.1
	kDriverIsLoadedUponDiscovery | kDriverIsOpenedUponLoad,
	"\p.virtio9p",
	0, 0, 0, 0, 0, 0, 0, 0, // reserved
	1, // nServices
	kServiceCategoryNdrvDriver, kNdrvTypeIsGeneric, 0x00, 0x10, 0x80, 0x00, //v0.1
};

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
	OSStatus err;

	err = VTInit(&info->deviceEntry, queueSizer, NULL /*queueRecv*/, NULL /*configChanged*/);
	if (err) return err;

	{
		uint8_t gotversion;
		uint32_t gotmsize;
		uint16_t gotstrlen;

		setFields(VTBuffers[0][0], "41242", 21, Tversion, 0xffff, MSIZE, 8);
		memcpy((char *)VTBuffers[0][0] + 13, "9P2000.u", 8);
		VTSend(0, 0);
		while (!VTDone(0)) {}

		getFields(VTBuffers[0][1], "....1..42", &gotversion, &gotmsize, &gotstrlen);
		if (gotversion != Rversion || gotmsize != MSIZE || gotstrlen != 8 ||
			memcmp((char *)VTBuffers[0][1] + 13, "9P2000.u", 8)) {
			lprintf("error\n");
			lprintf("gotversion %d gotmsize %d gotstrlen %d\n", gotversion, gotmsize, gotstrlen);
			return paramErr;
		}

		lprintf("ok\n");
	}

	return noErr;
}

static OSStatus finalize(DriverFinalInfo *info) {
	return noErr;
}

static void queueSizer(uint16_t queue, uint16_t *count, size_t *osize, size_t *isize) {
	*count = 2;
	*osize = 128*1024;
	*isize = 128*1024;
}

static void setFields(void *dest, const char *fields, ...) {
	char *destptr = dest;
    va_list args;
	char f;

    va_start(args, fields);

	while ((f = *fields++) != 0) {
		if (f == '.') {
			*destptr++ = 0;
		} else if (f == '1') {
			int arg = va_arg(args, int);
			*destptr++ = arg;
		} else if (f == '2') {
			int arg = va_arg(args, int);
			*destptr++ = arg;
			*destptr++ = arg >> 8;
		} else if (f == '4') {
			uint32_t arg = va_arg(args, uint32_t);
			*destptr++ = arg;
			*destptr++ = arg >> 8;
			*destptr++ = arg >> 16;
			*destptr++ = arg >> 24;
		}
	}

    va_end(args);
}

static void getFields(const void *src, const char *fields, ...) {
	const char *srcptr = src;
    va_list args;
	char f;

    va_start(args, fields);

	while ((f = *fields++) != 0) {

		if (f == '.') {
			srcptr++;
		} else if (f == '1') {
			char *arg = va_arg(args, char *);
			*arg = *srcptr++;
		} else if (f == '2') {
			uint16_t *arg = va_arg(args, uint16_t *);
			*arg =
				(uint16_t)srcptr[0] +
				((uint16_t)srcptr[1] << 8);
			srcptr += 2;
		} else if (f == '4') {
			uint32_t *arg = va_arg(args, uint32_t *);
			*arg =
				(uint32_t)srcptr[0] +
				((uint32_t)srcptr[1] << 8) +
				((uint32_t)srcptr[2] << 16) +
				((uint32_t)srcptr[3] << 24);
			srcptr += 4;
		}
	}

    va_end(args);
}
