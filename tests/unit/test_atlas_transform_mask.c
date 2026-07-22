/* Transform-mask OUTPUT contract: every check reads the UNPACKED atlas —
 * region.transform or a pack SHA-256 — never the packer's internal filter.
 * Etalon capture recipe: tests/fixtures/transform_mask_golden/PROVENANCE.txt. */

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Windows SDK before C17 stdnoreturn.h (matches test_atlas_pack_determinism). */
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
#include "nt_pack_format.h"
#include "ntpack_parse.h"
#include "test_helpers/atlas_transform_fixture.h"
#include "unity.h"
/* clang-format on */

#define TMP_DIR "build/tests/tmp"
#define GOLDEN_DIR "tests/fixtures/transform_mask_golden"

/* Atlas name the etalons were captured with. The name seeds the texture
 * page resource ids, so byte-identity to the etalons requires the same name. */
#define GOLDEN_ATLAS_NAME "transform_golden"

void setUp(void) {}
void tearDown(void) {}

/* --- IO helpers --- */

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

static char *read_text_file(const char *path) {
    size_t len = 0;
    uint8_t *bytes = read_bin_file(path, &len);
    if (!bytes) {
        return NULL;
    }
    /* Dumps/etalons are tiny; the cap bounds the file-tainted alloc size. */
    if (len > (1U << 20)) {
        free(bytes);
        return NULL;
    }
    char *txt = (char *)malloc(len + 1);
    if (!txt) {
        free(bytes);
        return NULL;
    }
    memcpy(txt, bytes, len);
    txt[len] = '\0';
    free(bytes);
    return txt;
}

static void read_sha_file(const char *path, char out[65]) {
    FILE *f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    size_t n = fread(out, 1, 64, f);
    (void)fclose(f);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(64, (unsigned)n, "etalon sha file must hold 64 hex digits");
    out[64] = '\0';
}

/* --- Pack builders --- */

/* Build the shared etalon fixture to `path` at the given atlas mask, thread=1. */
static bool build_fixture_pack(uint8_t atlas_mask, const char *atlas_name, const char *path) {
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);
    NtBuilderContext *ctx = nt_builder_start_pack(path);
    if (!ctx) {
        return false;
    }
    nt_builder_set_threads(ctx, 1);
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.allowed_transforms = atlas_mask;
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, atlas_name, &opts);
    atlas_transform_fixture_add(atlas);
    (void)nt_atlas_commit(atlas);
    nt_build_result_t r = nt_builder_finish_pack(ctx);
    nt_builder_free_pack(ctx);
    return r == NT_BUILD_OK;
}

/* Fill an opaque RGBA strip (asymmetric strips rotate under an unrestricted mask).
 * Distinct colors per sprite keep pixel hashes distinct — no dedup interference. */
static void fill_solid(uint8_t *px, uint16_t w, uint16_t h, uint8_t r, uint8_t g, uint8_t b) {
    for (size_t i = 0; i < (size_t)w * h; ++i) {
        px[(i * 4) + 0] = r;
        px[(i * 4) + 1] = g;
        px[(i * 4) + 2] = b;
        px[(i * 4) + 3] = 255;
    }
}

/* Three tall strips + ONE wide 44x8 strip the ALL packer transposes. mask_wide
 * restricts the wide strip to IDENTITY|FLIP_H; the unmasked A/B control proves
 * the packer WOULD transpose it — otherwise the masked assert is vacuous. */
static bool build_intersection_pack(const char *path, bool mask_wide) {
    (void)MKDIR(TMP_DIR);
    NtBuilderContext *ctx = nt_builder_start_pack(path);
    if (!ctx) {
        return false;
    }
    nt_builder_set_threads(ctx, 1);
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.allowed_transforms = NT_ATLAS_TRANSFORMS_ALL;
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, mask_wide ? "intersect_masked" : "intersect_control", &opts);

    uint8_t px[48 * 48 * 4];
    const uint16_t dims[4][2] = {{8, 44}, {8, 44}, {44, 8}, {8, 40}};
    const char *names[4] = {"s0", "s1", "s2_wide", "s3"};
    for (int i = 0; i < 4; ++i) {
        fill_solid(px, dims[i][0], dims[i][1], (uint8_t)(180 + (i * 10)), 90, 60);
        nt_atlas_sprite_opts_t so = nt_atlas_sprite_opts_defaults();
        so.name = names[i];
        if (mask_wide && i == 2) {
            so.allowed_transforms = (uint8_t)(NT_ATLAS_TRANSFORM_IDENTITY | NT_ATLAS_TRANSFORM_FLIP_H);
        }
        nt_atlas_add_raw(atlas, px, dims[i][0], dims[i][1], &so);
    }
    (void)nt_atlas_commit(atlas);
    nt_build_result_t r = nt_builder_finish_pack(ctx);
    nt_builder_free_pack(ctx);
    return r == NT_BUILD_OK;
}

