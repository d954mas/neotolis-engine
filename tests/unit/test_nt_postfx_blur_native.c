/* Real-GL link coverage for the blur helper's embedded shaders.
 *
 * test_nt_postfx_blur runs on the stub backend, which never calls glLinkProgram,
 * so a pair that compiles stage-by-stage and fails only at link went unnoticed
 * until an example aborted. The builder has the same blind spot: it validates
 * each stage separately and never links a pair. This test is the only thing
 * that links these two shaders.
 *
 * It only bites on drivers that fold branches before checking array bounds --
 * NVIDIA does, Mesa/llvmpipe (what CI runs under xvfb) accepts the rejected
 * form. A green CI run is therefore not proof that the shaders are portable. */

#include "graphics/nt_gfx.h"
#include "postfx/nt_postfx_blur.h"
#include "unity.h"
#include "window/nt_window.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>

void setUp(void) {
    TEST_ASSERT_TRUE_MESSAGE(glfwInit(), "glfwInit failed");
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    g_nt_window = (nt_window_t){
        .max_dpr = 1.0F,
        .resizable = false,
        .width = 64,
        .height = 64,
    };
    nt_window_init();

    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    nt_gfx_init(&desc);
    TEST_ASSERT_TRUE(g_nt_gfx.initialized);
}

void tearDown(void) {
    nt_gfx_shutdown();
    nt_window_shutdown();
}

static void test_blur_program_links_on_real_gl(void) {
    TEST_ASSERT_EQUAL_INT(NT_OK, nt_postfx_blur_init());
    nt_postfx_blur_shutdown();
}

/* Context restore relinks the same pair; a driver that rejects it would leave
 * the helper permanently uninitialized rather than failing at startup. */
static void test_blur_program_relinks_on_restore(void) {
    TEST_ASSERT_EQUAL_INT(NT_OK, nt_postfx_blur_init());
    TEST_ASSERT_EQUAL_INT(NT_OK, nt_postfx_blur_restore_gpu());
    nt_postfx_blur_shutdown();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_blur_program_links_on_real_gl);
    RUN_TEST(test_blur_program_relinks_on_restore);
    return UNITY_END();
}
