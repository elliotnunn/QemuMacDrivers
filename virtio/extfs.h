/*
Hook the ToExtFS vector, which is called for traps on non-HFS(+) volumes

- The ExtFS function is NOT defined in extfs.c: you define your own.
- Return extFSErr to pass through to the next filesystem.
- ExtFS will be run on a separate stack from the calling application.
*/

#pragma once

// Do not call this twice!
void InstallExtFS(void);

long ExtFS(void *pb, long selector);

long ExtFSStackUsed(void);
