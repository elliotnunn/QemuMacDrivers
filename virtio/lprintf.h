#pragma once

extern int lprintf_enable;
extern char lprintf_prefix[128];
void lprintf(const char *fmt, ...);
