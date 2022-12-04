#ifndef LATEBOOTHOOK_H
#define LATEBOOTHOOK_H

// Implemented by the callee
void InstallLateBootHook(void);

// Implemented by the caller
void LateBootHook(void);

#endif
