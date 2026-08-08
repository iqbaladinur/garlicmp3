#ifndef VERSION_H
#define VERSION_H

/* Manual semantic version — bump on each release. */
#define GARLICMP3_VERSION "1.1.0"

/* Git short hash injected by the Makefile (-DGARLICMP3_GIT_HASH=...). */
#ifndef GARLICMP3_GIT_HASH
#define GARLICMP3_GIT_HASH "unknown"
#endif

#endif
