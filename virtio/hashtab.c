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
	union {
		void *ptr;
		char inln[4];
	} key;
	union {
		void *ptr;
		char inln[4];
	} val;
	short klen;
	short vlen;
	int tag;
};

struct entry *table[1<<12];
char *blob, *bump, *limit;

static unsigned long hash(const void *key, short klen);
static void alloc(long bytes);
static struct entry *find(int tag, const void *key, short klen);

void HTinstall(int tag, const void *key, short klen, const void *val, short vlen) {
	struct entry *found = find(tag, key, klen);

	if (found) {
		void *val;
		if (vlen <= 4) {
			val = found->val.inln;
		} else {
			val = found->val.ptr;
		}

		if (vlen <= 4) {
			// Inline
			memcpy(found->val.inln, val, vlen);
			found->vlen = vlen;
		} else if (vlen <= found->vlen) {
			// Out of line but shorter
			memcpy(found->val.ptr, val, vlen);
			found->vlen = vlen;
		} else {
			// Allocate room for new value
			while ((uintptr_t)bump % 8) bump++;
			found->val.ptr = bump;
			memcpy(bump, val, vlen);
			bump += vlen;
			found->vlen = vlen;
		}
	} else { // create a new entry
		struct entry **root = &table[hash(key, klen) % (sizeof(table)/sizeof(*table))];

		struct entry ent = {
			.next = *root,
			.tag = tag,
			.klen = klen,
			.vlen = vlen,
		};

		if (klen <= 4) {
			// Store key within the entry
			memcpy(ent.key.inln, key, klen);
		} else {
			// Key can be unaligned
			alloc(klen);
			memcpy(bump, key, klen);
			ent.key.ptr = bump;
			bump += klen;
		}

		if (vlen <= 4) {
			// Store value within the entry
			memcpy(ent.val.inln, val, klen);
		} else {
			// Value must be aligned
			while ((uintptr_t)bump % 8) {
				alloc(1);
				bump++;
			}
			memcpy(bump, val, vlen);
			ent.val.ptr = bump;
			bump += vlen;
		}

		// Struct quite strict with alignment
		alloc(sizeof(struct entry) + alignof(struct entry));
		while ((uintptr_t)bump % alignof(struct entry)) bump++;
		*root = memcpy(bump, &ent, sizeof(struct entry));
		bump += sizeof(struct entry);
	}
}

void *HTlookup(int tag, const void *key, short klen) {
	struct entry *found = find(tag, key, klen);
	if (!found) return NULL;

	if (found->vlen <= 4) {
		return found->val.inln;
	} else {
		return found->val.ptr;
	}
}

static unsigned long hash(const void *key, short klen) {
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

static struct entry *find(int tag, const void *key, short klen) {
	struct entry *root = table[hash(key, klen) % (sizeof(table)/sizeof(*table))];

	for (struct entry *e=root; e!=NULL; e=e->next) {
		if (e->tag != tag || e->klen != klen) continue;

		if (klen <= 4) {
			if (!memcmp(key, e->key.inln, klen)) return e; // inline key
		} else {
			if (!memcmp(key, e->key.ptr, klen)) return e; // separate key
		}
	}

	return NULL;
}

#include <stdio.h>
int main(int argc, char **argv) {
#define SETSTR(k, v) HTinstall(0, k, strlen(k)+1, v, strlen(v)+1)
#define GETSTR(k) ((char *)HTlookup(0, k, strlen(k)+1))

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
