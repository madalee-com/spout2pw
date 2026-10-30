/*
 * debug_log.c - Shared debug logging for spout2pw Windows targets
 */

#define _POSIX_C_SOURCE 200809L

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "debug_log.h"

/* Global debug flag: set by WinMain (via --debug) or SPOUT2PW_DEBUG env var. */
int spout2pw_debug = 0;

/* Set to 1 by DllMain once __wine_init_unix_call() succeeds. */
int spout2pw_unixlib_ready = 0;

void spout2pw_dbg(const char *fmt, ...) {
    if (!spout2pw_debug && getenv("SPOUT2PW_DEBUG") == NULL)
        return;
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fflush(stderr);
}
