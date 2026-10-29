#ifndef NT_INPUT_INTERNAL_H
#define NT_INPUT_INTERNAL_H

#include "input/nt_input.h"

/* Backend helpers — called by platform backends to feed events into shared logic.
   Coordinates are in framebuffer pixels; each backend maps from its own space. */

void nt_input_set_key(nt_key_t key, bool down);
/* The mouse: every real mouse event is a move with the current button mask (a release keeps the mouse).
   A leave is ignored while a button is held, so a drag outside the window keeps it. */
void nt_input_mouse_move(float x, float y, uint8_t buttons_mask);
void nt_input_mouse_leave(void);
/* Touch/pen contacts (type NT_POINTER_TOUCH or NT_POINTER_PEN): a down creates one, a move for an
   unknown id is ignored. */
void nt_input_pointer_down(uint32_t id, float x, float y, float pressure, uint8_t type, uint8_t buttons_mask);
void nt_input_pointer_move(uint32_t id, float x, float y, float pressure, uint8_t type, uint8_t buttons_mask);
void nt_input_pointer_up(uint32_t id);
void nt_input_wheel(float dx, float dy);
void nt_input_clear_all_keys(void);
void nt_input_clear_all_pointers(void);
/* Backends call this on focus loss: the release events never arrive, so held input releases here. */
void nt_input_focus_lost(void);

/* nt_input automation surface (player gate + synthetic input injection). Compiled only when
   NT_INPUT_AUTOMATION_ENABLED (devapi input group in the build, or tests); a lean build drops the
   gate, the inject buffer, and all these symbols entirely.

   Player gate: when disabled the public real-apply wrappers (set_key, mouse_move, mouse_leave,
   pointer_*, wheel, buffer_char, focus_lost) early-return. It releases nothing: whoever pressed releases.
   Inject calls the *_apply cores directly so it always flows.

   Inject API (L1 engine capability; L2 devapi group drives it untrusted): IMMEDIATE only — each call
   stages into the inject buffer, drained whole in the next nt_input_poll. Returns true when the whole
   command is staged, false on overflow OR invalid input (whole-or-nothing: on false NO entry is
   written). NEVER asserts; validates defensively (out-of-range key / kind / type / mask -> false). */
#if NT_INPUT_AUTOMATION_ENABLED
void nt_input_set_player_enabled(bool enabled);
bool nt_input_inject_key(nt_key_t key, bool down);
/* A touch/pen contact event: kind POINTER_DOWN/MOVE/UP, type touch or pen. */
bool nt_input_inject_pointer(nt_inject_kind_t kind, uint32_t id, float x, float y, float pressure, uint8_t type, uint8_t buttons_mask);
bool nt_input_inject_mouse(float x, float y, uint8_t buttons_mask);
/* The mouse's button mask at its position when this applies (schedule order), so a pending
   inject_mouse queued ahead of it is honored. No-op while the mouse is not over the window. */
bool nt_input_inject_mouse_buttons(uint8_t buttons_mask);
bool nt_input_inject_wheel(float dx, float dy);
bool nt_input_inject_text(const uint32_t *cps, uint32_t n);
#endif

/* Event buffering — native backend queues events here during glfwPollEvents(),
   nt_input_platform_poll() drains them with current DPR. */

void nt_input_buffer_key(nt_key_t key, bool down);
void nt_input_buffer_mouse(double raw_x, double raw_y, uint8_t buttons);
void nt_input_buffer_mouse_leave(void);
void nt_input_buffer_wheel(float dx, float dy);
void nt_input_buffer_focus_lost(void);

/* Stage a typed UTF-32 codepoint from the native GLFW char callback. The callback fires during
   glfwPollEvents() (before nt_input_poll clears the ring), so it must NOT write the ring directly;
   platform_poll drains these into the ring AFTER the clear (symmetric with nt_input_buffer_key). */
void nt_input_buffer_char_event(uint32_t cp);

/* Push a typed UTF-32 codepoint into the shared char ring. Lives in nt_input.c
   (platform-agnostic), called by the web char source + native platform_poll drain. Drop-when-full. */
void nt_input_buffer_char(uint32_t cp);

/* Platform lifecycle — implemented by each backend (web, native, stub). */

void nt_input_platform_init(void);
void nt_input_platform_poll(void);
void nt_input_platform_shutdown(void);

#endif /* NT_INPUT_INTERNAL_H */
