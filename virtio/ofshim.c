#include <stdarg.h>
#include <stddef.h>
#include <string.h>

// Globals
void *ofcode; // Client Interface (raw machine code, not a tvector)
long stdout; // OF ihandle

// Prototypes
int of(const char *s, int narg, ...);
void ofprint(const char *s);
void ofhex(long x);

// Entry point (via the asm glue in ofshim.s)
void ofmain(void *initrd, long initrdsize, void *ci) {
	ofcode = ci; // the vector for calling into Open Firmware
	of("interpret",
		1, "stdout @",
		2, NULL, &stdout); // get handle for logging

	ofprint("it seems to be working\n");
	ofhex(0); ofhex(1); ofhex(-10);
	ofprint("<- like it?\n");
}

// Call wrapper for Open Firmware Client Interface
// Call as: if (of("name",
//                 narg, arg1, arg2, ...
//                 nret, ret1, ret2, ...)) {panic("failed")}
int of(const char *s, int narg, ...) {
	// array to contain {nameptr, narg, arg1..., nret, ret1...}
	long array[16] = {(long)s, narg, 0 /*nret will go here*/};
	va_list list;

	va_start(list, narg);
	for (int i=0; i<narg; i++) {
		array[3+i] = va_arg(list, long);
	}

	int nret = array[2] = va_arg(list, int);

	// Need asm glue because ofcode is a raw code pointer, not a full function ptr
	int result;
	asm volatile (
		"mtctr   %[ofcode]  \n"
		"mr      3,%[array] \n"
		"bctrl              \n"
		"mr      %[result],3\n"
		: [result] "=r" (result)
		: [array] "r" (array), [ofcode] "r" (ofcode) // args
		: "ctr", "lr", "r3", "r4", "r5", "r6", "r7", "r8", "memory" // clobbers
	);

	if (result == 0) {
		for (int i=0; i<nret; i++) {
			long *ptr = va_arg(list, long *);
			if (ptr) *ptr = array[3+narg+i];
		}
	}
	va_end(list);

	return result;
}

// I couldn't bear to static-link another printf implementation
void ofprint(const char *s) {
	of("write",
		3, stdout, s, strlen(s),
		1, NULL); // discard "bytes written"
}

void ofhex(long x) {
	const char *hex = "0123456789abcdef";
	char s[] = "00000000 ";
	for (int i=0; i<8; i++) {
		s[i] = hex[15 & (x >> (28-i*4))];
	}
	ofprint(s);
}
