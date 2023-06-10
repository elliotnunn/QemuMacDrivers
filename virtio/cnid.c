#include <stddef.h>

#include "cnid.h"

// Unsatisfactory!
static struct Node blob[64*1000];
static struct Node *next;

struct Node *Node(void) {
	return next++;
}

// NULL newparent means "delete"
// It's okay for the old parent to be NULL also
void PutNode(struct Node *n, struct Node *newparent) {
	// Remove from old parent
	if (n->parent) {
		if (n->parent->kids == n) {
			n->parent->kids = n->sibling;
		}

		FOREACHKID(i, n->parent) {
			if (i->sibling == n) {
				i->sibling = n->sibling;
				break;
			}
		}
	}

	n->parent = newparent;
	if (newparent != NULL) {
		n->sibling = newparent->kids;
		newparent->kids = n;
	} else {
		// Mark this and all child CNIDs as deleted
	}
}
