#include <DriverServices.h>
#include <PCI.h>
#include <Types.h>
#include <Video.h>
#include <VideoServices.h>

#include <string.h>

#include "hardwarecursor.h"

#include "viotransport.h"
#include "virtio-gpu-structs.h"
#include "lprintf.h"

#define CURSOR_RESOURCE 0xcc

static void *cursorPic;
static int cursorIsArrow;
static int cursorSet;
static int cursorVisible;
static int cursorX, cursorY;
static int hotX = 1, hotY = 1;

struct CursorColorTable {
	long ctSeed;
	short ctFlags;
	short ctSize;
	ColorSpec ctTable[256];
};
static struct CursorColorTable cursColorTab;

void InitHardwareCursor(void) {
	cursorPic = PoolAllocateResident(64 * 64 * 4, true);

	// Use the controlq (0) to create a 64x64 cursor resource
	{
		struct virtio_gpu_resource_create_2d *buf = (void *)VTBuffers[0][0];
		memset(buf, 0, sizeof(*buf));
		buf->hdr.le32_type = EndianSwap32Bit(VIRTIO_GPU_CMD_RESOURCE_CREATE_2D);
		buf->hdr.le32_flags = EndianSwap32Bit(VIRTIO_GPU_FLAG_FENCE);
		buf->le32_resource_id = EndianSwap32Bit(CURSOR_RESOURCE);
		buf->le32_format = EndianSwap32Bit(VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM);
		buf->le32_width = EndianSwap32Bit(64);
		buf->le32_height = EndianSwap32Bit(64);

		VTSend(0, 0);
		while (!VTDone(0)) {}
	}

	// Attach some backing memory (TODO: allocate the memory more reliably)
	{
		struct virtio_gpu_resource_attach_backing *buf = (void *)VTBuffers[0][0];
		struct virtio_gpu_mem_entry *buf2 = (void *)((char *)buf + sizeof(*buf));
		memset(buf, 0, sizeof(*buf) + sizeof(*buf2));
		buf->hdr.le32_type = EndianSwap32Bit(VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING);
		buf->hdr.le32_flags = EndianSwap32Bit(VIRTIO_GPU_FLAG_FENCE);
		buf->le32_resource_id = EndianSwap32Bit(CURSOR_RESOURCE);
		buf->le32_nr_entries = EndianSwap32Bit(1);

		buf2->le32_addr = EndianSwap32Bit((uint32_t)cursorPic + 0x4000);
		buf2->le32_length = EndianSwap32Bit(64*1024*1024);

		VTSend(0, 0);
		while (!VTDone(0)) {}
	}
}

// Graphics drivers that support hardware cursors must return true.
// <-- csSupportsHardwareCursor  true if hardware cursor is supported
OSStatus SupportsHardwareCursor(VDSupportsHardwareCursorRec *rec) {
	rec->csSupportsHardwareCursor = 1;
	return noErr;
}

// QuickDraw uses the SetHardwareCursor control call to set up the hardware
// cursor and determine whether the hardware can support it. The driver
// must determine whether it can support the given cursor and, if so,
// program the hardware cursor frame buffer (or equivalent), set up the
// CLUT, and return noErr. If the driver cannot support the cursor it must
// return controlErr. The driver must remember whether this call was
// successful for subsequent GetHardwareCursorDrawState or
// DrawHardwareCursor calls, but should not change the cursor’s x or y
// coordinates or its visible state.
//  --> csCursorRef    Reference to cursor data
OSStatus SetHardwareCursor(VDSetHardwareCursorRec *rec) {
	HardwareCursorDescriptorRec desc = {0};
	HardwareCursorInfoRec curs = {0};
	uint32_t colourCodes[255]; // code 255 is reserved for transparent
	uint32_t cursorHash;
	int i;
	int ok;
	int x, y;

	return paramErr;

	for (i=0; i<sizeof(colourCodes)/sizeof(*colourCodes); i++) {
		colourCodes[i] = i;
	}

	desc.majorVersion = kHardwareCursorDescriptorMajorVersion;
	desc.minorVersion = kHardwareCursorDescriptorMinorVersion;
	desc.height = 64;
	desc.width = 64;
	desc.bitDepth = 32;
	desc.maskBitDepth = 0; // unused, reserved
	desc.numColors = 255;
	desc.colorEncodings = colourCodes;
	desc.flags = 0;
	desc.supportedSpecialEncodings = kTransparentEncodedPixel;
	desc.specialEncodings[0] = 255; // reserved value for transparent pixel

	curs.majorVersion = kHardwareCursorInfoMajorVersion;
	curs.minorVersion = kHardwareCursorInfoMinorVersion;
	curs.cursorHeight = 0;
	curs.cursorWidth = 0;
	curs.colorMap = (ColorTable *)&cursColorTab;
	curs.hardwareCursor = cursorPic;

	ok = VSLPrepareCursorForHardwareCursor(rec->csCursorRef, &desc, &curs);
	if (!ok) {
		cursorSet = 0;
		return controlErr;
	}

	cursorHash = (curs.cursorWidth << 16) | curs.cursorHeight;
	for (i=0; i<curs.cursorWidth*curs.cursorHeight; i++) {
		cursorHash = (cursorHash << 5) - cursorHash + 197;
		cursorHash ^= *((uint32_t *)cursorPic + i);
	}
	cursorIsArrow = (cursorHash == 0xd29cd063);

	// Need to stretch out the row stride to match 64 pixels x 4 bytes
	// and ensure that all other pixels are transparent
	for (y=64-1; y>=0; y--) {
		uint32_t *oldRow = (uint32_t *)cursorPic + y*curs.cursorWidth;
		uint32_t *newRow = (uint32_t *)cursorPic + y*64;
		uint32_t rowData[64] = {0}; // zero alpha channel

		if (y < curs.cursorHeight) {
			memcpy(rowData, oldRow, curs.cursorWidth*sizeof(uint32_t));
		}

		memcpy(newRow, rowData, sizeof(rowData));
	}

	for (y=0; y<curs.cursorHeight; y++) {
		for (x=0; x<curs.cursorWidth; x++) {
			uint32_t *pixel = (uint32_t *)cursorPic + y*64 + x;

			if (*pixel == 255) { // reserved transparent value
				*pixel = 0; // zero alpha channel
			} else {
				RGBColor *rgb = &cursColorTab.ctTable[*pixel & 255].rgb;

				// TODO: understand what gamma correction is needed!
				*pixel = 0xff000000 |
					(((rgb->red >> 8) & 0xff) << 16) |
					(((rgb->green >> 8) & 0xff) << 8) |
					((rgb->blue >> 8) & 0xff);
			}
		}
	}

	// Use VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D to update the host resource from guest memory.
	{
		struct virtio_gpu_transfer_to_host_2d *buf = (void *)VTBuffers[0][0];
		memset(buf, 0, sizeof(*buf));
		//memset(VTBuffers[1][1], 0x88, sizeof(*buf));
		buf->hdr.le32_type = EndianSwap32Bit(VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D);
		buf->hdr.le32_flags = EndianSwap32Bit(VIRTIO_GPU_FLAG_FENCE);

		buf->r.le32_x = 0;
		buf->r.le32_y = 0;
		buf->r.le32_width = 0x40000000; // 64, swapped
		buf->r.le32_height = 0x40000000;

		buf->le32_resource_id = EndianSwap32Bit(CURSOR_RESOURCE);

		VTSend(0, 0);
 		while (!VTDone(0)) {}
	}

	cursorSet = 1;
	return noErr;
}

