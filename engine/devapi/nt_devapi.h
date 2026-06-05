#ifndef NT_DEVAPI_H
#define NT_DEVAPI_H

/* Debug command bus for live introspection + input emulation.
 *
 * Subsystems register named endpoints; a transport (TCP on native now, a
 * WebSocket adapter later for the browser) feeds one text command line per
 * request and returns one JSON line per response. Keeping requests as plain
 * tokenized lines means the engine never needs a JSON *parser* -- it only
 * writes JSON. Debug-only: everything compiles to no-ops unless
 * NT_DEVAPI_ENABLED is defined to 1 (mirrors the NT_UI_DEBUG_TOOLS gate).
 *
 * Protocol (line in -> JSON line out):
 *   "ping"                  -> {"ok":true}
 *   "endpoints"             -> {"ok":true,"data":["ui.tree",...]}
 *   "ui.tree"               -> {"ok":true,"data":[{"id":..,"type":..,"x":..}, ...]}
 *   "entity.list"           -> {"ok":true,"data":[{"entity":..,"x":..}, ...]}
 *   "input.key P tap"       -> {"ok":true}     (tap | down | up)
 *   "input.click 640 360"   -> {"ok":true}     (optional: left|right|middle)
 *   "input.move 100 200"    -> {"ok":true}
 */

#include <stdbool.h>
#include <stdint.h>

#include "input/nt_input.h"

typedef struct nt_ui_context nt_ui_context_t;

#ifndef NT_DEVAPI_ENABLED
#define NT_DEVAPI_ENABLED 0
#endif

#if NT_DEVAPI_ENABLED

/* Endpoint handler. argv = whitespace tokens AFTER the endpoint name.
 * Write a JSON value (no trailing newline) into out (cap = out_cap) and return
 * its byte length, or -1 on error. */
typedef int (*nt_devapi_handler_fn)(int argc, char **argv, char *out, int out_cap, void *user);

void nt_devapi_init(void);
void nt_devapi_shutdown(void);

/* Register an endpoint. name must be a stable string. Returns false if full. */
bool nt_devapi_register(const char *name, nt_devapi_handler_fn fn, void *user);

/* Route one command line into a JSON response line. Returns response length. */
int nt_devapi_dispatch(const char *line, char *out, int out_cap);

/* UI endpoints read this context (set after the game creates it). */
void nt_devapi_set_ui_context(const nt_ui_context_t *ctx);

/* Logical<->framebuffer mapping so input.click_ui can target a UI element by id.
 * Call once per frame with the current scale (EXPAND: no letterbox). */
void nt_devapi_set_view(float fb_w, float fb_h, float logical_w, float logical_h);

/* Register the built-in ui.* / entity.* / input.* endpoints. */
void nt_devapi_register_builtins(void);

/* Apply queued synthetic input. Call in frame() right AFTER nt_input_poll(). */
void nt_devapi_apply_pending(void);

/* TCP transport (native): non-blocking localhost server, polled per frame. */
bool nt_devapi_net_start(uint16_t port);
void nt_devapi_net_poll(void);
void nt_devapi_net_stop(void);

#else /* stubs */

static inline void nt_devapi_init(void) {}
static inline void nt_devapi_shutdown(void) {}
static inline void nt_devapi_register_builtins(void) {}
static inline void nt_devapi_set_ui_context(const nt_ui_context_t *ctx) { (void)ctx; }
static inline void nt_devapi_set_view(float fb_w, float fb_h, float logical_w, float logical_h) {
    (void)fb_w;
    (void)fb_h;
    (void)logical_w;
    (void)logical_h;
}
static inline void nt_devapi_apply_pending(void) {}
static inline bool nt_devapi_net_start(uint16_t port) {
    (void)port;
    return false;
}
static inline void nt_devapi_net_poll(void) {}
static inline void nt_devapi_net_stop(void) {}

#endif /* NT_DEVAPI_ENABLED */

#endif /* NT_DEVAPI_H */