/* Atlas mask ALL, single slice9 sprite — slice9 forces identity regardless. */
static bool build_slice9_pack(const char *path) {
    (void)MKDIR(TMP_DIR);
    NtBuilderContext *ctx = nt_builder_start_pack(path);
    if (!ctx) {
        return false;
    }
    nt_builder_set_threads(ctx, 1);
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.allowed_transforms = NT_ATLAS_TRANSFORMS_ALL;
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, "slice9", &opts);

    uint8_t px[48 * 48 * 4];
    fill_solid(px, 24, 40, 180, 90, 60);
    nt_atlas_sprite_opts_t so = nt_atlas_sprite_opts_defaults();
    so.name = "panel";
    so.slice9_left = 4;
    so.slice9_right = 4;
    so.slice9_top = 4;
    so.slice9_bottom = 4;
    nt_atlas_add_raw(atlas, px, 24, 40, &so);
    (void)nt_atlas_commit(atlas);
    nt_build_result_t r = nt_builder_finish_pack(ctx);
    nt_builder_free_pack(ctx);
    return r == NT_BUILD_OK;
}

/* --- Region dump / transform extraction (unpacked output) --- */

/* Write the structural region dump for a produced pack into a malloc'd,
 * NUL-terminated string (LF line endings). */
static char *dump_regions_text(const char *pack_path) {
    size_t len = 0;
    uint8_t *bytes = read_bin_file(pack_path, &len);
    if (!bytes) {
        return NULL;
    }
    char tmp[512];
    (void)snprintf(tmp, sizeof(tmp), "%s/xform_regions_dump.txt", TMP_DIR);
    FILE *f = fopen(tmp, "wb"); /* wb → LF, matches the LF-pinned etalon dumps */
    if (!f) {
        free(bytes);
        return NULL;
    }
    bool ok = atlas_transform_fixture_dump_regions(bytes, len, f);
    (void)fclose(f);
    free(bytes);
    if (!ok) {
        return NULL;
    }
    return read_text_file(tmp);
}

/* Parse the 6 space-separated unsigned fields of one dump line ("idx tr x y w h").
 * strtoul-based (cert-err34-c); parsing never crosses line_end. */
static bool parse_dump_line_u6(const char *line, const char *line_end, unsigned out[6]) {
    const char *cur = line;
    for (int k = 0; k < 6; ++k) {
        char *end = NULL;
        errno = 0;
        unsigned long v = strtoul(cur, &end, 10);
        if (end == cur || end > line_end || errno == ERANGE || v > 0xFFFFFFFFUL) {
            return false;
        }
        out[k] = (unsigned)v;
        cur = end;
    }
    return true;
}

/* Extract region.transform values (in region-index order) from a produced pack. */
static bool collect_transforms(const char *pack_path, uint8_t *out, int cap, int *count) {
    char *txt = dump_regions_text(pack_path);
    if (!txt) {
        return false;
    }
    int n = 0;
    const char *p = txt;
    while (*p) {
        const char *nl = strchr(p, '\n');
        const char *line_end = nl ? nl : p + strlen(p);
        unsigned fields[6];
        if (n < cap && parse_dump_line_u6(p, line_end, fields)) {
            out[n++] = (uint8_t)fields[1];
        }
        if (!nl) {
            break;
        }
        p = nl + 1;
    }
    free(txt);
    *count = n;
    return n > 0;
}

