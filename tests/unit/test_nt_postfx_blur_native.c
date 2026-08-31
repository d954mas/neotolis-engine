/* Real-GL linking catches shader-pair failures that stub tests and per-stage
 * builder validation cannot detect; results depend on the active driver. */

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

/* Restore must rebuild and link the embedded shader pair through the real GL backend. */
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
