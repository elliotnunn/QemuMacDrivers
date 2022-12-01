#ifndef DEBUGPOLLPATCH_H
#define DEBUGPOLLPATCH_H

// Implemented by the callee
void InstallDebugPollPatch(void);

// Implemented by the caller
void DebugPollCallback(void);

#endif