/* Every emitted region.transform has its bit set in the effective mask. */
static void assert_all_in_mask(const char *pack_path, uint8_t mask, const char *what) {
    uint8_t t[64];
    int n = 0;
    TEST_ASSERT_TRUE_MESSAGE(collect_transforms(pack_path, t, 64, &n), "collect transforms failed");
    TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, n, "no regions unpacked");
    for (int i = 0; i < n; ++i) {
        /* Range first: a corrupt transform >= 32 would make the shift UB. */
        TEST_ASSERT_TRUE_MESSAGE(t[i] < 8U, "transform value out of D4 range");
        TEST_ASSERT_TRUE_MESSAGE((mask & (1U << t[i])) != 0U, what);
    }
}

/* Round density to 1e-6 fixed point for exact-integer comparison (no ULP noise). */
static int64_t density_fixed(double d) { return llround(d * 1000000.0); }

/* Collect transforms and require the exact region count. */
static void collect_expect_n(const char *pack_path, int expected, uint8_t t[64]) {
    int n = 0;
    TEST_ASSERT_TRUE_MESSAGE(collect_transforms(pack_path, t, 64, &n), "collect transforms failed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(expected, n, "unexpected region count");
}

/* --- Defaults --- */

void test_defaults_allowed_transforms(void) {
    nt_atlas_opts_t o = nt_atlas_opts_defaults();
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(NT_ATLAS_TRANSFORMS_ALL, o.allowed_transforms, "atlas default mask must be ALL");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xFFU, o.allowed_transforms, "atlas default mask must be 0xFF");
    nt_atlas_sprite_opts_t s = nt_atlas_sprite_opts_defaults();
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0U, s.allowed_transforms, "sprite default mask must be 0 (inherit)");
}

/* --- Forbidden orientations never reach the unpacked output --- */

void test_export_mask_emits_only_identity_and_rot90(void) {
    const char *path = TMP_DIR "/xform_export.ntpack";
    TEST_ASSERT_TRUE_MESSAGE(build_fixture_pack(NT_ATLAS_TRANSFORMS_EXPORT, "export", path), "EXPORT pack failed");
    /* EXPORT = IDENTITY|ROT90 → the only bits set are values 0 and 5. */
    assert_all_in_mask(path, NT_ATLAS_TRANSFORMS_EXPORT, "region transform outside {identity, rot90}");
    /* Positive pin: the fixture's dimension-swapping strips make rot90 a win, so a
     * regression that collapses partial masks to identity-only must fail here. */
    uint8_t t[64];
    int n = 0;
    TEST_ASSERT_TRUE_MESSAGE(collect_transforms(path, t, 64, &n), "collect transforms failed");
    bool saw_rot90 = false;
    for (int i = 0; i < n; ++i) {
        saw_rot90 = saw_rot90 || (t[i] == NT_ATLAS_XFORM_ROT90);
    }
    TEST_ASSERT_TRUE_MESSAGE(saw_rot90, "EXPORT mask must actually emit rot90 on this fixture");
}

/* --- A partial rotation mask never packs worse than identity-only --- */

void test_export_density_at_least_identity(void) {
    const char *export_path = TMP_DIR "/xform_export_density.ntpack";
    const char *identity_path = TMP_DIR "/xform_identity_density.ntpack";
    TEST_ASSERT_TRUE_MESSAGE(build_fixture_pack(NT_ATLAS_TRANSFORMS_EXPORT, "density", export_path), "EXPORT density pack failed");
    TEST_ASSERT_TRUE_MESSAGE(build_fixture_pack(NT_ATLAS_TRANSFORMS_IDENTITY, "density", identity_path), "IDENTITY density pack failed");

    nt_bench_atlas_metrics_t me;
    nt_bench_atlas_metrics_t mi;
    memset(&me, 0, sizeof(me));
    memset(&mi, 0, sizeof(mi));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, nt_bench_parse_ntpack(export_path, &me), "parse EXPORT pack");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, nt_bench_parse_ntpack(identity_path, &mi), "parse IDENTITY pack");
    /* Empirical pin, NOT a packer theorem — greedy packing with more orientations
     * can pack worse; after an intentional packer change, re-baseline. Pins
     * fill_texture: frontier density flips with page arrangement (128x64 vs 64x128). */
    TEST_ASSERT_GREATER_OR_EQUAL_INT64_MESSAGE(density_fixed(mi.density_fill_texture), density_fixed(me.density_fill_texture), "EXPORT texture density must be >= IDENTITY texture density");
}

