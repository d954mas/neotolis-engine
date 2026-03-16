#include "input/nt_input_internal.h"

/* No-op platform backend for headless builds and testing. */

void nt_input_platform_init(void) {}

void nt_input_platform_poll(void) {}

void nt_input_platform_shutdown(void) {}

/* Buffer stubs — nt_input_internal.h declares these for the native callback
   path. Stub backend never receives events but must satisfy the linker. */

void nt_input_buffer_key(nt_key_t key, bool down) {
    (void)key;
    (void)down;
}

void nt_input_buffer_pointer(bool is_down, double raw_x, double raw_y, uint8_t buttons) {
    (void)is_down;
    (void)raw_x;
    (void)raw_y;
    (void)buttons;
}

void nt_input_buffer_wheel(float dx, float dy) {
    (void)dx;
    (void)dy;
}

void nt_input_buffer_focus_lost(void) {}
