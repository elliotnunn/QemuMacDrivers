#ifndef BLIT_H
#define BLIT_H

#include <stdint.h>

extern const int BlitterAlign[6];

void Blit1(const void *src, long srcrowbytes, void *dst, long dstrowbytes, long t, long l, long b, long r, uint32_t *clut);

// Work in 32-bit longs
void blit1asm(const void *srcpix, long srcrowskip, void *dstpix, long dstrowskip, long w, long h, uint32_t color0, uint32_t colorXOR);

#endif
