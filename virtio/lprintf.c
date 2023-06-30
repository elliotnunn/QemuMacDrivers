#include <stdarg.h>
#include <stdio.h>

#include "scc.h"

#include "lprintf.h"

int lprintf_enable;
char lprintf_prefix[128];

void lprintf(const char *fmt, ...) {
	if (!lprintf_enable) return;
	static int leftmargin = 1;

	static char buf[4096];
	va_list aptr;

	va_start(aptr, fmt);
	vsnprintf(buf, sizeof(buf), fmt, aptr);
	va_end(aptr);

	// Convert \r and \n to \r\n
	for (int i=0; i<sizeof buf && buf[i]; i++) {
		char this = buf[i];

		if (leftmargin) {
			for (int j=0; j<sizeof lprintf_prefix && lprintf_prefix[j]; j++) {
				sccWrite(lprintf_prefix[j]);
			}
			leftmargin = 0;
		}

		if (this == '\r' || this == '\n') {
			sccWrite('\r');
			sccWrite('\n');
			leftmargin = 1;
		} else {
			sccWrite(this);
		}
	}
}
