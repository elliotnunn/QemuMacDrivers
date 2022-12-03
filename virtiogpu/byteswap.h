#ifndef BYTESWAP_H
#define BYTESWAP_H

#include <intrinsics.h>
#include <stdint.h>

#if TARGET_CPU_PPC
#define SETLE32(lvalue, rvalue) __stwbrx((unsigned int)(rvalue), &(lvalue), 0)
#define SETLE16(lvalue, rvalue) __sthbrx((unsigned short)(rvalue), &(lvalue), 0)
#define GETLE32(rvalue) __lwbrx(&(rvalue), 0)
#define GETLE16(rvalue) __lhbrx(&(rvalue), 0)
#endif

#endif
