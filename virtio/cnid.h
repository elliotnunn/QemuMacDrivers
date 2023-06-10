/*
Catalog Node ID graph, unsophisticated
*/

#pragma once

struct Node {
	unsigned long verify;
	struct Node *parent;
	struct Node *kids;
	struct Node *sibling;
	char name[256];
};

struct Node *Node(void);
void PutNode(struct Node *n, struct Node *newparent);

#define FOREACHKID(i, par) for (struct Node *i=par->kids; i; i=i->sibling)
