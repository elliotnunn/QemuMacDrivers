#ifndef BLIT_H
#define BLIT_H

#include <stdint.h>

extern const char BlitterAlign[6];

// rowbytes applies to the src, and can be bitshifted to apply to dst

// The pointers to l and r return the actual width copied after alignment,
// which is useful for knowing if the cursor needs redrawing.
void Blit(int bppshift,
	short t, short *l, short b, short *r, const void *src, void *dest, long rowbytes,
	uint32_t clut[256], uint8_t gamma_red[256], uint8_t gamma_grn[256], uint8_t gamma_blu[256]);

// Work in 32-bit longs
void blit1asm(const void *srcpix, long srcrowskip, void *dstpix, long dstrowskip, long w, long h, uint32_t color0, uint32_t colorXOR);

#endif
