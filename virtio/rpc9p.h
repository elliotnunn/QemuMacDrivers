// A synchronous 9P2000.u interface backing onto Virtio.
// Functions return true on failure.
// Takes over the Virtio interface, implements DNotified and DConfigChange.

// Track use of FID 0-31 and automatically clunk when reuse is attempted

#pragma once

#include <stdint.h>
#include <stdbool.h>

enum {
	NOFID = -1,
};

struct Qid9 {
	uint8_t type;
	uint32_t version;
	uint64_t path;
};

struct Stat9 { // Abbreviated from the 9p2000.u spec
	struct Qid9 qid;
	uint32_t mode;
	uint32_t atime, mtime;
	uint64_t length;
	char name[512];
	char linktarget[512];
};

// Single large buffer used by Read9/write
// Clobbered by all calls
extern char *Buf9;
extern uint32_t Max9;

// Linux error code (check when a function returns true)
extern uint32_t Err9;

bool Init9(uint16_t vioq, uint16_t viobuffers);
bool Attach9(uint32_t fid, uint32_t afid, char *uname, char *aname, struct Qid9 *retqid);
bool Walk9(uint32_t fid, uint32_t newfid, uint16_t nwname, const char **name, uint16_t *retnwqid, struct Qid9 *retqid);
bool Lopen9(uint32_t fid, uint32_t flags, struct Qid9 *retqid, uint32_t *retiounit);
void Clrdirbuf9(void *buf, size_t bufsize);
char Readdir9(uint32_t fid, void *buf, size_t bufsize, struct Qid9 *retqid, char *rettype, char retname[512]);
bool Getattr9(uint32_t fid, struct Qid9 *retqid, uint64_t *retsize, uint64_t *rettime);
bool Clunk9(uint32_t fid);
bool Read9(uint32_t fid, uint64_t offset, uint32_t count, uint32_t *actual_count);
bool Write9(uint32_t fid, uint64_t offset, uint32_t count, uint32_t *actual_count);

// TODO: tidy this up
#define O_ACCMODE	00000003
#define O_RDONLY	00000000
#define O_WRONLY	00000001
#define O_RDWR		00000002
#ifndef O_CREAT
#define O_CREAT		00000100	/* not fcntl */
#endif
#ifndef O_EXCL
#define O_EXCL		00000200	/* not fcntl */
#endif
#ifndef O_NOCTTY
#define O_NOCTTY	00000400	/* not fcntl */
#endif
#ifndef O_TRUNC
#define O_TRUNC		00001000	/* not fcntl */
#endif
#ifndef O_APPEND
#define O_APPEND	00002000
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK	00004000
#endif
#ifndef O_DSYNC
#define O_DSYNC		00010000	/* used to be O_SYNC, see below */
#endif
#ifndef FASYNC
#define FASYNC		00020000	/* fcntl, for BSD compatibility */
#endif
#ifndef O_DIRECT
#define O_DIRECT	00040000	/* direct disk access hint */
#endif
#ifndef O_LARGEFILE
#define O_LARGEFILE	00100000
#endif
#ifndef O_DIRECTORY
#define O_DIRECTORY	00200000	/* must be a directory */
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW	00400000	/* don't follow links */
#endif
#ifndef O_NOATIME
#define O_NOATIME	01000000
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC	02000000	/* set close_on_exec */
#endif
