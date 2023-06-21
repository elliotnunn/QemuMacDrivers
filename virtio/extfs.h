// Delicately hook the "ToExtFS" global.
// Do not call twice!

#pragma once

#include <Types.h>

// Implemented by the callee
void InstallExtFS(void);

// Implemented by the caller, not to be confused with the lowmem global
// If passing through then MUST return the original selector
// pb from a0, selector from d0
long ExtFS(void *pb, long selector);
