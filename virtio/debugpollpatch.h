#pragma once

// Implemented by the callee
void InstallDebugPollPatch(void);

// Implemented by the caller
void DebugPollCallback(void);
