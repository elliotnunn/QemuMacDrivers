#pragma once

// Implemented by the callee
void InstallLateBootHook(void);

// Implemented by the caller
void LateBootHook(void);
