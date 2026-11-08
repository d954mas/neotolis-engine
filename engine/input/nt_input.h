#ifndef NT_INPUT_H
#define NT_INPUT_H

#include "core/nt_types.h"

/* ---- Key enum (physical keys, layout-independent) ---- */

typedef enum {
    NT_KEY_A = 0,
    NT_KEY_B,
    NT_KEY_C,
    NT_KEY_D,
    NT_KEY_E,
    NT_KEY_F,
    NT_KEY_G,
    NT_KEY_H,
    NT_KEY_I,
    NT_KEY_J,
    NT_KEY_K,
    NT_KEY_L,
    NT_KEY_M,
    NT_KEY_N,
    NT_KEY_O,
    NT_KEY_P,
    NT_KEY_Q,
    NT_KEY_R,
    NT_KEY_S,
    NT_KEY_T,
    NT_KEY_U,
    NT_KEY_V,
    NT_KEY_W,
    NT_KEY_X,
    NT_KEY_Y,
    NT_KEY_Z,
    NT_KEY_0, // NOLINT(misc-confusable-identifiers)
    NT_KEY_1, // NOLINT(misc-confusable-identifiers)
    NT_KEY_2,
    NT_KEY_3,
    NT_KEY_4,
    NT_KEY_5,
    NT_KEY_6,
    NT_KEY_7,
    NT_KEY_8,
    NT_KEY_9,
    NT_KEY_ARROW_UP,
    NT_KEY_ARROW_DOWN,
    NT_KEY_ARROW_LEFT,
    NT_KEY_ARROW_RIGHT,
    NT_KEY_SPACE,
    NT_KEY_ENTER,
    NT_KEY_ESCAPE,
    NT_KEY_TAB,
    NT_KEY_BACKSPACE,
    NT_KEY_LSHIFT,
    NT_KEY_RSHIFT,
    NT_KEY_LCTRL,
    NT_KEY_RCTRL,
    NT_KEY_LALT,
    NT_KEY_RALT,
    NT_KEY_F1,
    NT_KEY_F2,
    NT_KEY_F3,
    NT_KEY_F4,
    NT_KEY_F5,
    NT_KEY_F6,
    NT_KEY_F7,
    NT_KEY_F8,
    NT_KEY_F9,
    NT_KEY_F10,
    NT_KEY_F11,
    NT_KEY_F12,
    NT_KEY_DELETE,
    NT_KEY_INSERT,
    NT_KEY_HOME,
    NT_KEY_END,
    NT_KEY_PAGE_UP,
    NT_KEY_PAGE_DOWN,
    NT_KEY_COUNT /* Must be last -- array size sentinel */
} nt_key_t;

/* ---- Pointer types ---- */

typedef enum {
    NT_POINTER_MOUSE = 0,
    NT_POINTER_TOUCH,
    NT_POINTER_PEN,
} nt_pointer_type_t;

/* ---- Button enum and state ---- */

typedef enum {
    NT_BUTTON_LEFT = 0,
    NT_BUTTON_RIGHT,
    NT_BUTTON_MIDDLE,
    NT_BUTTON_MAX /* Array size sentinel */
} nt_button_t;

typedef struct {
    bool is_down;
    bool is_pressed;
    bool is_released;
} nt_button_state_t;

/* ---- Pointer state ---- */

#define NT_INPUT_MAX_POINTERS 8

typedef struct {
    uint32_t id;             /* touch/pen: the platform contact id; 0 for the mouse */
    float x, y;              /* Framebuffer pixels, canvas-relative */
    float dx, dy;            /* Movement delta in framebuffer pixels */
    float wheel_dx;          /* Wheel delta in NOTCHES: 1.0 == one physical detent (platform edge
                                normalizes; trackpads deliver fractional notches). Consumers scale
                                by px-per-notch, not here. */
    float wheel_dy;          /* Vertical wheel delta in notches (see wheel_dx). Sign: positive ==
                                content scrolls down, matching DOM WheelEvent.deltaY (web is canonical;
                                the native GLFW edge negates yoffset to agree). */
    float pressure;          /* 0.0-1.0 */
    uint8_t type;            /* nt_pointer_type_t */
    bool active;             /* Pointer currently exists */
    bool deactivate_pending; /* an up or a mouse leave defers deactivation by one frame */
    nt_button_state_t buttons[NT_BUTTON_MAX];
} nt_pointer_t;

