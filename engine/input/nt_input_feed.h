#ifndef NT_INPUT_FEED_H
#define NT_INPUT_FEED_H

#include "input/nt_input.h"

/* Input feed: the window module's platform pump and the devapi input group hand events to the core
   here, on the game thread. Each event applies to the next frame in arrival order; nt_input_poll
   publishes it. Coordinates are in framebuffer pixels. */

typedef enum {
    NT_INPUT_EV_KEY = 0,
    NT_INPUT_EV_CHAR,          /* a typed UTF-32 codepoint */
    NT_INPUT_EV_MOUSE_MOVE,    /* a real pump reports a button change as a move with the held buttons */
    NT_INPUT_EV_MOUSE_BUTTONS, /* bot only: buttons at the mouse's current position; no-op off the window */
    NT_INPUT_EV_MOUSE_LEAVE,   /* ignored while a button is held: a drag outside keeps the mouse */
    NT_INPUT_EV_CONTACT_DOWN,  /* touch/pen: a down creates a contact, a move for an unknown id is ignored */
    NT_INPUT_EV_CONTACT_MOVE,
    NT_INPUT_EV_CONTACT_UP,
    NT_INPUT_EV_WHEEL, /* notches, on the mouse */
    NT_INPUT_EV_FOCUS_LOST,
} nt_input_event_kind_t;

typedef struct {
    uint8_t kind; /* nt_input_event_kind_t */
    union {
        struct {
            uint8_t key; /* nt_key_t */
            bool down;
        } key;
        uint32_t cp;
        struct {
            float x, y;
            uint8_t buttons; /* {1,2,4} = left, right, middle */
        } mouse;
        struct {
            uint32_t id;
            float x, y, pressure;
            uint8_t type; /* NT_POINTER_TOUCH or NT_POINTER_PEN */
            uint8_t buttons;
        } contact; /* UP reads only id */
        struct {
            float dx, dy;
        } wheel;
    };
} nt_input_event_t;

/* A real device event: dropped while the player gate is closed. */
void nt_input_feed(nt_input_event_t e);

/* A bot event: always applies. The devapi group validates untrusted commands; a bad event asserts. */
void nt_input_inject(nt_input_event_t e);

/* Closed while a bot drives the host, so a real mouse cannot move the bot's pointer. Closing releases
   nothing: whoever pressed releases. */
void nt_input_set_player_enabled(bool enabled);

#endif /* NT_INPUT_FEED_H */
