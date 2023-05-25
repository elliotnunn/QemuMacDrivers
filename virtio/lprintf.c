#include <stdarg.h>
#include <stdio.h>

#include "scc.h"

#include "lprintf.h"

int lprintf_enable;

void lprintf(const char *fmt, ...) {
	if (!lprintf_enable) return;

	static char buf[256];
	va_list aptr;

	va_start(aptr, fmt);
	vsprintf(buf, fmt, aptr);
	va_end(aptr);

	for (int i=0; i<sizeof buf && buf[i]; i++) {
		if (buf[i] == '\n') {
			sccWrite('\r');
		}
		sccWrite(buf[i]);
	}
}
