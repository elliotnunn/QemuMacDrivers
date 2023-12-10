// Write Hot Code And Lie

#include <stdarg.h>
#include <stddef.h>

// Open Firmware Client Interface
// Points to raw PowerPC code, not to a tvector
void *ofci;

// Protos
int of(const char *s, int nargs, ...);

// Until the linker works, this must be the first entry
void openFirmwareEntry(void *initrd, long initrdsize, void *ci) {
	ofci = ci;

	int r;

//  	r = ofci((long []){(long)"interpret", 1, 1, (long)".\" elliot wins\" cr", 0, 9999});

	r = of("interpret",
		1, "cr .\" how about this\" cr",
		1, NULL);

// 	if (r != 0) for (;;) of("abc", 0, 0);

// 	const char *x = "interpret";
// 	const char *y = "cr";
//
// 	long a[] = {(long)x, 1, 1, (long)y, 0, 99};
// 	for (int i=0; i<10; i++) ofci(a);

// 	if (a[3] == 99) return;
// 	for (;;) ofci(a);

// 	of("cr", 0, 0);
}

// Call wrapper for OF Client Interface
// Call as of("name", nargs, arg1, arg2, nret, ret1, ret2)
int of(const char *s, int nargs, ...) {
	long array[16] = {(long)s, nargs, 1234 /*nret placeholder*/};
	va_list list;

	va_start(list, nargs);

	for (int i=0; i<nargs; i++) {
		array[3+i] = (long)va_arg(list, void *);
// 		ofci((long []){(long)"interpret", 1, 1, (long)".\" narg cnt\" cr", 0, 9999});
	}

	int nrets = va_arg(list, int);

	array[2] = nrets;

	asm volatile (
		"mtctr   %[ofci]    \n"
		"mr      3,%[array] \n"
		"bctrl              \n"
		: // no result
		: [array] "r" (array), [ofci] "r" (ofci) // args
		: "ctr", "lr", "r3", "r4", "r5", "r6", "r7", "r8", "memory" // clobbers
	);

	for (int i=0; i<nrets; i++) {
		long *ptr = va_arg(list, long *);
		if (ptr) *ptr = (long)array[3+nargs+i];
	}

// 	return retval;
}

void *memset(void *s, int c, long len) {
    unsigned char *dst = s;
    while (len > 0) {
        *dst = (unsigned char) c;
        dst++;
        len--;
    }
    return s;
}