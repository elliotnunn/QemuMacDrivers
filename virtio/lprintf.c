#include <stdarg.h>
#include <stdio.h>

#include "scc.h"

#include "lprintf.h"

int lprintf_enable;

void lprintf(const char *fmt, ...) {
	if (!lprintf_enable) return;

	static char buf[4096];
	va_list aptr;

	va_start(aptr, fmt);
	vsnprintf(buf, sizeof(buf), fmt, aptr);
	va_end(aptr);

	// Convert \r and \n to \r\n
	for (int i=0; i<sizeof buf && buf[i]; i++) {
		char this = buf[i];

		if (this == '\r' || this == '\n') {
			sccWrite('\r');
			sccWrite('\n');
		} else {
			sccWrite(this);
		}
	}
}
