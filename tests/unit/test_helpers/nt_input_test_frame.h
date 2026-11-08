#ifndef NT_INPUT_TEST_FRAME_H
#define NT_INPUT_TEST_FRAME_H

/* Tests feed input like a backend: events apply to the next frame, nt_input_poll publishes it. */

#include "input/nt_input_internal.h"

/* Publish a frame with nothing held and no edges. */
static inline void nt_input_test_idle(void) {
    nt_input_init();
    nt_input_poll();
}

/* Publish a frame with exactly one fresh press of key and nothing else held. */
static inline void nt_input_test_key_press(nt_key_t key) {
    nt_input_test_idle();
    nt_input_set_key(key, true);
    nt_input_poll();
}

#endif /* NT_INPUT_TEST_FRAME_H */
