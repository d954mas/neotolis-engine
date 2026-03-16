#include "unity.h"
#include "window/nt_window.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

/* Suppress GLFW/GLX internal leaks (X11 extension query cache) */
const char *__lsan_default_suppressions(void);                                           // NOLINT(bugprone-reserved-identifier)
const char *__lsan_default_suppressions(void) { return "leak:extensionSupportedGLX\n"; } // NOLINT(bugprone-reserved-identifier)

void setUp(void) { g_nt_window = (nt_window_t){.max_dpr = 2.0F, .resizable = true}; }

void tearDown(void) {}

/* GLFW init + hidden window + GL 3.3 context via nt_window_init() */
void test_native_window_creates(void) {
    /* Pre-init GLFW so we can set VISIBLE=FALSE before nt_window_init creates
       the window. glfwInit() is idempotent; hints persist until reset. */
    TEST_ASSERT_TRUE_MESSAGE(glfwInit(), "glfwInit failed");
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    g_nt_window.width = 320;
    g_nt_window.height = 240;

    nt_window_init();

    TEST_ASSERT_EQUAL_UINT32(320, g_nt_window.width);
    TEST_ASSERT_EQUAL_UINT32(240, g_nt_window.height);
    TEST_ASSERT_TRUE(g_nt_window.fb_width > 0);
    TEST_ASSERT_TRUE(g_nt_window.fb_height > 0);
    TEST_ASSERT_TRUE(g_nt_window.dpr >= 1.0F);

    nt_window_shutdown();
}

/* Shutdown leaves clean state */
void test_native_window_shutdown_cleans_up(void) {
    TEST_ASSERT_TRUE_MESSAGE(glfwInit(), "glfwInit failed");
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    g_nt_window.width = 320;
    g_nt_window.height = 240;

    nt_window_init();
    nt_window_shutdown();

    /* After shutdown, re-init should work (proves cleanup was complete) */
    g_nt_window = (nt_window_t){.max_dpr = 2.0F, .resizable = true};
    g_nt_window.width = 640;
    g_nt_window.height = 480;

    TEST_ASSERT_TRUE_MESSAGE(glfwInit(), "glfwInit after shutdown failed");
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    nt_window_init();

    TEST_ASSERT_EQUAL_UINT32(640, g_nt_window.width);
    TEST_ASSERT_EQUAL_UINT32(480, g_nt_window.height);
    TEST_ASSERT_TRUE(g_nt_window.fb_width > 0);

    nt_window_shutdown();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_native_window_creates);
    RUN_TEST(test_native_window_shutdown_cleans_up);
    return UNITY_END();
}
