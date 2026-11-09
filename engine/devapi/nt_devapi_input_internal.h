#ifndef NT_DEVAPI_INPUT_INTERNAL_H
#define NT_DEVAPI_INPUT_INTERNAL_H

#include "input/nt_input_feed.h"

#include <stdbool.h>
#include <stdint.h>

/* Input-group internals: the inject scheduler shared with the ui group (ui resolves coords then reuses
   this one scheduler). Only included by group-on TUs. */

/* Bounded BSS schedule cap (-D overridable). Lives here, not in nt_devapi_input.c, so the ui drag cap +
   the unit tests derive their sizes from the real cap. */
#ifndef NT_DEVAPI_INPUT_SCHED_MAX
#define NT_DEVAPI_INPUT_SCHED_MAX 256
#endif

/* Reuse surface for the ui group: it resolves coords then DELEGATES scheduling here, so there is one
   scheduler. Whole-or-nothing: preflight can_reserve(N), then issue N
   nt_devapi_input_sched calls (single-threaded -> atomic). */
bool nt_devapi_input_sched_can_reserve(uint32_t n);
/* Inject ev at the at_frame-th sim-advance from now (0 = the next). A mouse buttons event applies at
   the mouse's position then, so a queued move is honored. */
void nt_devapi_input_sched(nt_input_event_t ev, uint16_t at_frame);

#endif /* NT_DEVAPI_INPUT_INTERNAL_H */
