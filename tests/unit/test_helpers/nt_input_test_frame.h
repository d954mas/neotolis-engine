#ifndef NT_INPUT_TEST_FRAME_H
#define NT_INPUT_TEST_FRAME_H

/* Tests feed input like the window pump: nt_input_feed (gated) or nt_input_inject (bot) apply an event to the
   next frame, nt_input_poll publishes it. */

#include "input/nt_input_feed.h"

static inline nt_input_event_t nt_test_key(nt_key_t key, bool down) { return (nt_input_event_t){.kind = NT_INPUT_EV_KEY, .key = {(uint8_t)key, down}}; }
static inline nt_input_event_t nt_test_char(uint32_t cp) { return (nt_input_event_t){.kind = NT_INPUT_EV_CHAR, .cp = cp}; }
static inline nt_input_event_t nt_test_mouse(float x, float y, uint8_t buttons) { return (nt_input_event_t){.kind = NT_INPUT_EV_MOUSE_MOVE, .mouse = {x, y, buttons}}; }
static inline nt_input_event_t nt_test_mouse_buttons(uint8_t buttons) { return (nt_input_event_t){.kind = NT_INPUT_EV_MOUSE_BUTTONS, .mouse = {.buttons = buttons}}; }
static inline nt_input_event_t nt_test_leave(void) { return (nt_input_event_t){.kind = NT_INPUT_EV_MOUSE_LEAVE}; }
static inline nt_input_event_t nt_test_contact_down(uint32_t id, float x, float y, float pressure, uint8_t type, uint8_t buttons) {
    return (nt_input_event_t){.kind = NT_INPUT_EV_CONTACT_DOWN, .contact = {id, x, y, pressure, type, buttons}};
}
static inline nt_input_event_t nt_test_contact_move(uint32_t id, float x, float y, float pressure, uint8_t buttons) {
    return (nt_input_event_t){.kind = NT_INPUT_EV_CONTACT_MOVE, .contact = {id, x, y, pressure, 0, buttons}};
}
static inline nt_input_event_t nt_test_contact_up(uint32_t id) { return (nt_input_event_t){.kind = NT_INPUT_EV_CONTACT_UP, .contact = {.id = id}}; }
static inline nt_input_event_t nt_test_wheel(float dx, float dy) { return (nt_input_event_t){.kind = NT_INPUT_EV_WHEEL, .wheel = {dx, dy}}; }
static inline nt_input_event_t nt_test_focus_lost(void) { return (nt_input_event_t){.kind = NT_INPUT_EV_FOCUS_LOST}; }

/* Publish a frame with exactly one fresh press of key and nothing else held. */
static inline void nt_input_test_key_press(nt_key_t key) {
    nt_input_init();
    nt_input_feed(nt_test_key(key, true));
    nt_input_poll();
}

#endif /* NT_INPUT_TEST_FRAME_H */
