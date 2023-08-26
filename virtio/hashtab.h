#pragma once

void HTinstall(int tag, const void *key, short klen, const void *val, short vlen);
void *HTlookup(int tag, const void *key, short klen);