/* --- Per-sprite mask intersects the atlas mask --- */

void test_per_sprite_mask_intersection(void) {
    const char *control_path = TMP_DIR "/xform_intersect_control.ntpack";
    const char *masked_path = TMP_DIR "/xform_intersect_masked.ntpack";
    TEST_ASSERT_TRUE_MESSAGE(build_intersection_pack(control_path, false), "control pack failed");
    TEST_ASSERT_TRUE_MESSAGE(build_intersection_pack(masked_path, true), "masked pack failed");
    uint8_t t[64];
    /* A/B control: unrestricted, the packer transposes the wide strip. */
    collect_expect_n(control_path, 4, t);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(NT_ATLAS_XFORM_TRANSPOSE, t[2], "control: packer must transpose the wide strip — masked assert would be vacuous");
    /* Same fixture, wide strip masked to IDENTITY|FLIP_H → transform is 0 or 1. */
    collect_expect_n(masked_path, 4, t);
    TEST_ASSERT_TRUE_MESSAGE(t[2] == NT_ATLAS_XFORM_IDENTITY || t[2] == NT_ATLAS_XFORM_FLIP_H, "masked sprite emitted a forbidden transform");
    /* Inheriting sprites still resolve to a valid D4 value (identity always representable). */
    for (int i = 0; i < 4; ++i) {
        TEST_ASSERT_TRUE_MESSAGE(t[i] <= NT_ATLAS_XFORM_ANTITRANSPOSE, "transform value out of D4 range");
    }
}

/* --- A zero atlas mask (zero-init struct) behaves as identity-only --- */

void test_zero_mask_behaves_as_identity(void) {
    const char *path = TMP_DIR "/xform_zeromask.ntpack";
    TEST_ASSERT_TRUE_MESSAGE(build_fixture_pack(0x00U, "zeromask", path), "zero-mask pack failed");
    assert_all_in_mask(path, NT_ATLAS_TRANSFORMS_IDENTITY, "zero mask must emit identity only");
}

/* Two 44x8 strips the ALL packer transposes + one 8x40 tall strip, with per-sprite
 * masks (0 = inherit) — probes the identity-floor and no-widening corners. */
static bool build_masked_strips_pack(const char *path, const char *name, uint8_t atlas_mask, const uint8_t sprite_masks[3]) {
    (void)MKDIR(TMP_DIR);
    NtBuilderContext *ctx = nt_builder_start_pack(path);
    if (!ctx) {
        return false;
    }
    nt_builder_set_threads(ctx, 1);
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.allowed_transforms = atlas_mask;
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, name, &opts);

    uint8_t px[48 * 48 * 4];
    const uint16_t dims[3][2] = {{44, 8}, {44, 8}, {8, 40}};
    const char *names[3] = {"m0", "m1", "m2"};
    for (int i = 0; i < 3; ++i) {
        fill_solid(px, dims[i][0], dims[i][1], (uint8_t)(60 + (i * 20)), 140, 200);
        nt_atlas_sprite_opts_t so = nt_atlas_sprite_opts_defaults();
        so.name = names[i];
        so.allowed_transforms = sprite_masks[i];
        nt_atlas_add_raw(atlas, px, dims[i][0], dims[i][1], &so);
    }
    (void)nt_atlas_commit(atlas);
    nt_build_result_t r = nt_builder_finish_pack(ctx);
    nt_builder_free_pack(ctx);
    return r == NT_BUILD_OK;
}

/* --- Disjoint atlas∩sprite floors to identity (the floor is load-bearing:
 * without it orient_count would be 0 and the packer would assert-crash) --- */

void test_disjoint_sprite_mask_floors_to_identity(void) {
    const char *path = TMP_DIR "/xform_disjoint.ntpack";
    const uint8_t masks[3] = {NT_ATLAS_TRANSFORM_TRANSPOSE, 0U, 0U}; /* 0x21 & 0x10 == 0 */
    TEST_ASSERT_TRUE_MESSAGE(build_masked_strips_pack(path, "disjoint", NT_ATLAS_TRANSFORMS_EXPORT, masks), "disjoint pack failed");
    uint8_t t[64];
    collect_expect_n(path, 3, t);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(NT_ATLAS_XFORM_IDENTITY, t[0], "disjoint sprite mask must floor to identity");
}

