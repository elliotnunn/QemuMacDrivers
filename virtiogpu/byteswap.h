#ifndef BYTESWAP_H
#define BYTESWAP_H

#include <intrinsics.h>
#include <stdint.h>

#if TARGET_CPU_PPC
#define SETLE32(ptr, rvalue) __stwbrx((unsigned int)(rvalue), (ptr), 0)
#define SETLE16(ptr, rvalue) __sthbrx((unsigned short)(rvalue), (ptr), 0)
#define GETLE32(ptr) __lwbrx((ptr), 0)
#define GETLE16(ptr) __lhbrx((ptr), 0)
#endif

#endif
