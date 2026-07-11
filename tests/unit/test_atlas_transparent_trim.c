/* Fully transparent sprites currently fail loud during alpha trim. */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Windows SDK must be included early (before stdnoreturn.h from C17 headers) */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/* EXPECT_BUILD_ASSERT + longjmp leaks the builder's in-flight pipeline allocs. */
const char *__lsan_default_suppressions(void);  // NOLINT(bugprone-reserved-identifier)
const char *__lsan_default_suppressions(void) { // NOLINT(bugprone-reserved-identifier)
    return "leak:nt_builder_end_atlas\n"
           "leak:pipeline_alpha_trim\n"
           "leak:alpha_plane_extract\n";
}

/* clang-format off */
#include "nt_builder.h"
#include "unity.h"
/* clang-format on */

#include <setjmp.h>

/* --- Build assert catching (setjmp/longjmp via hookable handler) --- */

static jmp_buf s_build_assert_jmp;
static NtBuilderContext *s_build_assert_ctx; /* freed after longjmp to avoid ASAN leaks */

static void test_build_assert_handler(const char *expr, const char *file, int line) {
    (void)expr;
    (void)file;
    (void)line;
    longjmp(s_build_assert_jmp, 1);
}

/* Expect NT_BUILD_ASSERT to fire inside `code`.
 * `ctx` is the builder context — freed after longjmp to satisfy ASAN. */
#define EXPECT_BUILD_ASSERT(ctx, code)                                                                                                                                                                 \
    do {                                                                                                                                                                                               \
        s_build_assert_ctx = (ctx);                                                                                                                                                                    \
        nt_build_assert_handler = test_build_assert_handler;                                                                                                                                           \
        if (setjmp(s_build_assert_jmp) == 0) {                                                                                                                                                         \
            code;                                                                                                                                                                                      \
            nt_build_assert_handler = NULL;                                                                                                                                                            \
            TEST_FAIL_MESSAGE("Expected NT_BUILD_ASSERT to fire");                                                                                                                                     \
        }                                                                                                                                                                                              \
        nt_build_assert_handler = NULL;                                                                                                                                                                \
        nt_builder_free_pack(s_build_assert_ctx);                                                                                                                                                      \
        s_build_assert_ctx = NULL;                                                                                                                                                                     \
    } while (0)

/* --- Temp directory for test output --- */

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

#define TMP_DIR "build/tests/tmp"

void setUp(void) {}
void tearDown(void) {}

void test_transparent_after_trim_aborts(void) {
    (void)MKDIR(TMP_DIR);
    NtBuilderContext *ctx = nt_builder_start_pack(TMP_DIR "/atlas_transparent_trim.ntpack");
    TEST_ASSERT_NOT_NULL(ctx);

    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    nt_builder_begin_atlas(ctx, "transparent", &opts);

    /* 32x32 RGBA, all bytes 0 → alpha 0 everywhere → empty mask after trim. */
    uint8_t *pixels = (uint8_t *)calloc((size_t)32 * 32 * 4, 1);
    TEST_ASSERT_NOT_NULL(pixels);
    /* raw sprites require an explicit name (no path to derive one from). */
    nt_builder_atlas_add_raw(ctx, pixels, 32, 32, &(nt_atlas_sprite_opts_t){.name = "ghost.png", .origin_x = 0.5F, .origin_y = 0.5F});

    /* Catch the alpha-trim invariant without terminating the test process. */
    EXPECT_BUILD_ASSERT(ctx, nt_builder_end_atlas(ctx));

    free(pixels);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_transparent_after_trim_aborts);
    return UNITY_END();
}
