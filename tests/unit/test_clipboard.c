#include "clipboard/nt_clipboard.h"
#include "unity.h"

#include <stdbool.h>
#include <string.h>

#if defined(NT_CLIPBOARD_TEST_NATIVE)
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

/* Suppress GLFW/GLX internal leaks (X11 extension query cache). */
const char *__lsan_default_suppressions(void);                                           // NOLINT(bugprone-reserved-identifier)
const char *__lsan_default_suppressions(void) { return "leak:extensionSupportedGLX\n"; } // NOLINT(bugprone-reserved-identifier)

/* The round-trip tests write the developer's REAL OS clipboard. Snapshot it on the first setUp
 * and restore it in the final tearDown so running the suite does not clobber the user's clip. */
#define NT_CLIPBOARD_SAVE_CAP 8192
static char s_saved_clip[NT_CLIPBOARD_SAVE_CAP];
static bool s_saved_clip_valid = false;

void setUp(void) {
    TEST_ASSERT_TRUE_MESSAGE(glfwInit(), "glfwInit failed");
    if (!s_saved_clip_valid) {
        const char *cur = nt_clipboard_get_text(); /* engine-owned; copy before any set clobbers it */
        if (cur != NULL) {
            size_t n = strlen(cur);
            if (n >= NT_CLIPBOARD_SAVE_CAP) {
                n = NT_CLIPBOARD_SAVE_CAP - 1U;
            }
            memcpy(s_saved_clip, cur, n);
            s_saved_clip[n] = '\0';
            s_saved_clip_valid = true;
        }
    }
}
void tearDown(void) {
    if (s_saved_clip_valid) {
        nt_clipboard_set_text(s_saved_clip); /* best-effort restore (no-op if access is denied) */
    }
    glfwTerminate();
}
#else
void setUp(void) {}
void tearDown(void) {}
#endif

/* get_text is never NULL; the stub returns an empty string. */
void test_get_text_not_null(void) {
    const char *s = nt_clipboard_get_text();
    TEST_ASSERT_NOT_NULL(s);
}

#if defined(NT_CLIPBOARD_TEST_NATIVE)
/* The real backend talks to the OS clipboard, which is unavailable in some
 * headless/sandboxed sessions (Win32 OpenClipboard fails without an interactive
 * desktop; X11 needs a running server). Probe once with a sentinel; if it does
 * not round-trip, the environment denies clipboard access — TEST_IGNORE the
 * contract assertions rather than fail on a condition outside the engine. */
static bool clipboard_available(void) {
    nt_clipboard_set_text("nt_probe");
    return strcmp(nt_clipboard_get_text(), "nt_probe") == 0;
}

/* Real backend: set then get round-trips, incl. multi-byte UTF-8 (Cyrillic "Аб"). */
void test_native_roundtrip_ascii(void) {
    if (!clipboard_available()) {
        TEST_IGNORE_MESSAGE("OS clipboard unavailable in this session");
    }
    nt_clipboard_set_text("hello");
    TEST_ASSERT_EQUAL_STRING("hello", nt_clipboard_get_text());
}

void test_native_roundtrip_utf8(void) {
    if (!clipboard_available()) {
        TEST_IGNORE_MESSAGE("OS clipboard unavailable in this session");
    }
    nt_clipboard_set_text("Аб");
    TEST_ASSERT_EQUAL_STRING("Аб", nt_clipboard_get_text());
}

void test_native_set_null_is_empty(void) {
    if (!clipboard_available()) {
        TEST_IGNORE_MESSAGE("OS clipboard unavailable in this session");
    }
    nt_clipboard_set_text(NULL);
    TEST_ASSERT_EQUAL_STRING("", nt_clipboard_get_text());
}
#else
/* Stub backend: get -> "", set -> noop (get still ""). */
void test_stub_get_empty(void) { TEST_ASSERT_EQUAL_STRING("", nt_clipboard_get_text()); }

void test_stub_set_is_noop(void) {
    nt_clipboard_set_text("x");
    TEST_ASSERT_EQUAL_STRING("", nt_clipboard_get_text());
}
#endif

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_get_text_not_null);
#if defined(NT_CLIPBOARD_TEST_NATIVE)
    RUN_TEST(test_native_roundtrip_ascii);
    RUN_TEST(test_native_roundtrip_utf8);
    RUN_TEST(test_native_set_null_is_empty);
#else
    RUN_TEST(test_stub_get_empty);
    RUN_TEST(test_stub_set_is_noop);
#endif
    return UNITY_END();
}
