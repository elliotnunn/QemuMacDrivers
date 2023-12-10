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

	r = of("interpret",
		1, "cr .\" how about this\" cr",
		1, NULL);
}

// Call wrapper for OF Client Interface
// Call as: of("name",
//             nargs, arg1, arg2, ...
//             nrets, ret1, ret2, ...);
int of(const char *s, int nargs, ...) {
	long array[16] = {(long)s, nargs, 1234 /*nret placeholder*/};
	va_list list;

	va_start(list, nargs);
	for (int i=0; i<nargs; i++) {
		array[3+i] = (long)va_arg(list, void *);
	}

	int nrets = array[2] = va_arg(list, int);

	// Need asm glue because it's a code pointer, not a full function ptr
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
	va_end(args);

	return array[3+nargs+nrets];
}
