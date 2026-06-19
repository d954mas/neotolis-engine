#ifndef NT_INPUT_INTERNAL_H
#define NT_INPUT_INTERNAL_H

#include "input/nt_input.h"

/* Backend helpers — called by platform backends to feed events into shared logic.
   Coordinates are in framebuffer pixels; each backend maps from its own space. */

void nt_input_set_key(nt_key_t key, bool down);
void nt_input_pointer_down(uint32_t id, float x, float y, float pressure, uint8_t type, uint8_t buttons_mask);
void nt_input_pointer_move(uint32_t id, float x, float y, float pressure, uint8_t type, uint8_t buttons_mask);
void nt_input_pointer_up(uint32_t id);
void nt_input_wheel(float dx, float dy);
void nt_input_clear_all_keys(void);
void nt_input_clear_all_pointers(void);

/* Player gate (L2 devapi seam + test). When disabled, the public real-apply wrappers
   (set_key, pointer_*, wheel, buffer_char) early-return; the ON->OFF edge releases held
   real input. Inject calls the *_apply cores directly so it always flows. */
void nt_input_set_player_enabled(bool enabled);

/* Synthetic input injection (L1 engine capability; L2 devapi group drives it untrusted). IMMEDIATE
   only: each call stages into the inject buffer, drained whole in the next nt_input_poll (via the
   *_apply cores, bypassing the player gate). Scheduling (frame offsets, hold, gesture stride) lives
   in the devapi layer, which releases due events here on a sim-advance. Each returns true when the
   whole command is staged, false on overflow (whole-or-nothing: on false NO entry is written).
   NEVER asserts -- the same API is driven by untrusted L2. Compiled only when NT_INPUT_INJECT_ENABLED
   (devapi in the build, or tests); a pure release build drops the buffer + these symbols entirely. */
#if NT_INPUT_INJECT_ENABLED
bool nt_input_inject_key(nt_key_t key, bool down);
bool nt_input_inject_pointer(nt_inject_kind_t kind, uint32_t id, float x, float y, float pressure, uint8_t type, uint8_t buttons_mask);
/* Set the button mask on slot `id` at its CURRENT polled position when this applies (apply-time, in
   schedule order) — never moves the pointer and bakes no x/y. If the slot does not exist yet at apply
   time it is created at (0,0) (no prior pointer). Lets input.button respect a pending input.move
   queued ahead of it instead of baking a stale submit-time position. */
bool nt_input_inject_buttons(uint32_t id, uint8_t buttons_mask);
bool nt_input_inject_wheel(float dx, float dy);
bool nt_input_inject_text(const uint32_t *cps, uint32_t n);
#endif

/* Event buffering — native backend queues events here during glfwPollEvents(),
   nt_input_platform_poll() drains them with current DPR. */

void nt_input_buffer_key(nt_key_t key, bool down);
void nt_input_buffer_pointer(bool is_down, double raw_x, double raw_y, uint8_t buttons);
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
