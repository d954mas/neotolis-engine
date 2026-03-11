#include "input/nt_input.h"
#include "window/nt_window.h"

#include <math.h>
#include <string.h>

/* ---- Global input state ---- */

nt_input_t g_nt_input;

/* ---- File-scope statics for edge detection ---- */

static bool s_keys_current[NT_KEY_COUNT];
static bool s_keys_previous[NT_KEY_COUNT];
static float s_prev_x[NT_INPUT_MAX_POINTERS];
static float s_prev_y[NT_INPUT_MAX_POINTERS];
static nt_button_state_t
    s_prev_buttons[NT_INPUT_MAX_POINTERS][NT_BUTTON_MAX];

/* ---- Coordinate mapping ---- */

static void map_css_to_fb(float css_x, float css_y, float *fb_x,
                          float *fb_y) {
    /* RED: intentionally return 0 to make tests fail */
    (void)css_x;
    (void)css_y;
    *fb_x = 0.0F;
    *fb_y = 0.0F;
}

/* ---- Lifecycle ---- */

void nt_input_init(void) {
    memset(&g_nt_input, 0, sizeof(g_nt_input));
    memset(s_keys_current, 0, sizeof(s_keys_current));
    memset(s_keys_previous, 0, sizeof(s_keys_previous));
    memset(s_prev_x, 0, sizeof(s_prev_x));
    memset(s_prev_y, 0, sizeof(s_prev_y));
    memset(s_prev_buttons, 0, sizeof(s_prev_buttons));
    nt_input_platform_init();
}

void nt_input_poll(void) {
    /* RED: intentionally empty to make tests fail */
    nt_input_platform_poll();
}

void nt_input_shutdown(void) {
    nt_input_platform_shutdown();
    memset(&g_nt_input, 0, sizeof(g_nt_input));
}

/* ---- Key query functions (RED: return false) ---- */

bool nt_input_key_is_down(nt_key_t key) {
    (void)key;
    return false;
}

bool nt_input_key_is_pressed(nt_key_t key) {
    (void)key;
    return false;
}

bool nt_input_key_is_released(nt_key_t key) {
    (void)key;
    return false;
}

bool nt_input_any_key_pressed(void) { return false; }

/* ---- Mouse convenience helpers (RED: return false) ---- */

bool nt_input_mouse_is_down(nt_button_t button) {
    (void)button;
    return false;
}

bool nt_input_mouse_is_pressed(nt_button_t button) {
    (void)button;
    return false;
}

bool nt_input_mouse_is_released(nt_button_t button) {
    (void)button;
    return false;
}

/* ---- Internal helpers (RED: minimal stubs) ---- */

void nt_input_set_key(nt_key_t key, bool down) {
    (void)key;
    (void)down;
    /* RED: do nothing */
}

void nt_input_pointer_down(uint32_t id, float css_x, float css_y,
                           float pressure, uint8_t type,
                           uint8_t buttons_mask) {
    float fb_x = 0.0F;
    float fb_y = 0.0F;
    map_css_to_fb(css_x, css_y, &fb_x, &fb_y);
    (void)fb_x;
    (void)fb_y;
    (void)id;
    (void)pressure;
    (void)type;
    (void)buttons_mask;
    /* RED: do nothing with mapped coords */
}

void nt_input_pointer_move(uint32_t id, float css_x, float css_y,
                           float pressure, uint8_t buttons_mask) {
    (void)id;
    (void)css_x;
    (void)css_y;
    (void)pressure;
    (void)buttons_mask;
    /* RED: do nothing */
}

void nt_input_pointer_up(uint32_t id) {
    (void)id;
    /* RED: do nothing */
}

void nt_input_wheel(float dx, float dy) {
    (void)dx;
    (void)dy;
    /* RED: do nothing */
}

void nt_input_clear_all_keys(void) {
    /* RED: do nothing */
}
