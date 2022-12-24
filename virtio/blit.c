#include "lprintf.h"

#include "blit.h"

const int BlitterAlign[6] = {
	4, // 1-bit
	4, // 2-bit
	4, // 4-bit
	1, // 8-bit
	2, // 16-bit
	4, // 32-bit
};

// Higher-level wrapper around asm loop
void blit1(const void *src, long srcrowbytes, void *dst, long dstrowbytes, long t, long l, long b, long r, uint32_t *clut) {
	enum {CHUNK = 32};

	long w;
	l &= -CHUNK;
	r = (r + CHUNK - 1) & -CHUNK;

	w = r - l;

	blit1asm(
		(char *)src + t*srcrowbytes + l/8 - 4,         // srcpix: subtract 4 to use PowerPC preincrement
		srcrowbytes - w/8,                             // srcrowskip
		(char *)dst + t*dstrowbytes + l*4,             // dstpix
		dstrowbytes - w*4,                             // dstrowskip
		w/CHUNK,                                       // w: pixels/CHUNK
		b-t,                                           // h
		clut[0],                                       // color0
		clut[0]^clut[1]);                              // colorXOR
}
