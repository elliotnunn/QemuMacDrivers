#ifndef BLIT_H
#define BLIT_H

#include <stdint.h>

extern const char BlitterAlign[6];

// rowbytes applies to the src, and can be bitshifted to apply to dst

void Blit(int bppshift,
	long t, long l, long b, long r, const void *src, void *dest, long rowbytes,
	uint32_t clut[256], uint8_t gamma_red[256], uint8_t gamma_grn[256], uint8_t gamma_blu[256]);

// Work in 32-bit longs
void blit1asm(const void *srcpix, long srcrowskip, void *dstpix, long dstrowskip, long w, long h, uint32_t color0, uint32_t colorXOR);

#endif
