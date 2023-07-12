#pragma once

#define ORIGCODE68K "%o" // no arguments: 32-bit address of original routine
#define BYTECODE68K "%b" // 1 argument: 8-bit literal
#define WORDCODE68K "%w" // 1 argument: 16-bit literal
#define LONGCODE68K "%l" // 1 argument: 32-bit literal
// ... and everything else in the format string is hex or whitespace

void *Patch68k(unsigned long vector, const char *fmt, ...);
void Unpatch68k(void *patch);
