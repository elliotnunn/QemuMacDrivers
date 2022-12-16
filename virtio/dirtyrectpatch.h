#ifndef DIRTYRECTPATCH_H
#define DIRTYRECTPATCH_H

// Implemented by the callee
void InstallDirtyRectPatch(void);

// Implemented by the caller
void DirtyRectCallback(short top, short left, short bottom, short right);

#endif
