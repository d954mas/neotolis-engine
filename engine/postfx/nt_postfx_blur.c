#include "postfx/nt_postfx_blur.h"

#include "core/nt_assert.h"

static struct {
    bool initialized;
#ifdef NT_TEST_ACCESS
    uint32_t draw_count;
#endif
} s_blur;

nt_result_t nt_postfx_blur_init(void) {
    s_blur.initialized = true;
    return NT_OK;
}

void nt_postfx_blur_shutdown(void) { s_blur.initialized = false; }

void nt_postfx_blur_restore_gpu(void) {
    if (!s_blur.initialized) {
        return;
    }
}

bool nt_postfx_blur_gaussian(const nt_postfx_blur_pass_t *pass) {
    (void)pass;
    return false;
}

#ifdef NT_TEST_ACCESS
uint32_t nt_postfx_blur_test_build_kernel(float radius, float sigma, float out_weights[NT_POSTFX_BLUR_MAX_KERNEL]) {
    (void)radius;
    (void)sigma;
    NT_ASSERT(out_weights != NULL);
    if (out_weights != NULL) {
        out_weights[0] = 0.0F;
    }
    return 0;
}

uint32_t nt_postfx_blur_test_draw_count(void) { return s_blur.draw_count; }

void nt_postfx_blur_test_reset_counters(void) { s_blur.draw_count = 0; }
#endif
