#include <stdarg.h>
#include <stddef.h>

// Open Firmware Client Interface (raw machine code, not a tvector)
void *ofcode;

// Protos
int of(const char *s, int narg, ...);

void openFirmwareEntry(void *initrd, long initrdsize, void *ci) {
	ofcode = ci;

	int r;

	r = of("interpret", 1, "cr", 0);

	if (r) {
		r = of("interpret",
			1, "cr .\" was nonzero\" cr",
			1, NULL);
	} else {
		r = of("interpret",
			1, "cr .\" was zero\" cr",
			1, NULL);
	}
}

// Call wrapper for Open Firmware Client Interface
// Call as: if (of("name",
//                 narg, arg1, arg2, ...
//                 nret, ret1, ret2, ...)) {panic("failed")}
int of(const char *s, int narg, ...) {
	long array[16] = {(long)s, narg, 0 /*nret goes here*/};
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

	for (int i=0; i<nret; i++) {
		long *ptr = va_arg(list, long *);
		if (ptr) *ptr = array[3+narg+i];
	}
	va_end(list);

	return result;
}
