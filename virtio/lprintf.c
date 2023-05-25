#include <stdio.h>
#include <stdarg.h>

#include "lprintf.h"

static void lprint(char *c) {
	while (*c != 0) {
		asm volatile (
			"mr      6,%0 \n\t"
			"lis     3,0x1137 \n\t"
			"ori     3,3,0x24fa \n\t"
			"lis     4,0x7781 \n\t"
			"ori     4,4,0x0f9b \n\t"
			"li      5,47 \n\t"
			"sc \n\t" :
			: // output
			"r" (*c) : // input
			"r3", "r4", "r5", "memory"
		);
		c++;
	}
}

void lprintf(const char *fmt, ...) {
	char buf[512];

	va_list aptr;

	va_start(aptr, fmt);
	vsprintf(buf, fmt, aptr);
	va_end(aptr);

	lprint(buf);
}
