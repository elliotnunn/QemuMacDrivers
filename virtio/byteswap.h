#ifndef BYTESWAP_H
#define BYTESWAP_H

#include <intrinsics.h>
#include <stdint.h>

#if TARGET_CPU_PPC
#define SETLE32(ptr, rvalue) __stwbrx((unsigned int)(rvalue), (void *)(ptr), 0)
#define SETLE16(ptr, rvalue) __sthbrx((unsigned short)(rvalue), (void *)(ptr), 0)
#define GETLE32(ptr) __lwbrx((void *)(ptr), 0)
#define GETLE16(ptr) __lhbrx((void *)(ptr), 0)
#endif

#define LE32(val) ((((uint32_t)val & 0xff) << 24) | \
	(((uint32_t)val & 0xff00) << 8) | \
	(((uint32_t)val & 0xff0000) >> 8) | \
	(((uint32_t)val & 0xff000000) >> 24))

#define LE16(val) ((((uint16_t)val & 0xff) << 8) | \
	(((uint16_t)val & 0xff00) >> 8))

#endif
