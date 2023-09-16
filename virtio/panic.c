#include "scc.h"

#include <Debugging.h>

#include "panic.h"

void panic(const char *panicstr) {
	const char *prefix = "\npanic: ";

	for (const char *p=prefix; *p!=0; p++) {
		sccWrite(*p);
	}

	for (const char *p=panicstr; *p!=0; p++) {
		sccWrite(*p);
	}

	sccWrite('\n');

	unsigned char pstring[256] = {0};
	for (int i=0; i<255; i++) {
		if (panicstr[i]) {
			pstring[i+1] = panicstr[i];
			pstring[0] = i+1;
		} else {
			break;
		}
	}
	DebugStr(pstring);
	for (;;) *(volatile char *)0x68f168f1 = 1;
}
