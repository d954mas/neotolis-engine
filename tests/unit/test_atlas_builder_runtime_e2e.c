/* Builder -> pack -> runtime integration: every other suite reads packs with a
 * test-side parser or feeds the runtime hand-built blobs, so a builder/runtime
 * format skew would stay invisible until a game loads a real pack. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

/* clang-format off */
#include "atlas/nt_atlas.h"
#include "nt_atlas_format.h"
#include "nt_builder.h"
#include "hash/nt_hash.h"
#include "test_helpers/atlas_dedup_f_fixture.h"
#include "test_helpers/atlas_dedup_fixture.h"
#include "unity.h"
/* clang-format on */

#define TMP_DIR "build/tests/tmp"

static void *s_user_data;

void setUp(void) { s_user_data = NULL; }
void tearDown(void) {
    if (s_user_data != NULL) {
        nt_atlas_test_drive_cleanup(s_user_data);
        s_user_data = NULL;
    }
}

static uint8_t *read_bin_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    (void)fseek(f, 0, SEEK_END);
    const long len = ftell(f);
    (void)fseek(f, 0, SEEK_SET);
    if (len <= 0) {
        (void)fclose(f);
        return NULL;
    }
    uint8_t *buf = (uint8_t *)malloc((size_t)len);
    TEST_ASSERT_NOT_NULL_MESSAGE(buf, "pack read buffer alloc failed");
    const size_t got = fread(buf, 1, (size_t)len, f);
    (void)fclose(f);
    if (got != (size_t)len) {
        free(buf);
        return NULL;
    }
    *out_len = (size_t)len;
    return buf;
}

/* UV bbox straight off the runtime's raw vertex array — the fold observable. */
static void runtime_region_uv_bbox(const struct nt_atlas_data *ad, const nt_texture_region_t *r, uint16_t out_bbox[4]) {
    const nt_atlas_vertex_t *verts = nt_atlas_test_raw_vertices(ad);
    out_bbox[0] = UINT16_MAX;
    out_bbox[1] = UINT16_MAX;
    out_bbox[2] = 0;
    out_bbox[3] = 0;
    for (uint32_t v = 0; v < r->vertex_count; ++v) {
        const nt_atlas_vertex_t *p = &verts[r->vertex_start + v];
        out_bbox[0] = (p->atlas_u < out_bbox[0]) ? p->atlas_u : out_bbox[0];
        out_bbox[1] = (p->atlas_v < out_bbox[1]) ? p->atlas_v : out_bbox[1];
        out_bbox[2] = (p->atlas_u > out_bbox[2]) ? p->atlas_u : out_bbox[2];
        out_bbox[3] = (p->atlas_v > out_bbox[3]) ? p->atlas_v : out_bbox[3];
    }
}

static const nt_texture_region_t *find_runtime_region(const struct nt_atlas_data *ad, const char *name) {
    const uint32_t idx = nt_atlas_test_find_region_raw(ad, nt_hash64_str(name).value);
    TEST_ASSERT_NOT_EQUAL_UINT32_MESSAGE(NT_ATLAS_INVALID_REGION, idx, name);
    return nt_atlas_test_get_region_raw(ad, idx);
}