/* --- A sprite mask can only restrict — it cannot widen the atlas mask --- */

void test_sprite_mask_cannot_widen_atlas_mask(void) {
    const char *path = TMP_DIR "/xform_widen.ntpack";
    const uint8_t masks[3] = {NT_ATLAS_TRANSFORMS_ALL, NT_ATLAS_TRANSFORMS_ALL, NT_ATLAS_TRANSFORMS_ALL};
    TEST_ASSERT_TRUE_MESSAGE(build_masked_strips_pack(path, "widen", NT_ATLAS_TRANSFORMS_IDENTITY, masks), "widen pack failed");
    assert_all_in_mask(path, NT_ATLAS_TRANSFORMS_IDENTITY, "sprite mask must not widen the atlas mask");
}

/* --- Slice9 emits only identity regardless of the atlas mask --- */

void test_slice9_emits_identity(void) {
    const char *path = TMP_DIR "/xform_slice9.ntpack";
    TEST_ASSERT_TRUE_MESSAGE(build_slice9_pack(path), "slice9 pack failed");
    uint8_t t[64];
    int n = 0;
    TEST_ASSERT_TRUE_MESSAGE(collect_transforms(path, t, 64, &n), "collect transforms failed");
    TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, n, "no slice9 region unpacked");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(NT_ATLAS_XFORM_IDENTITY, t[0], "slice9 sprite must emit identity");
}

/* --- Byte-identity to the master-captured etalons --- */

static void assert_golden(uint8_t mask, const char *sha_path, const char *dump_path, const char *pack_path) {
    TEST_ASSERT_TRUE_MESSAGE(build_fixture_pack(mask, GOLDEN_ATLAS_NAME, pack_path), "golden pack build failed");

    char actual_sha[65];
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, nt_bench_file_sha256_hex(pack_path, actual_sha), "hash produced pack");
    char expected_sha[65];
    read_sha_file(sha_path, expected_sha);

    char *dump = dump_regions_text(pack_path);
    TEST_ASSERT_NOT_NULL_MESSAGE(dump, "produced pack dump");
    char *etalon_dump = read_text_file(dump_path);
    TEST_ASSERT_NOT_NULL_MESSAGE(etalon_dump, "read etalon dump");

    /* On any divergence, emit the current dump so the diff vs the committed
     * etalon shows WHAT moved. */
    if (strcmp(etalon_dump, dump) != 0 || strcmp(expected_sha, actual_sha) != 0) {
        TEST_MESSAGE("current structural region dump (compare against the committed etalon dump):");
        TEST_MESSAGE(dump);
    }
    TEST_ASSERT_EQUAL_STRING_MESSAGE(etalon_dump, dump, "structural region dump diverged from etalon");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(expected_sha, actual_sha, "pack SHA-256 diverged from etalon (byte-identity broken)");
    free(dump);
    free(etalon_dump);
}

void test_golden_byte_identity_all(void) { assert_golden(NT_ATLAS_TRANSFORMS_ALL, GOLDEN_DIR "/etalon_all.sha256", GOLDEN_DIR "/etalon_all.dump.txt", TMP_DIR "/xform_golden_all.ntpack"); }

void test_golden_byte_identity_identity(void) {
    assert_golden(NT_ATLAS_TRANSFORMS_IDENTITY, GOLDEN_DIR "/etalon_identity.sha256", GOLDEN_DIR "/etalon_identity.dump.txt", TMP_DIR "/xform_golden_identity.ntpack");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_allowed_transforms);
    RUN_TEST(test_export_mask_emits_only_identity_and_rot90);
    RUN_TEST(test_export_density_at_least_identity);
    RUN_TEST(test_per_sprite_mask_intersection);
    RUN_TEST(test_zero_mask_behaves_as_identity);
    RUN_TEST(test_disjoint_sprite_mask_floors_to_identity);
    RUN_TEST(test_sprite_mask_cannot_widen_atlas_mask);
    RUN_TEST(test_slice9_emits_identity);
    RUN_TEST(test_golden_byte_identity_all);
    RUN_TEST(test_golden_byte_identity_identity);
    return UNITY_END();
}
