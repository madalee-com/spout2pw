/*
 * debug_log.h - Shared debug logging for spout2pw Windows targets
 *
 * Provides spout2pw_dbg(), a variadic stderr logger that is silent unless
 * debug mode is enabled (via the SPOUT2PW_DEBUG environment variable or the
 * global spout2pw_debug flag set by WinMain).
 */

#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

/* Set by WinMain when --debug is passed on the command line. */
extern int spout2pw_debug;

/* Set to 1 by DllMain once __wine_init_unix_call() succeeds. Used by
 * ServiceMain to tolerate a redundant lookup failure when the handle was
 * already established (Wine may re-invoke ServiceMain in a context where
 * find_builtin_dll() can't re-find the module, even though the handle is
 * valid). */
extern int spout2pw_unixlib_ready;

void spout2pw_dbg(const char *fmt, ...);

#endif /* DEBUG_LOG_H */
