#include <Memory.h>
#include <MixedMode.h>
#include <Patches.h>

#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#include "lprintf.h"

#include "patch68k.h"

struct block {
	void *original;
	long vector;
	char code[128];
};

static int hex(char c) {
	if ('0'<=c && c<='9') return c - '0';
	if ('a'<=c && c<='f') return c - 'a' + 10;
	if ('A'<=c && c<='F') return c - 'A' + 10;
	return -1;
}

static void *getvec(long vec) {
	if ((vec & 0xa800) == 0xa800) {
		return GetToolTrapAddress(vec);
	} else if ((vec & 0xa800) == 0xa000) {
		return GetOSTrapAddress(vec);
	} else {
		return *(void **)vec;
	}
}

static void setvec(long vec, void *addr) {
	if ((vec & 0xa800) == 0xa800) {
		SetToolTrapAddress(addr, vec);
	} else if ((vec & 0xa800) == 0xa000) {
		SetOSTrapAddress(addr, vec);
	} else {
		*(void **)vec = addr;
	}
}

void *Patch68k(unsigned long vector, const char *fmt, ...) {
	va_list argp;
	va_start(argp, fmt);

	// if this allocation fails then the system is crashed anyway
	struct block *block = (struct block *)NewPtrSysClear(sizeof (struct block));

	block->vector = vector;
	block->original = getvec(vector);

	char *code = block->code;
	int midhex = 0;

	for (const char *i=fmt; *i!=0; i++) {
		if (*i == '%') {
			i++;
			char kind = *i;
			if (kind == 'b') {
				*code++ = va_arg(argp, int);
			} else if (kind == 'w') {
				short word = va_arg(argp, int);
				*code++ = word >> 8;
				*code++ = word;
			} else if (kind == 'l') {
				long lword = va_arg(argp, long);
				*code++ = lword >> 24;
				*code++ = lword >> 16;
				*code++ = lword >> 8;
				*code++ = lword;
			} else if (kind == 'o') {
				long lword = (long)block->original;

				// Special case: never JMP or JSR to NULL or 0xffffffff
				if (lword == -1 || lword == 0) {
					if (code[-2] == 0x4e && code[-1] == 0xf9) { // JMP
						code[-2] = 0x4e; // becomes RTS
						code[-1] = 0x75;
					} else if (code[-2] == 0x4e && code[-1] == 0xb9) { // JSR
						code[-2] = 0x60; // becomes BRA.S .+6
						code[-1] = 0x04;
					}
				}

				*code++ = lword >> 24;
				*code++ = lword >> 16;
				*code++ = lword >> 8;
				*code++ = lword;
			}
		} else if (hex(*i) != -1) {
			if (midhex) {
				*code++ |= hex(*i);
			} else {
				*code = hex(*i) << 4;
			}
			midhex = !midhex;
		}
	}

	va_end(argp);

	BlockMove(block, block, sizeof *block); // clear 68k emulator cache
	SetPtrSize((Ptr)block, (char *)code - (char *)block); // shrink
	setvec(vector, &block->code); // install

	if (lprintf_enable) {
		lprintf("Patched %#x. Old code at %p. New code in sys heap at %p:\n   ",
			vector, block->original, &block->code);
		for (char *i=block->code; i<code; i+=2) {
			lprintf(" %02x%02x", i[0], i[1]);
		}
		lprintf("\n");
	}

	return &block->code;
}

void Unpatch68k(void *patch) {
	struct block *block = patch - offsetof(struct block, code);

	if (getvec(block->vector) == &block->code) {
		// yay, we never got over-patched
		lprintf("Unpatched %#x back to %p\n", block->vector, block->original);

		setvec(block->vector, block->original);
		DisposePtr((Ptr)block);
	} else {
		// someone over-patched us! yuck...
		if ((long)block->original == 0 || (long)block->original == -1) {
			// As always, never jump to a nonexistent address
			block->code[0] = 0x4e; // RTS
			block->code[1] = 0x75;
		} else {
			block->code[0] = 0x4e; // JMP
			block->code[1] = 0xb9;
			memcpy(block->code+2, &block->original, 4);
		}
		BlockMove(block, block, 14); // clear 68k emulator cache

		lprintf("Unpatched %#x using a thunk:\n   ", block->vector);
		for (char *i=block->code; i<block->code+6; i+=2) {
			lprintf(" %02x%02x", i[0], i[1]);
		}
		lprintf("\n");
	}
}