static void build_e2e_pack(const char *path) {
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);
    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL_MESSAGE(ctx, "start_pack failed");
    nt_builder_set_threads(ctx, 1);
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.allowed_transforms = NT_ATLAS_TRANSFORMS_ROTATIONS;
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, "e2e_rt", &opts);
    const uint8_t rotations[4] = {NT_ATLAS_XFORM_IDENTITY, NT_ATLAS_XFORM_ROT90, NT_ATLAS_XFORM_ROT180, NT_ATLAS_XFORM_ROT270};
    atlas_dedup_f_fixture_add(atlas, rotations, 4, NT_ATLAS_TRANSFORMS_ROTATIONS, 0U);
    {
        /* A never-folding filler so one region provably keeps its own placement. */
        uint8_t px[16 * 10 * 4];
        for (uint32_t t = 0; t < 16U * 10U; ++t) {
            px[(t * 4U) + 0U] = 10U;
            px[(t * 4U) + 1U] = 200U;
            px[(t * 4U) + 2U] = 10U;
            px[(t * 4U) + 3U] = 255U;
        }
        nt_atlas_sprite_opts_t so = nt_atlas_sprite_opts_defaults();
        so.name = "e2e_filler";
        nt_atlas_add_raw(atlas, px, 16, 10, &so);
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(NT_BUILD_OK, nt_atlas_commit(atlas), "e2e commit failed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(NT_BUILD_OK, nt_builder_finish_pack(ctx), "e2e finish_pack failed");
    nt_builder_free_pack(ctx);
}

void test_builder_pack_resolves_in_runtime_atlas(void) {
    const char *path = TMP_DIR "/e2e_builder_runtime.ntpack";
    build_e2e_pack(path);

    size_t pack_len = 0;
    uint8_t *pack = read_bin_file(path, &pack_len);
    TEST_ASSERT_NOT_NULL_MESSAGE(pack, "read produced pack");
    uint32_t blob_size = 0;
    const uint8_t *blob = atlas_dedup_find_asset(pack, pack_len, (uint8_t)NT_ASSET_ATLAS, 0, &blob_size);
    TEST_ASSERT_NOT_NULL_MESSAGE(blob, "pack must contain the atlas asset");

    /* The REAL builder blob must pass the REAL runtime validator on both entry
     * points — this is the assertion no synthetic-blob suite can make. */
    TEST_ASSERT_NOT_EQUAL_UINT32_MESSAGE(0, nt_atlas_test_activate(blob, blob_size), "builder blob must activate in the runtime");
    nt_atlas_test_drive_resolve(blob, blob_size, &s_user_data);
    TEST_ASSERT_NOT_NULL_MESSAGE(s_user_data, "builder blob must resolve in the runtime");
    const struct nt_atlas_data *ad = (const struct nt_atlas_data *)s_user_data;

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(5, nt_atlas_test_region_count(ad), "one runtime region per sprite");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, nt_atlas_test_page_count(ad), "the fixture fits one page");
    TEST_ASSERT_NOT_EQUAL_UINT64_MESSAGE(0, nt_atlas_test_page_resource_id(ad, 0), "page resource id must be published");

    /* The D4 fold must be visible through runtime data: all four F regions map
     * onto one placement rectangle, the filler onto another. */
    const uint8_t rotations[4] = {NT_ATLAS_XFORM_IDENTITY, NT_ATLAS_XFORM_ROT90, NT_ATLAS_XFORM_ROT180, NT_ATLAS_XFORM_ROT270};
    uint16_t bbox0[4];
    runtime_region_uv_bbox(ad, find_runtime_region(ad, "f_t0"), bbox0);
    for (uint32_t i = 1; i < 4; ++i) {
        char name[16];
        (void)snprintf(name, sizeof(name), "f_t%u", (unsigned)rotations[i]);
        uint16_t bbox[4];
        runtime_region_uv_bbox(ad, find_runtime_region(ad, name), bbox);
        TEST_ASSERT_EQUAL_UINT16_ARRAY_MESSAGE(bbox0, bbox, 4, "a folded twin must share the root's placement rectangle");
    }
    uint16_t filler_bbox[4];
    const nt_texture_region_t *filler = find_runtime_region(ad, "e2e_filler");
    runtime_region_uv_bbox(ad, filler, filler_bbox);
    TEST_ASSERT_TRUE_MESSAGE(memcmp(bbox0, filler_bbox, sizeof(filler_bbox)) != 0, "the filler must keep a placement of its own");

    /* Region metadata round-trips the format: dims, full-glyph trim, transforms
     * inside the build mask, filler at identity. */
    const nt_texture_region_t *root = find_runtime_region(ad, "f_t0");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(NT_ATLAS_F_W, root->source_w, "root source_w");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(NT_ATLAS_F_H, root->source_h, "root source_h");
    TEST_ASSERT_EQUAL_INT16_MESSAGE(0, root->trim_offset_x, "the F glyph reaches every canvas edge");
    TEST_ASSERT_EQUAL_INT16_MESSAGE(0, root->trim_offset_y, "the F glyph reaches every canvas edge");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(4, root->vertex_count, "RECT sprite quad");
    TEST_ASSERT_TRUE_MESSAGE((NT_ATLAS_TRANSFORMS_ROTATIONS & (uint8_t)(1U << root->transform)) != 0U, "root transform escaped the build mask");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(NT_ATLAS_XFORM_IDENTITY, filler->transform, "an unfolded lone sprite ships at identity");

    /* cached_pos bake integration: with ipu pinned to 1, the baked position is
     * local + trim offset — zero here, so it must equal the raw vertex. */
    nt_atlas_test_set_ipu_and_recompute((struct nt_atlas_data *)s_user_data, 1.0F);
    const nt_atlas_vertex_t *raw = &nt_atlas_test_raw_vertices(ad)[root->vertex_start];
    const float(*cached)[2] = nt_atlas_test_cached_pos(ad);
    /* Values are small exact integers; UNITY_EXCLUDE_FLOAT bars float asserts. */
    TEST_ASSERT_EQUAL_INT32_MESSAGE((int32_t)raw->local_x, (int32_t)cached[root->vertex_start][0], "cached_pos X must bake local + zero trim at ipu 1");
    TEST_ASSERT_EQUAL_INT32_MESSAGE((int32_t)raw->local_y, (int32_t)cached[root->vertex_start][1], "cached_pos Y must bake local + zero trim at ipu 1");

    free(pack);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_builder_pack_resolves_in_runtime_atlas);
    return UNITY_END();
}
