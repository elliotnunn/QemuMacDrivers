/*
TODO: make opportunistically resizable (will need SysTask time hook)
*/

#include <stddef.h>
#include <string.h>
#include <stdalign.h>

#include "hashtab.h"

struct entry {
	struct entry *next;
	void *key;
	void *val;
	short klen;
	short vlen;
};

struct entry *table[1<<12];
char blob[1<<15];
char *bump = blob;

static unsigned long hash(void *key, short klen);

void HTinstall(void *key, short klen, void *val, short vlen) {
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
	memcpy(bump, key, klen);
	key = bump;
	bump += klen;

	// Value must be aligned
	while ((uintptr_t)bump % 8) bump++;
	memcpy(bump, val, vlen);
	val = bump;
	bump += vlen;

	// Struct quite strict with alignment
	while ((uintptr_t)bump % alignof(struct entry)) bump++;
	struct entry *e = (struct entry *)bump;
	*e = (struct entry){*root, key, val, klen, vlen};
	*root = e;
	bump += sizeof(*e);
}

void *HTlookup(void *key, short klen) {
	struct entry *root = table[hash(key, klen) % (sizeof(table)/sizeof(*table))];

	for (struct entry *e=root; e!=NULL; e=e->next) {
		if (e->klen == klen && !memcmp(e->key, key, klen)) {
			return e->val;
		}
	}

	return NULL;
}

static unsigned long hash(void *key, short klen) {
	unsigned long hashval = 0;
	for (short i=0; i<klen; i++) {
		hashval = hashval * 31 + ((unsigned char *)key)[i];
	}
	return hashval;
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