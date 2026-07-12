/* Fully transparent sprites report a graceful error instead of aborting. */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Windows SDK must be included early (before stdnoreturn.h from C17 headers) */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/* clang-format off */
#include "nt_builder.h"
#include "unity.h"
/* clang-format on */

/* --- Temp directory for test output --- */

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

#define TMP_DIR "build/tests/tmp"
#define PACK_PATH TMP_DIR "/atlas_transparent_trim.ntpack"

void setUp(void) {}
void tearDown(void) {}

/* A fully-transparent sprite must produce a graceful TRANSPARENT_AFTER_TRIM
 * error: end_atlas does not abort, finish_pack writes no pack, and the whole
 * error path is leak-free (this test runs under live LSan). */
void test_transparent_after_trim_reports_error(void) {
    (void)MKDIR(TMP_DIR);
    (void)remove(PACK_PATH); /* stale file from a prior run would defeat the no-write check */

    NtBuilderContext *ctx = nt_builder_start_pack(PACK_PATH);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    nt_builder_begin_atlas(ctx, "transparent", &opts);

    /* 32x32 RGBA, all bytes 0 → alpha 0 everywhere → empty mask after trim. */
    uint8_t *pixels = (uint8_t *)calloc((size_t)32 * 32 * 4, 1);
    TEST_ASSERT_NOT_NULL(pixels);
    /* raw sprites require an explicit name (no path to derive one from). */
    nt_builder_atlas_add_raw(ctx, pixels, 32, 32, &(nt_atlas_sprite_opts_t){.name = "ghost.png", .origin_x = 0.5F, .origin_y = 0.5F});

    /* No abort — the pipeline accumulates the error and cleans up. */
    nt_builder_end_atlas(ctx);

    uint32_t n = 0;
    const nt_build_error_t *errs = nt_builder_get_errors(ctx, &n);
    TEST_ASSERT_EQUAL_UINT32(1, n);
    TEST_ASSERT_EQUAL_INT(NT_BUILD_ERR_KIND_TRANSPARENT_AFTER_TRIM, errs[0].kind);
    TEST_ASSERT_EQUAL_STRING("ghost.png", errs[0].sprite);

    /* Poisoned build: non-OK code, and NO .ntpack written. */
    nt_build_result_t result = nt_builder_finish_pack(ctx);
    TEST_ASSERT_NOT_EQUAL(NT_BUILD_OK, result);

    FILE *f = fopen(PACK_PATH, "rb");
    TEST_ASSERT_NULL_MESSAGE(f, "no .ntpack must exist after a failed build");
    if (f) {
        (void)fclose(f);
    }

    nt_builder_free_pack(ctx);
    free(pixels);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_transparent_after_trim_reports_error);
    return UNITY_END();
}
