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

/* Native: should_close is false after init */
void test_native_should_close_initially_false(void) {
    TEST_ASSERT_TRUE_MESSAGE(glfwInit(), "glfwInit failed");
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    g_nt_window.width = 320;
    g_nt_window.height = 240;
    nt_window_init();

    TEST_ASSERT_FALSE(nt_window_should_close());

    nt_window_shutdown();
}

/* Native: request_close makes should_close true */
void test_native_request_close(void) {
    TEST_ASSERT_TRUE_MESSAGE(glfwInit(), "glfwInit failed");
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    g_nt_window.width = 320;
    g_nt_window.height = 240;
    nt_window_init();

    nt_window_request_close();
    TEST_ASSERT_TRUE(nt_window_should_close());

    nt_window_shutdown();
}

/* Native: set_vsync does not crash */
void test_native_set_vsync(void) {
    TEST_ASSERT_TRUE_MESSAGE(glfwInit(), "glfwInit failed");
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    g_nt_window.width = 320;
    g_nt_window.height = 240;
    nt_window_init();

    nt_window_set_vsync(NT_VSYNC_ON);
    nt_window_set_vsync(NT_VSYNC_OFF);
    nt_window_set_vsync(NT_VSYNC_ADAPTIVE);

    nt_window_shutdown();
}

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
/* stdnoreturn.h (pulled in above) defines noreturn -> _Noreturn, which breaks
   winnt.h's __declspec(noreturn). */
#undef noreturn
#include <stdint.h>
#include <windows.h>

/* NT_HYBRID_HPG contract: the exe's export table carries the GPU hint symbols with
   value 1 (prefer dGPU). A glfw DLL or a dropped define silently loses them. */
void test_native_hybrid_hpg_exports(void) {
    HMODULE self = GetModuleHandleW(NULL);
    FARPROC nv = GetProcAddress(self, "NvOptimusEnablement");
    FARPROC amd = GetProcAddress(self, "AmdPowerXpressRequestHighPerformance");
#if NT_TEST_HYBRID_HPG
    TEST_ASSERT_NOT_NULL(nv);
    TEST_ASSERT_NOT_NULL(amd);
    /* uintptr_t hop: a direct FARPROC->object-pointer cast trips -Wpedantic */
    TEST_ASSERT_EQUAL_UINT32(1U, *(const DWORD *)(uintptr_t)nv); // NOLINT(performance-no-int-to-ptr)
    TEST_ASSERT_EQUAL_INT(1, *(const int *)(uintptr_t)amd);      // NOLINT(performance-no-int-to-ptr)
#else
    TEST_ASSERT_NULL(nv);
    TEST_ASSERT_NULL(amd);
#endif
}
#endif

int main(void) {
    UNITY_BEGIN();
#if defined(_WIN32)
    RUN_TEST(test_native_hybrid_hpg_exports);
#endif
    RUN_TEST(test_native_window_creates);
    RUN_TEST(test_native_window_shutdown_cleans_up);
    RUN_TEST(test_native_should_close_initially_false);
    RUN_TEST(test_native_request_close);
    RUN_TEST(test_native_set_vsync);
    return UNITY_END();
}