// Sets the cursor’s x and y coordinates and visible state. If the cursor
// was successfully set by a previous call to SetHardwareCursor, the driver
// must program the hardware with the given x, y, and visible parameters
// and then return noErr. If the cursor was not successfully set by the
// last SetHardwareCursor call, the driver must return controlErr.
// --> csCursorX           X coordinate
// --> csCursorY           Y coordinate
// --> csCursorVisible     true if the cursor must be visible
OSStatus DrawHardwareCursor(VDDrawHardwareCursorRec *rec) {
	struct virtio_gpu_update_cursor *obuf = (void *)VTBuffers[1][0];
	struct virtio_gpu_ctrl_hdr *ibuf = (void *)VTBuffers[1][1];

	return paramErr;

	// Need to re-guess the hotspot and tell the host
	if (rec->csCursorVisible && !cursorVisible) {
		if (cursorIsArrow) {
			hotX = 1;
			hotY = 1;
		} else {
			hotX = hotX - rec->csCursorX + cursorX;
			hotY = hotY - rec->csCursorY + cursorY;
		}
	}

	if (!cursorSet) return controlErr;

	memset(obuf, 0, sizeof(*obuf));
	obuf->hdr.le32_type = EndianSwap32Bit(VIRTIO_GPU_CMD_MOVE_CURSOR);
	obuf->hdr.le32_flags = EndianSwap32Bit(VIRTIO_GPU_FLAG_FENCE);
	obuf->pos.le32_scanout_id = 0;
	obuf->pos.le32_x = EndianSwap32Bit(cursorX + hotX);
	obuf->pos.le32_y = EndianSwap32Bit(cursorY + hotY);

	obuf->le32_resource_id = EndianSwap32Bit(cursorVisible ? CURSOR_RESOURCE : 0);

	if (rec->csCursorVisible != cursorVisible) {
		obuf->hdr.le32_type = EndianSwap32Bit(VIRTIO_GPU_CMD_UPDATE_CURSOR);
		obuf->le32_hot_x = EndianSwap32Bit(hotX);
		obuf->le32_hot_y = EndianSwap32Bit(hotY);
	}

	// We are the only user of the cursorq, so return before checking it was done
	// A reply never seems to get written to this message
	while (!VTDone(1)) {}
	VTSend(1, 0);

	cursorX = rec->csCursorX;
	cursorY = rec->csCursorY;
	cursorVisible = rec->csCursorVisible;

	return noErr;
}

// The csCursorSet parameter should be true if the last SetHardwareCursor
// control call was successful and false otherwise. If csCursorSet is true,
// the csCursorX, csCursorY, and csCursorVisible values must match the
// parameters passed in to the last DrawHardwareCursor control call.
// <-- csCursorX           X coordinate from last DrawHardwareCursor call
// <-- csCursorY           Y coordinate from last DrawHardwareCursor call
// <-- csCursorVisible     true if the cursor is visible
// <-- csCursorSet         true if cursor was successfully set by the last
//                         SetHardwareCursor call
OSStatus GetHardwareCursorDrawState(VDHardwareCursorDrawStateRec *rec) {
	return paramErr;

	rec->csCursorX = cursorX;
	rec->csCursorY = cursorY;
	rec->csCursorVisible = cursorVisible;
	rec->csCursorSet = cursorSet;
	return noErr;
}
