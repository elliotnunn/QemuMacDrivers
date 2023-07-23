/*
TODO: make opportunistically resizable (will need SysTask time hook)
*/

#include <stddef.h>
#include <string.h>
#include <stdalign.h>

#include "hashtab.h"

#include "lprintf.h"

struct entry {
	struct entry *next;
	void *key;
	void *val;
	short klen;
	short vlen;
};

struct entry *table[1<<12];
char *blob, *bump, *limit;

static unsigned long hash(void *key, short klen);
static void alloc(long bytes);

void HTinstall(void *key, short klen, void *val, short vlen) {
// 	if (klen == 4)
// 		lprintf("HTInstall %08x -> %08x.%s\n", *(unsigned long *)key, *(unsigned long *)val, (char *)val+4);
// 	else
// 		lprintf("HTInstall %08x.%s -> %08x\n", *(unsigned long *)key, (char *)key+4, *(unsigned long *)val);

	struct entry **root = &table[hash(key, klen) % (sizeof(table)/sizeof(*table))];

	for (struct entry *e=*root; e!=NULL; e=e->next) {
		if (e->klen == klen && !memcmp(e->key, key, klen)) {
			if (vlen <= e->vlen) {
				// Replace the value in-place
				memcpy(e->val, val, vlen);
				e->vlen = vlen;
			} else {
				// Allocate room for new value
				while ((uintptr_t)bump % 8) bump++;
				e->val = bump;
				e->vlen = vlen;
				memcpy(bump, val, vlen);
				bump += vlen;
			}
			return;
		}
	}

	// Key can be unaligned
	alloc(klen);
	memcpy(bump, key, klen);
	key = bump;
	bump += klen;

	// Value must be aligned
	while ((uintptr_t)bump % 8) {
		alloc(1);
		bump++;
	}
	memcpy(bump, val, vlen);
	val = bump;
	bump += vlen;

	// Struct quite strict with alignment
	alloc(sizeof(struct entry) + alignof(struct entry));
	while ((uintptr_t)bump % alignof(struct entry)) bump++;
	struct entry *e = (struct entry *)bump;
	*e = (struct entry){*root, key, val, klen, vlen};
	*root = e;
	bump += sizeof(*e);
}

void *HTlookup(void *key, short klen) {
	struct entry *root = table[hash(key, klen) % (sizeof(table)/sizeof(*table))];

// 	if (klen == 4)
// 		lprintf("HTLookup %08x -> ", *(unsigned long *)key);
// 	else
// 		lprintf("HTLookup %08x.%s -> ", *(unsigned long *)key, (char *)key+4);

	for (struct entry *e=root; e!=NULL; e=e->next) {
		if (e->klen == klen && !memcmp(e->key, key, klen)) {
// 			if (klen == 4)
// 				lprintf("%08x.%s\n", *(unsigned long *)e->val, (char *)e->val+4);
// 			else
// 				lprintf("%08x\n", *(unsigned long *)e->val);
			return e->val;
		}
	}

// 	lprintf("not found\n");

	return NULL;
}

static unsigned long hash(void *key, short klen) {
	unsigned long hashval = 0;
	for (short i=0; i<klen; i++) {
		hashval = hashval * 31 + ((unsigned char *)key)[i];
	}
	return hashval;
}

// Ensure at least this many bytes at bump
static void alloc(long bytes) {
	if (bump + bytes <= limit) return;

	long size = 64*1024;

	for (;;) {
		blob = NewPtrSys(64*1024);
		if (blob) break;
		size >>= 1;
		if (size < 2048) {
			blob = (char *)0xdeadbeef;
			break;
		}
	}

	bump = blob;
	limit = blob + size;
}

#include <stdio.h>
int main(int argc, char **argv) {
#define SETSTR(k, v) HTinstall(k, strlen(k)+1, v, strlen(v)+1)
#define GETSTR(k) ((char *)HTlookup(k, strlen(k)+1))

	SETSTR("one", "alpha");
	SETSTR("two", "beta");
	SETSTR("three", "gamma");
	SETSTR("one", "something else");

	printf("%s %s %s\n", GETSTR("one"), GETSTR("two"), GETSTR("three"));

	for (char *x=blob; x!=bump; x++) {
		printf("%02x", *x);
	}
	printf("\n");
}
