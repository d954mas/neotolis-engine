#ifndef NT_DEVAPI_INPUT_INTERNAL_H
#define NT_DEVAPI_INPUT_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "input/nt_input.h" /* nt_inject_kind_t for the reuse-wrapper signature below. */

/* Input-group internals: the inject scheduler shared with the ui group (ui resolves coords then reuses
   this one scheduler) + the tick/reset hooks, exposed to the input/ui unit tests. Kept out of the core
   internal header so the core never knows the input group's scheduler. Only included by group-on TUs. */

/* Bounded BSS schedule cap (-D overridable). Lives here, not in nt_devapi_input.c, so the ui drag cap +
   the unit tests derive their sizes from the real cap. */
#ifndef NT_DEVAPI_INPUT_SCHED_MAX
#define NT_DEVAPI_INPUT_SCHED_MAX 256
#endif

/* Reuse surface for the ui group: it resolves coords then DELEGATES scheduling here, so there is one
   scheduler on the immediate buffer. Whole-or-nothing: preflight can_reserve(N), then issue N
   sched_* calls (single-threaded -> atomic). */
bool nt_devapi_input_sched_can_reserve(uint32_t n);
bool nt_devapi_input_sched_pointer(nt_inject_kind_t kind, uint32_t id, float x, float y, float pressure, uint8_t type, uint8_t buttons_mask, uint16_t at_frame);
bool nt_devapi_input_sched_wheel(float dx, float dy, uint16_t at_frame);

/* Reset hook: drop pending schedule entries only (applied input, its copy and the advance clock stay). */
void nt_devapi_input_reset(void);

#endif /* NT_DEVAPI_INPUT_INTERNAL_H */
