/*
Layering the File Mgr API on 9P semantics requires a growable hash table

File Mgr calls are not allowed to move memory (i.e. call the Memory Mgr at all,
or access an unlocked block). The only exceptions are synchronous MountVol,
Open and OpenWD calls. So the HTpreallocate() call can be made at those times.

TODO:
Access system task time for another opportunity to grow
Reclaim stale data in the array (e.g. free list inside the heap block)
*/

#include <LowMem.h>
#include <Memory.h>

#include <stddef.h>
#include <string.h>
#include <stdalign.h>

#include "hashtab.h"

#include "lprintf.h"

// Keep to 16 bytes
struct entry {
	union {
		size_t offset;
		char inln[4];
	} key;
	union {
		size_t offset;
		char inln[4];
	} val;
	short klen;
	short vlen;
	int tag;
};

// Linearly probed hash table
// Grow exponentially to keep occupancy between 25% and 50%
static struct entry *table;
static size_t tablesize, tableused;

static Handle blob;
static size_t blobsize, blobused;

static unsigned long hash(int tag, const void *key, short klen);
static size_t store(const void *data, size_t bytes);
static struct entry *find(int tag, const void *key, short klen);
static void *entrykey(struct entry *e);
static void *entryval(struct entry *e);
static void dump(void);

void HTpreallocate(void) {
	short saveMemErr = LMGetMemErr();

	size_t newtablesize = 4096;
	while (newtablesize/2 <= tableused) newtablesize *= 2;

	if (newtablesize != tablesize) {
		struct entry *newtable = (void *)NewPtrSysClear(newtablesize * sizeof (struct entry));
		if (newtable != NULL) {
			// Copy entries across
			for (size_t i=0; i<tablesize; i++) {
				struct entry *e = &table[i];
				unsigned long probe = hash(e->tag, entrykey(e), e->klen);
				while (newtable[probe & (newtablesize - 1)].klen != 0) probe++;
				newtable[probe & (newtablesize - 1)] = *e;
			}

			if (table) DisposePtr((void *)table);
			table = newtable;
			tablesize = newtablesize;
			lprintf("Hash table slots = %d\n", tablesize);
		}
	}

	size_t newblobsize = 64*1024;
	while (newblobsize/2 <= blobused) newblobsize *= 2;

	if (newblobsize != blobsize) {
		if (blob == NULL) {
			blob = NewHandleClear(newblobsize);
			if (blob) HLock(blob);
			blobsize = blob ? newblobsize : 0;
		} else {
			HUnlock(blob);
			SetHandleSize(blob, newblobsize);
			blobsize = GetHandleSize(blob);
			HLock(blob);
		}
		lprintf("Hash table storage bytes = %d\n", blobsize);
	}

	LMSetMemErr(saveMemErr);
}

void HTinstall(int tag, const void *key, short klen, const void *val, short vlen) {
	struct entry *found = find(tag, key, klen);

	if (!found) {
		lprintf("Hash table out of slots!\n");
		*(volatile char *)0x68f168f1 = 1;
	} else if (found->klen != 0) {
		// Overwrite existing table entry
		if (vlen <= 4) {
			// Inline
			memcpy(found->val.inln, val, vlen);
		} else if (vlen <= ((found->vlen + 7) & -8)) {
			// Out of line but shorter
			memcpy(*blob + found->val.offset, val, vlen);
		} else {
			// Allocate room for new value
			found->val.offset = store(val, vlen);
		}
		found->vlen = vlen;
	} else {
		// Populate new table entry
		tableused++;
		found->tag = tag;
		found->klen = klen;
		found->vlen = vlen;

		if (klen <= 4) {
			// Store key within the entry
			memcpy(found->key.inln, key, klen);
		} else {
			// Store outside the entry
			found->key.offset = store(key, klen);
		}

		if (vlen <= 4) {
			// Store value within the entry
			memcpy(found->val.inln, val, vlen);
		} else {
			// Store outside the entry
			found->val.offset = store(val, vlen);
		}
	}
}

void *HTlookup(int tag, const void *key, short klen) {
	struct entry *found = find(tag, key, klen);
	if (!found || found->klen == 0) return NULL;
	return entryval(found);
}

static unsigned long hash(int tag, const void *key, short klen) {
	unsigned long hashval = tag;
	for (short i=0; i<klen; i++) {
		hashval = hashval * 31 + ((unsigned char *)key)[i];
	}
	return hashval;
}

// Ensure at least this many bytes at bump
// Bump-allocate inside our large block
static size_t store(const void *data, size_t bytes) {
	if (blobused + bytes > blobsize) {
		lprintf("Hash table out of storage area!\n");
		*(volatile char *)0x68f16851 = 1;
	}

	memcpy(*blob + blobused, data, bytes);

	size_t ret = blobused;
	blobused += (bytes + 7) & -8; // everything aligned to 8 bytes forever
	return ret;
}

static struct entry *find(int tag, const void *key, short klen) {
	unsigned long start = hash(tag, key, klen);

	for (size_t i=0; i<tablesize; i++) {
		unsigned long probe = (start + i) & (tablesize - 1);
		struct entry *e = &table[probe];

		if (e->klen == 0) {
			return e; // key not found, but here is where to put it
		}

		if (e->tag == tag && e->klen == klen && !memcmp(entrykey(e), key, klen)) {
			return e;
		}
	}

	return NULL; // key not found AND the table is full (very bad)
}

static void *entrykey(struct entry *e) {
	if (e->klen <= 4) {
		return e->key.inln;
	} else {
		return *blob + e->key.offset;
	}
}

static void *entryval(struct entry *e) {
	if (e->vlen <= 4) {
		return e->val.inln;
	} else {
		return *blob + e->val.offset;
	}
}

static void dump(void) {
	lprintf("hashtable dump\n");
	for (int i=0; i<tablesize; i++) {
		if (table[i].klen == 0) continue;

		lprintf("   [% 5d] '%c' ", i, table[i].tag);

		unsigned char *x;
		x = entrykey(&table[i]);
		for (int j=0; j<table[i].klen; j++) {
			lprintf("%02x", x[j]);
		}
		lprintf(": ");
		x = entryval(&table[i]);
		for (int j=0; j<table[i].vlen; j++) {
			lprintf("%02x", x[j]);
		}
		lprintf("\n");
	}
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
}
