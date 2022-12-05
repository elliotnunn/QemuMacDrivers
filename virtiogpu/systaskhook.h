#ifndef SYSTASKHOOK_H
#define SYSTASKHOOK_H

// Implemented by the callee
void InstallSysTaskHook(void);

// Implemented by the caller
void SysTaskHook(void);

#endif
