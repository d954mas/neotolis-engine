#ifndef NT_CRASH_H
#define NT_CRASH_H

#include <stdint.h>

/* Crash record: on a fatal crash the engine stores one text record (game and engine
   version, GPU identity, optional game context, time, reason, fault location or stack,
   tail of WARN/ERROR log lines). The game
   reads it on the next launch, delivers it and clears it. The engine never sends it.
   Web also dispatches window event 'nt-crash' (detail.text) once the record is captured. */

/* name: web — localStorage key (several games can share one origin);
         native — record file path, UTF-8 (a relative path resolves against the cwd at crash time).
   game_version: names the game build, so the record maps to that build's symbols.
   Both strings are copied; non-empty, shorter than 256 bytes. One call per process.
   Takes one of NT_LOG_MAX_SINKS log sinks. Native: installs its own unhandled-exception filter /
   SIGABRT and fault-signal handlers, replacing earlier ones without chaining. */
void nt_crash_init(const char *name, const char *game_version);

/* Copies optional game text, replacing the previous context; empty clears it. Requires init and
   non-NULL text. Keeps at most 1023 bytes, without splitting valid UTF-8; CR/LF become spaces.
   The source may be freed immediately. Main-thread updates; capture during an update is best effort. */
void nt_crash_set_context(const char *text);

/* Copies the previous crash record into out as a NUL-terminated string without removing it and
   returns its length; 0 when there is no record or the storage is unavailable or unreadable
   (out[0] is then '\0'). The stored text is returned as it is, cut at cap - 1 bytes (and at an
   embedded NUL). Requires nt_crash_init, out != NULL and cap >= 1. */
uint32_t nt_crash_load_previous(char *out, uint32_t cap);

/* Removes the stored record. No record or unavailable storage is a no-op. Requires nt_crash_init. */
void nt_crash_clear_previous(void);

#endif // NT_CRASH_H
