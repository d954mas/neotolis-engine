#ifndef NT_INPUT_INTERNAL_H
#define NT_INPUT_INTERNAL_H

#include "input/nt_input.h"

/* Backend entry points. The window module's platform pump (nt_window_poll) calls them on the game
   thread for each platform event, in arrival order; they apply to the next frame's state, and
   nt_input_poll publishes it. Coordinates are in framebuffer pixels; the pump maps from its own
   space. */

void nt_input_set_key(nt_key_t key, bool down);
/* A typed UTF-32 codepoint. */
void nt_input_char(uint32_t cp);
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

   Inject API (L1 engine capability): applies at the call through the *_apply cores, past the gate.
   The devapi group validates the untrusted command first; an invalid argument here asserts. */
#if NT_INPUT_AUTOMATION_ENABLED
void nt_input_set_player_enabled(bool enabled);
void nt_input_inject_key(nt_key_t key, bool down);
/* Touch/pen contacts (type touch or pen), like the backend entry points. */
void nt_input_inject_pointer_down(uint32_t id, float x, float y, float pressure, uint8_t type, uint8_t buttons_mask);
void nt_input_inject_pointer_move(uint32_t id, float x, float y, float pressure, uint8_t buttons_mask);
void nt_input_inject_pointer_up(uint32_t id);
void nt_input_inject_mouse(float x, float y, uint8_t buttons_mask);
/* The mouse's button mask at its current position. No-op while the mouse is not over the window. */
void nt_input_inject_mouse_buttons(uint8_t buttons_mask);
void nt_input_inject_wheel(float dx, float dy);
void nt_input_inject_char(uint32_t cp);
#endif

#endif /* NT_INPUT_INTERNAL_H */
