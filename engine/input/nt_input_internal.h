#ifndef NT_INPUT_INTERNAL_H
#define NT_INPUT_INTERNAL_H

#include "input/nt_input.h"

/* Backend entry points. A backend calls them on the game thread as each platform event reaches
   it; they apply to the next frame's state in that order, and nt_input_poll publishes it.
   Coordinates are in framebuffer pixels; each backend maps from its own space. */

void nt_input_set_key(nt_key_t key, bool down);
/* A typed UTF-32 codepoint. */
void nt_input_buffer_char(uint32_t cp);
/* The mouse: every real mouse event is a move with the current button mask (a release keeps the mouse).
   A leave is ignored while a button is held, so a drag outside the window keeps it. */
void nt_input_mouse_move(float x, float y, uint8_t buttons_mask);
void nt_input_mouse_leave(void);
/* Touch/pen contacts (type NT_POINTER_TOUCH or NT_POINTER_PEN): a down creates one, a move for an
   unknown id is ignored. */
void nt_input_pointer_down(uint32_t id, float x, float y, float pressure, uint8_t type, uint8_t buttons_mask);
void nt_input_pointer_move(uint32_t id, float x, float y, float pressure, uint8_t buttons_mask);
void nt_input_pointer_up(uint32_t id);
void nt_input_wheel(float dx, float dy);
/* The window lost focus: the release events never arrive, so held input releases here, in order
   with the events around it. */
void nt_input_focus_lost(void);

/* nt_input automation surface (player gate + synthetic input injection). Compiled only when
   NT_INPUT_AUTOMATION_ENABLED (devapi input group in the build, or tests); a lean build drops the
   gate and all these symbols entirely.

   Player gate: when disabled the real-input entry points above (including focus_lost) early-return,
   judging each event when it reaches the core. It releases nothing: whoever pressed releases.

   Inject API (L1 engine capability; L2 devapi group drives it untrusted): applies at the call through
   the *_apply cores, past the gate. Returns false on invalid input and applies nothing. NEVER
   asserts; validates defensively (out-of-range key / kind / type / mask -> false). */
#if NT_INPUT_AUTOMATION_ENABLED
void nt_input_set_player_enabled(bool enabled);
bool nt_input_inject_key(nt_key_t key, bool down);
/* A touch/pen contact event: kind POINTER_DOWN/MOVE/UP, type touch or pen. */
bool nt_input_inject_pointer(nt_inject_kind_t kind, uint32_t id, float x, float y, float pressure, uint8_t type, uint8_t buttons_mask);
bool nt_input_inject_mouse(float x, float y, uint8_t buttons_mask);
/* The mouse's button mask at its current position. No-op while the mouse is not over the window. */
bool nt_input_inject_mouse_buttons(uint8_t buttons_mask);
void nt_input_inject_wheel(float dx, float dy);
void nt_input_inject_char(uint32_t cp);
#endif

/* Platform hooks — implemented by each backend (web, stub). poll runs first in nt_input_poll, for
   a backend that stages events outside the frame (web). */

void nt_input_platform_init(void);
void nt_input_platform_poll(void);

#endif /* NT_INPUT_INTERNAL_H */
