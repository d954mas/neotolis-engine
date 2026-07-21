/* Transform-mask OUTPUT contract (XFORM-01/02/03/04). Every correctness check is
 * on the UNPACKED produced atlas — region.transform read back from the pack, or a
 * SHA-256 over the serialized pack — never the packer's internal orientation
 * filter (D-11). Byte-identity is proven against the Plan 81-01 master etalons. */

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

/* Atlas name used by the Plan 81-01 capture harness. The name seeds the texture
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
    char *txt = (char *)realloc(bytes, len + 1);
    if (!txt) {
        free(bytes);
        return NULL;
    }
    txt[len] = '\0';
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

/* Build the shared 81-01 fixture to `path` at the given atlas mask, thread=1. */
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

/* Fill an opaque RGBA strip (asymmetric strips rotate under an unrestricted mask). */
static void fill_solid(uint8_t *px, uint16_t w, uint16_t h) {
    for (size_t i = 0; i < (size_t)w * h; ++i) {
        px[(i * 4) + 0] = 180;
        px[(i * 4) + 1] = 90;
        px[(i * 4) + 2] = 60;
        px[(i * 4) + 3] = 255;
    }
}

/* Atlas mask ALL, but sprite index 1 masked to IDENTITY|FLIP_H, sprite 3 inherits. */
static bool build_intersection_pack(const char *path) {
    (void)MKDIR(TMP_DIR);
    NtBuilderContext *ctx = nt_builder_start_pack(path);
    if (!ctx) {
        return false;
    }
    nt_builder_set_threads(ctx, 1);
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.allowed_transforms = NT_ATLAS_TRANSFORMS_ALL;
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, "intersect", &opts);

    uint8_t px[48 * 48 * 4];
    const uint16_t dims[4][2] = {{8, 44}, {8, 44}, {44, 8}, {8, 40}};
    const char *names[4] = {"s0", "s1_target", "s2", "s3_inherit"};
    for (int i = 0; i < 4; ++i) {
        fill_solid(px, dims[i][0], dims[i][1]);
        nt_atlas_sprite_opts_t so = nt_atlas_sprite_opts_defaults();
        so.name = names[i];
        if (i == 1) {
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
    fill_solid(px, 24, 40);
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

/* --- Region dump / transform extraction (unpacked output, D-11) --- */

/* Reuse the 81-01 fixture dump utility: write the structural region dump for a
 * produced pack into a malloc'd, NUL-terminated string (LF line endings). */
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

/* Extract region.transform values (in region-index order) from a produced pack. */
static bool collect_transforms(const char *pack_path, uint8_t *out, int cap, int *count) {
    char *txt = dump_regions_text(pack_path);
    if (!txt) {
        return false;
    }
    int n = 0;
    const char *p = txt;
    while (*p) {
        unsigned idx = 0;
        unsigned tr = 0;
        unsigned x = 0;
        unsigned y = 0;
        unsigned w = 0;
        unsigned h = 0;
        if (sscanf(p, "%u %u %u %u %u %u", &idx, &tr, &x, &y, &w, &h) == 6 && n < cap) {
            out[n++] = (uint8_t)tr;
        }
        const char *nl = strchr(p, '\n');
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
        TEST_ASSERT_TRUE_MESSAGE((mask >> t[i]) & 1U, what);
    }
}

/* Round density to 1e-6 fixed point for exact-integer comparison (no ULP noise). */
static int64_t density_fixed(double d) { return llround(d * 1000000.0); }

/* --- XFORM-01: defaults --- */

void test_defaults_allowed_transforms(void) {
    nt_atlas_opts_t o = nt_atlas_opts_defaults();
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(NT_ATLAS_TRANSFORMS_ALL, o.allowed_transforms, "atlas default mask must be ALL");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xFFU, o.allowed_transforms, "atlas default mask must be 0xFF");
    nt_atlas_sprite_opts_t s = nt_atlas_sprite_opts_defaults();
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0U, s.allowed_transforms, "sprite default mask must be 0 (inherit)");
}

/* --- XFORM-04: forbidden-orientation (unpacked output check, D-11) --- */

void test_export_mask_emits_only_identity_and_rot90(void) {
    const char *path = TMP_DIR "/xform_export.ntpack";
    TEST_ASSERT_TRUE_MESSAGE(build_fixture_pack(NT_ATLAS_TRANSFORMS_EXPORT, "export", path), "EXPORT pack failed");
    /* EXPORT = IDENTITY|ROT90 → the only bits set are values 0 and 5. */
    assert_all_in_mask(path, NT_ATLAS_TRANSFORMS_EXPORT, "region transform outside {identity, rot90}");
}

/* --- XFORM-04: a partial rotation mask never packs worse than identity-only --- */

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
    /* Page-area efficiency (Σ poly_px / Σ page_px) is the packer's objective, so
     * EXPORT (⊇ IDENTITY choices) can never do worse. Frontier density measures
     * used-bbox tightness (arrangement-dependent), not packing quality — on this
     * fixture EXPORT transposes the whole page (128x64 vs 64x128) at equal texture
     * density, which lowers frontier while leaving fill_texture unchanged. */
    TEST_ASSERT_GREATER_OR_EQUAL_INT64_MESSAGE(density_fixed(mi.density_fill_texture), density_fixed(me.density_fill_texture), "EXPORT texture density must be >= IDENTITY texture density");
}

/* --- XFORM-03: per-sprite mask intersects the atlas mask --- */

void test_per_sprite_mask_intersection(void) {
    const char *path = TMP_DIR "/xform_intersect.ntpack";
    TEST_ASSERT_TRUE_MESSAGE(build_intersection_pack(path), "intersection pack failed");
    uint8_t t[64];
    int n = 0;
    TEST_ASSERT_TRUE_MESSAGE(collect_transforms(path, t, 64, &n), "collect transforms failed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(4, n, "expected 4 regions in add order");
    /* Sprite 1 masked to IDENTITY|FLIP_H → its region transform is 0 or 1. */
    TEST_ASSERT_TRUE_MESSAGE(t[1] == NT_ATLAS_XFORM_IDENTITY || t[1] == NT_ATLAS_XFORM_FLIP_H, "masked sprite emitted a forbidden transform");
    /* Inheriting sprites still resolve to a valid D4 value (identity always representable). */
    for (int i = 0; i < n; ++i) {
        TEST_ASSERT_TRUE_MESSAGE(t[i] <= NT_ATLAS_XFORM_ANTITRANSPOSE, "transform value out of D4 range");
    }
}

/* --- XFORM-03: slice9 emits only identity regardless of the atlas mask --- */

void test_slice9_emits_identity(void) {
    const char *path = TMP_DIR "/xform_slice9.ntpack";
    TEST_ASSERT_TRUE_MESSAGE(build_slice9_pack(path), "slice9 pack failed");
    uint8_t t[64];
    int n = 0;
    TEST_ASSERT_TRUE_MESSAGE(collect_transforms(path, t, 64, &n), "collect transforms failed");
    TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, n, "no slice9 region unpacked");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(NT_ATLAS_XFORM_IDENTITY, t[0], "slice9 sprite must emit identity");
}

/* --- XFORM-02: byte-identity to the Plan 81-01 master etalons --- */

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
     * etalon shows WHAT moved (D-14). */
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
    RUN_TEST(test_slice9_emits_identity);
    RUN_TEST(test_golden_byte_identity_all);
    RUN_TEST(test_golden_byte_identity_identity);
    return UNITY_END();
}
