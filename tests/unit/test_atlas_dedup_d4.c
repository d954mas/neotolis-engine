/* D4 probe fixture contract: the 'F' images must be pairwise distinct and must
 * carry the dimension swap, or every later relative-transform assertion could
 * pass by symmetry instead of by correctness. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <direct.h>
#include <windows.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

/* clang-format off */
#include "nt_atlas_format.h"
#include "nt_builder.h"
#include "nt_builder_atlas_geometry.h"
#include "nt_pack_format.h"
#include "test_helpers/atlas_dedup_f_fixture.h"
#include "test_helpers/atlas_dedup_fixture.h"
#include "unity.h"
/* clang-format on */

#define TMP_DIR "build/tests/tmp"

void setUp(void) {}
void tearDown(void) {}

static uint8_t *read_bin_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    (void)fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    (void)fseek(f, 0, SEEK_SET);
    if (sz < 0) {
        (void)fclose(f);
        return NULL;
    }
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) {
        (void)fclose(f);
        return NULL;
    }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    (void)fclose(f);
    *out_len = rd;
    return buf;
}

void test_f_fixture_images_pairwise_distinct(void) { TEST_ASSERT_TRUE_MESSAGE(atlas_dedup_f_images_pairwise_distinct(), "the 8 D4 images of the F glyph must be pairwise distinct"); }

void test_f_fixture_dims_swap_on_diagonal(void) {
    uint8_t px[NT_ATLAS_F_MAX_PX];
    for (uint8_t t = 0; t < 8; ++t) {
        uint16_t w = 0;
        uint16_t h = 0;
        atlas_dedup_f_fill(t, px, &w, &h);
        uint32_t expect_w = 0;
        uint32_t expect_h = 0;
        d4_dims_after(t, (uint32_t)NT_ATLAS_F_W, (uint32_t)NT_ATLAS_F_H, &expect_w, &expect_h);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE((uint16_t)expect_w, w, "image width must follow d4_dims_after");
        TEST_ASSERT_EQUAL_UINT16_MESSAGE((uint16_t)expect_h, h, "image height must follow d4_dims_after");
        /* Non-vacuity: the base box is 9x13, so a diagonal transform must swap. */
        const bool diagonal = (t & 4U) != 0U;
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(diagonal ? NT_ATLAS_F_H : NT_ATLAS_F_W, w, "diagonal transforms must swap the box dims");
    }
}

void test_f_fixture_packs(void) {
    const char *path = TMP_DIR "/dedup_f_rotations.ntpack";
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);
    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL_MESSAGE(ctx, "start_pack failed");
    nt_builder_set_threads(ctx, 1);
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.allowed_transforms = NT_ATLAS_TRANSFORMS_ROTATIONS;
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, "dedup_f", &opts);
    const uint8_t rotations[4] = {NT_ATLAS_XFORM_IDENTITY, NT_ATLAS_XFORM_ROT90, NT_ATLAS_XFORM_ROT180, NT_ATLAS_XFORM_ROT270};
    atlas_dedup_f_fixture_add(atlas, rotations, 4, NT_ATLAS_TRANSFORMS_ROTATIONS, 0U);
    TEST_ASSERT_EQUAL_INT_MESSAGE(NT_BUILD_OK, nt_atlas_commit(atlas), "F fixture commit failed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(NT_BUILD_OK, nt_builder_finish_pack(ctx), "F fixture finish_pack failed");
    nt_builder_free_pack(ctx);

    size_t len = 0;
    uint8_t *bytes = read_bin_file(path, &len);
    TEST_ASSERT_NOT_NULL_MESSAGE(bytes, "read produced pack");
    nt_atlas_dedup_region_t regions[8];
    uint32_t count = 0;
    bool ok = atlas_dedup_collect_regions(bytes, len, regions, 8, &count);
    free(bytes);
    TEST_ASSERT_TRUE_MESSAGE(ok, "collect regions from F pack");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(4, count, "one region per rotation image");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_f_fixture_images_pairwise_distinct);
    RUN_TEST(test_f_fixture_dims_swap_on_diagonal);
    RUN_TEST(test_f_fixture_packs);
    return UNITY_END();
}