/* ---- Global input state ---- */

/* pointers[0] is the mouse: active while the cursor is over the window or a button is held, and a
   button release keeps it. Touch and pen contacts take slots 1.., each from its down to its up.
   primary indexes the pointer a single-pointer consumer follows (updated by nt_input_poll): taken
   by a press, or by hovering while nothing is held, and kept from the press through its release
   frame. A contact already held when the primary ends never takes over. nt_input_poll publishes
   it; consumers only read it. */
typedef struct {
    nt_pointer_t pointers[NT_INPUT_MAX_POINTERS];
    uint8_t primary;
} nt_input_t;

extern nt_input_t g_nt_input;

/* ---- Text input mode ---- */

/* Hint to the platform on-screen keyboard / text-entry surface (mobile soft keyboard). Web maps
 * these to the <canvas> inputmode/type; native + stub are no-ops. The web EM_JS depends on the
 * numeric values (PASSWORD == 4) -- keep them in sync. */
typedef enum {
    NT_TEXT_INPUT_TEXT = 0,
    NT_TEXT_INPUT_NUMERIC,
    NT_TEXT_INPUT_EMAIL,
    NT_TEXT_INPUT_URL,
    NT_TEXT_INPUT_PASSWORD,
} nt_text_input_mode_t;

/* ---- Key query functions ---- */

bool nt_input_key_is_down(nt_key_t key);
bool nt_input_key_is_pressed(nt_key_t key);
bool nt_input_key_is_released(nt_key_t key);
bool nt_input_any_key_pressed(void);

/* Typed UTF-32 characters per frame; later ones in the same frame are dropped. Exposed so the
   devapi layer can reject a text batch that could never land whole. */
#ifndef NT_INPUT_CHAR_MAX
#define NT_INPUT_CHAR_MAX 32
#endif

/* Read the next character typed before the last poll (FIFO). Returns false and leaves *out
   untouched when none is left. */
bool nt_input_pop_char(uint32_t *out_codepoint);

/* Hint the platform on-screen keyboard for the focused text field (mobile soft keyboard).
   No-op on native/stub. Apply it when a field gains focus. */
void nt_input_set_text_input_mode(nt_text_input_mode_t mode);

/* ---- nt_input automation surface (player gate + synthetic input injection) ---- */

/* Capability gate for the whole nt_input automation surface (inject pipeline + player gate). The
   sole driver is the devapi input group; CMake derives this from it. Default 0 -> the lean apply
   layer (no gate, no inject symbols), so a consumer that forgets to wire it stays lean. */
#ifndef NT_INPUT_AUTOMATION_ENABLED
#define NT_INPUT_AUTOMATION_ENABLED 0
#endif

/* Event kinds the devapi schedule releases through the inject API. */
typedef enum {
    NT_INJECT_KEY = 0,       /* key down/up */
    NT_INJECT_MOUSE_MOVE,    /* the mouse at (x,y) with a button mask */
    NT_INJECT_MOUSE_BUTTONS, /* the mouse's button mask at its CURRENT position (no-op while it is not over the window) */
    NT_INJECT_POINTER_DOWN,  /* touch/pen contact create/press */
    NT_INJECT_POINTER_MOVE,  /* touch/pen contact move */
    NT_INJECT_POINTER_UP,    /* touch/pen contact release + deactivate-pending */
    NT_INJECT_WHEEL,         /* the mouse only */
    NT_INJECT_CHAR,          /* a typed codepoint */
} nt_inject_kind_t;

/* ---- Mouse convenience helpers ---- */

bool nt_input_mouse_is_down(nt_button_t button);
bool nt_input_mouse_is_pressed(nt_button_t button);
bool nt_input_mouse_is_released(nt_button_t button);

/* ---- Lifecycle ---- */

void nt_input_init(void);

/* Publish the input that reached the core since the previous poll and start the next frame. Call
   once per frame AFTER nt_window_poll(). No schedule: the devapi layer releases due inject events
   before this poll. */
void nt_input_poll(void);

void nt_input_shutdown(void);

#endif /* NT_INPUT_H */
