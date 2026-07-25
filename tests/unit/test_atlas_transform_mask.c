/* Transform-mask OUTPUT contract: every check reads the UNPACKED atlas —
 * region.transform or a pack SHA-256 — never the packer's internal filter.
 * Etalon capture recipe: tests/fixtures/transform_mask_golden/PROVENANCE.txt. */

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Include the Windows SDK before headers that may define C17 noreturn. */
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
#include "nt_builder_atlas_test.h"
#include "nt_pack_format.h"
#include "ntpack_parse.h"
#include "test_helpers/atlas_dedup_fixture.h"
#include "test_helpers/atlas_transform_fixture.h"
#include "unity.h"
/* clang-format on */

#define TMP_DIR "build/tests/tmp"
#define GOLDEN_DIR "tests/fixtures/transform_mask_golden"
#define DEDUP_GOLDEN_DIR "tests/fixtures/dedup_golden"

/* Atlas name the etalons were captured with. The name seeds the texture
 * page resource ids, so byte-identity to the etalons requires the same name. */
#define GOLDEN_ATLAS_NAME "transform_golden"
#define DEDUP_GOLDEN_ATLAS_NAME "dedup_golden"

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

/* Build the shared etalon fixture to `path` at the given atlas mask, thread=1.
 * Dedup is off so these etalons stay a pure transform-mask contract — dedup
 * behaviour is pinned separately by the dedup_golden etalon. */
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
    opts.dedup = false;
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

/* Three tall strips + ONE wide 44x8 strip the ALL packer transposes. override_wide
 * restricts the wide strip to IDENTITY|FLIP_H; the unmasked A/B control proves
 * the packer WOULD transpose it — otherwise the masked assert is vacuous. */
static bool build_override_pack(const char *path, bool override_wide) {
    (void)MKDIR(TMP_DIR);
    NtBuilderContext *ctx = nt_builder_start_pack(path);
    if (!ctx) {
        return false;
    }
    nt_builder_set_threads(ctx, 1);
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.allowed_transforms = NT_ATLAS_TRANSFORMS_ALL;
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, override_wide ? "override_masked" : "override_control", &opts);

    uint8_t px[48 * 48 * 4];
    const uint16_t dims[4][2] = {{8, 44}, {8, 44}, {44, 8}, {8, 40}};
    const char *names[4] = {"s0", "s1", "s2_wide", "s3"};
    for (int i = 0; i < 4; ++i) {
        fill_solid(px, dims[i][0], dims[i][1], (uint8_t)(180 + (i * 10)), 90, 60);
        nt_atlas_sprite_opts_t so = nt_atlas_sprite_opts_defaults();
        so.name = names[i];
        if (override_wide && i == 2) {
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

/* Atlas-level dedup ON over the 10-frame post-trim fixture — the etalon that
 * pins the folding behaviour the transform goldens deliberately exclude. */
static bool build_dedup_golden_pack(const char *path) {
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);
    NtBuilderContext *ctx = nt_builder_start_pack(path);
    if (!ctx) {
        return false;
    }
    nt_builder_set_threads(ctx, 1);
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, DEDUP_GOLDEN_ATLAS_NAME, &opts);
    atlas_dedup_fixture_add(atlas);
    (void)nt_atlas_commit(atlas);
    nt_build_result_t r = nt_builder_finish_pack(ctx);
    nt_builder_free_pack(ctx);
    return r == NT_BUILD_OK;
}

/* --- Region dump / transform extraction (unpacked output) --- */

#define REGIONS_DUMP_TMP TMP_DIR "/xform_regions_dump.txt"

/* Write the structural region dump for a produced pack into a malloc'd,
 * NUL-terminated string (LF line endings). `tmp_path` keeps the on-disk copy a
 * capture run can move straight into a fixture. */
static char *dump_regions_text(const char *pack_path, const char *tmp_path) {
    size_t len = 0;
    uint8_t *bytes = read_bin_file(pack_path, &len);
    if (!bytes) {
        return NULL;
    }
    FILE *f = fopen(tmp_path, "wb"); /* wb → LF, matches the LF-pinned etalon dumps */
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
    return read_text_file(tmp_path);
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

#define DUMP_ROW_CAP 64

/* Parsed dump rows ("idx transform umin vmin w h") in region-index order. */
static bool collect_dump_rows(const char *pack_path, unsigned (*out)[6], int cap, int *count) {
    char *txt = dump_regions_text(pack_path, REGIONS_DUMP_TMP);
    if (!txt) {
        return false;
    }
    int n = 0;
    const char *p = txt;
    while (*p) {
        const char *nl = strchr(p, '\n');
        const char *line_end = nl ? nl : p + strlen(p);
        if (n < cap && parse_dump_line_u6(p, line_end, out[n])) {
            ++n;
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

/* Extract region.transform values (in region-index order) from a produced pack. */
static bool collect_transforms(const char *pack_path, uint8_t *out, int cap, int *count) {
    unsigned rows[DUMP_ROW_CAP][6];
    const int limit = (cap < DUMP_ROW_CAP) ? cap : DUMP_ROW_CAP;
    if (!collect_dump_rows(pack_path, rows, limit, count)) {
        return false;
    }
    for (int i = 0; i < *count; ++i) {
        out[i] = (uint8_t)rows[i][1];
    }
    return true;
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

/* Per region against that region's OWN effective mask: a non-zero sprite mask
 * replaces the atlas mask, identity is the floor. An alias region must satisfy
 * this too — its transform is its own relative, not the root's orientation. */
static void assert_each_in_own_mask(const char *pack_path, uint8_t atlas_mask, const uint8_t *sprite_masks, int count, const char *what) {
    uint8_t t[64];
    int n = 0;
    TEST_ASSERT_TRUE_MESSAGE(collect_transforms(pack_path, t, 64, &n), "collect transforms failed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(count, n, "unexpected region count");
    for (int i = 0; i < n; ++i) {
        /* Range first: a corrupt transform >= 32 would make the shift UB. */
        TEST_ASSERT_TRUE_MESSAGE(t[i] < 8U, "transform value out of D4 range");
        const uint8_t eff = (uint8_t)((sprite_masks[i] != 0U ? sprite_masks[i] : atlas_mask) | NT_ATLAS_TRANSFORMS_IDENTITY);
        TEST_ASSERT_TRUE_MESSAGE((eff & (uint8_t)(1U << t[i])) != 0U, what);
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

void test_each_d4_mask_bit_selects_its_transform_value(void) {
    uint8_t values[8];
    TEST_ASSERT_EQUAL_UINT32(1, nt_atlas_test_collect_transform_values(0, values));
    TEST_ASSERT_EQUAL_UINT8(NT_ATLAS_XFORM_IDENTITY, values[0]);

    for (uint8_t transform = 1; transform < 8; ++transform) {
        uint8_t mask = (uint8_t)(1U << transform);
        TEST_ASSERT_EQUAL_UINT32(2, nt_atlas_test_collect_transform_values(mask, values));
        TEST_ASSERT_EQUAL_UINT8(NT_ATLAS_XFORM_IDENTITY, values[0]);
        TEST_ASSERT_EQUAL_UINT8(transform, values[1]);
    }
}

/* --- Forbidden orientations never reach the unpacked output --- */

void test_identity_rot90_mask_emits_only_identity_and_rot90(void) {
    const char *path = TMP_DIR "/xform_identity_rot90.ntpack";
    TEST_ASSERT_TRUE_MESSAGE(build_fixture_pack(NT_ATLAS_TRANSFORMS_IDENTITY_ROT90, "identity_rot90", path), "IDENTITY_ROT90 pack failed");
    assert_all_in_mask(path, NT_ATLAS_TRANSFORMS_IDENTITY_ROT90, "region transform outside {identity, rot90}");
    /* Positive pin: the fixture's dimension-swapping strips make rot90 a win, so a
     * regression that collapses partial masks to identity-only must fail here. */
    uint8_t t[64];
    int n = 0;
    TEST_ASSERT_TRUE_MESSAGE(collect_transforms(path, t, 64, &n), "collect transforms failed");
    bool saw_rot90 = false;
    for (int i = 0; i < n; ++i) {
        saw_rot90 = saw_rot90 || (t[i] == NT_ATLAS_XFORM_ROT90);
    }
    TEST_ASSERT_TRUE_MESSAGE(saw_rot90, "IDENTITY_ROT90 mask must actually emit rot90 on this fixture");
}

/* --- A partial rotation mask never packs worse than identity-only --- */

void test_identity_rot90_density_at_least_identity(void) {
    const char *identity_rot90_path = TMP_DIR "/xform_identity_rot90_density.ntpack";
    const char *identity_path = TMP_DIR "/xform_identity_density.ntpack";
    TEST_ASSERT_TRUE_MESSAGE(build_fixture_pack(NT_ATLAS_TRANSFORMS_IDENTITY_ROT90, "density", identity_rot90_path), "IDENTITY_ROT90 density pack failed");
    TEST_ASSERT_TRUE_MESSAGE(build_fixture_pack(NT_ATLAS_TRANSFORMS_IDENTITY, "density", identity_path), "IDENTITY density pack failed");

    nt_bench_atlas_metrics_t mr;
    nt_bench_atlas_metrics_t mi;
    memset(&mr, 0, sizeof(mr));
    memset(&mi, 0, sizeof(mi));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, nt_bench_parse_ntpack(identity_rot90_path, &mr), "parse IDENTITY_ROT90 pack");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, nt_bench_parse_ntpack(identity_path, &mi), "parse IDENTITY pack");
    /* Empirical pin, NOT a packer theorem — greedy packing with more orientations
     * can pack worse; after an intentional packer change, re-baseline. Pins
     * fill_texture: frontier density flips with page arrangement (128x64 vs 64x128). */
    TEST_ASSERT_GREATER_OR_EQUAL_INT64_MESSAGE(density_fixed(mi.density_fill_texture), density_fixed(mr.density_fill_texture), "IDENTITY_ROT90 texture density must be >= IDENTITY texture density");
}

/* --- A non-zero sprite mask may narrow the atlas default --- */

void test_sprite_mask_can_narrow_atlas_mask(void) {
    const char *control_path = TMP_DIR "/xform_override_control.ntpack";
    const char *masked_path = TMP_DIR "/xform_override_masked.ntpack";
    TEST_ASSERT_TRUE_MESSAGE(build_override_pack(control_path, false), "control pack failed");
    TEST_ASSERT_TRUE_MESSAGE(build_override_pack(masked_path, true), "masked pack failed");
    uint8_t t[64];
    /* A/B control: unrestricted, the packer transposes the wide strip. */
    collect_expect_n(control_path, 4, t);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(NT_ATLAS_XFORM_TRANSPOSE, t[2], "control: packer must transpose the wide strip — masked assert would be vacuous");
    /* Same fixture, wide strip masked to IDENTITY|FLIP_H → transform is 0 or 1. */
    collect_expect_n(masked_path, 4, t);
    TEST_ASSERT_TRUE_MESSAGE(t[2] == NT_ATLAS_XFORM_IDENTITY || t[2] == NT_ATLAS_XFORM_FLIP_H, "masked sprite emitted a forbidden transform");
    /* Inheriting sprites still resolve to a valid D4 value (identity always representable). */
    const uint8_t sprite_masks[4] = {0, 0, (uint8_t)(NT_ATLAS_TRANSFORM_IDENTITY | NT_ATLAS_TRANSFORM_FLIP_H), 0};
    assert_each_in_own_mask(masked_path, NT_ATLAS_TRANSFORMS_ALL, sprite_masks, 4, "region transform outside its own effective mask");
}

/* --- A zero atlas mask (zero-init struct) behaves as identity-only --- */

void test_zero_mask_behaves_as_identity(void) {
    const char *path = TMP_DIR "/xform_zeromask.ntpack";
    TEST_ASSERT_TRUE_MESSAGE(build_fixture_pack(0x00U, "zeromask", path), "zero-mask pack failed");
    assert_all_in_mask(path, NT_ATLAS_TRANSFORMS_IDENTITY, "zero mask must emit identity only");
}

/* Two 44x8 strips the ALL packer transposes + one 8x40 tall strip. */
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

/* --- A non-zero sprite mask replaces and may widen the atlas mask --- */

void test_sprite_mask_can_widen_atlas_mask(void) {
    const char *path = TMP_DIR "/xform_widen.ntpack";
    const uint8_t masks[3] = {NT_ATLAS_TRANSFORMS_ALL, NT_ATLAS_TRANSFORMS_ALL, NT_ATLAS_TRANSFORMS_ALL};
    TEST_ASSERT_TRUE_MESSAGE(build_masked_strips_pack(path, "widen", NT_ATLAS_TRANSFORMS_IDENTITY, masks), "widen pack failed");
    uint8_t t[64];
    collect_expect_n(path, 3, t);
    bool saw_non_identity = false;
    for (int i = 0; i < 3; ++i) {
        saw_non_identity = saw_non_identity || (t[i] != NT_ATLAS_XFORM_IDENTITY);
    }
    TEST_ASSERT_TRUE_MESSAGE(saw_non_identity, "sprite override must widen an identity-only atlas on this fixture");
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

/* Compare an already-produced pack against its committed etalon pair. The dump
 * lands in `dump_tmp` so a capture run can copy it into the fixture verbatim. */
static void assert_matches_etalon(const char *pack_path, const char *sha_path, const char *dump_path, const char *dump_tmp) {
    char actual_sha[65];
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, nt_bench_file_sha256_hex(pack_path, actual_sha), "hash produced pack");
    char expected_sha[65];
    read_sha_file(sha_path, expected_sha);

    char *dump = dump_regions_text(pack_path, dump_tmp);
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

static void assert_golden(uint8_t mask, const char *sha_path, const char *dump_path, const char *pack_path) {
    TEST_ASSERT_TRUE_MESSAGE(build_fixture_pack(mask, GOLDEN_ATLAS_NAME, pack_path), "golden pack build failed");
    assert_matches_etalon(pack_path, sha_path, dump_path, TMP_DIR "/xform_golden_dump.txt");
}

void test_golden_byte_identity_all(void) { assert_golden(NT_ATLAS_TRANSFORMS_ALL, GOLDEN_DIR "/etalon_all.sha256", GOLDEN_DIR "/etalon_all.dump.txt", TMP_DIR "/xform_golden_all.ntpack"); }

void test_golden_byte_identity_identity(void) {
    assert_golden(NT_ATLAS_TRANSFORMS_IDENTITY, GOLDEN_DIR "/etalon_identity.sha256", GOLDEN_DIR "/etalon_identity.dump.txt", TMP_DIR "/xform_golden_identity.ntpack");
}

/* --- Byte-identity of a dedup-ON pack (the folding contract) --- */

void test_golden_byte_identity_dedup_on(void) {
    const char *path = TMP_DIR "/dedup_golden.ntpack";
    TEST_ASSERT_TRUE_MESSAGE(build_dedup_golden_pack(path), "dedup golden pack build failed");
    /* Non-vacuity: without the fold the 10 frames would not share 4 placements. */
    nt_atlas_dedup_region_t regions[NT_ATLAS_DEDUP_FRAME_COUNT];
    uint32_t count = 0;
    size_t len = 0;
    uint8_t *bytes = read_bin_file(path, &len);
    TEST_ASSERT_NOT_NULL_MESSAGE(bytes, "read dedup golden pack");
    const bool ok = atlas_dedup_collect_regions(bytes, len, regions, NT_ATLAS_DEDUP_FRAME_COUNT, &count);
    free(bytes);
    TEST_ASSERT_TRUE_MESSAGE(ok, "collect dedup golden regions");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(NT_ATLAS_DEDUP_FRAME_COUNT, count, "unexpected region count");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(NT_ATLAS_DEDUP_STATE_COUNT, atlas_dedup_distinct_placements(regions, count), "the etalon must be captured on a folded pack");

    assert_matches_etalon(path, DEDUP_GOLDEN_DIR "/etalon_dedup_on.sha256", DEDUP_GOLDEN_DIR "/etalon_dedup_on.dump.txt", TMP_DIR "/dedup_golden_dump.txt");
}

/* --- An alias region's transform lies in its OWN effective mask --- */

/* build_override_pack's layout with the wide strip doubled into a byte-identical
 * pair: the twins fold onto one placement the ALL packer transposes to align with
 * the tall column. Each twin carries its own mask, so the group intersection is
 * readable per region from the unpacked output. */
#define TWIN_SPRITE_COUNT 5
#define TWIN_A 2
#define TWIN_B 3

static bool build_twin_mask_pack(const char *path, const char *name, const uint8_t *masks) {
    (void)MKDIR(TMP_DIR);
    NtBuilderContext *ctx = nt_builder_start_pack(path);
    if (!ctx) {
        return false;
    }
    nt_builder_set_threads(ctx, 1);
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.allowed_transforms = NT_ATLAS_TRANSFORMS_ALL;
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, name, &opts);

    uint8_t px[48 * 48 * 4];
    const uint16_t dims[TWIN_SPRITE_COUNT][2] = {{8, 44}, {8, 44}, {44, 8}, {44, 8}, {8, 40}};
    const char *names[TWIN_SPRITE_COUNT] = {"w0", "w1", "twin_a", "twin_b", "w3"};
    for (int i = 0; i < TWIN_SPRITE_COUNT; ++i) {
        /* The twins share one colour, so their content is byte-identical. */
        const uint8_t r = (i == TWIN_B) ? (uint8_t)(180 + (TWIN_A * 10)) : (uint8_t)(180 + (i * 10));
        fill_solid(px, dims[i][0], dims[i][1], r, 90, 60);
        nt_atlas_sprite_opts_t so = nt_atlas_sprite_opts_defaults();
        so.name = names[i];
        so.allowed_transforms = masks[i];
        nt_atlas_add_raw(atlas, px, dims[i][0], dims[i][1], &so);
    }
    (void)nt_atlas_commit(atlas);
    nt_build_result_t r = nt_builder_finish_pack(ctx);
    nt_builder_free_pack(ctx);
    return r == NT_BUILD_OK;
}

/* Collect the rows and require the twins to actually share one placement —
 * without the fold this case would say nothing about an alias region. */
static void collect_twin_rows(const char *pack_path, unsigned (*rows)[6]) {
    int n = 0;
    TEST_ASSERT_TRUE_MESSAGE(collect_dump_rows(pack_path, rows, DUMP_ROW_CAP, &n), "collect dump rows failed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(TWIN_SPRITE_COUNT, n, "unexpected region count");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(rows[TWIN_A][2], rows[TWIN_B][2], "the byte-identical twins must share one placement U");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(rows[TWIN_A][3], rows[TWIN_B][3], "the byte-identical twins must share one placement V");
}

void test_alias_region_transform_is_in_its_own_mask(void) {
    const char *control_path = TMP_DIR "/xform_twin_control.ntpack";
    const char *masked_path = TMP_DIR "/xform_twin_masked.ntpack";
    const uint8_t both_all[TWIN_SPRITE_COUNT] = {0, 0, NT_ATLAS_TRANSFORMS_ALL, NT_ATLAS_TRANSFORMS_ALL, 0};
    const uint8_t mixed[TWIN_SPRITE_COUNT] = {0, 0, NT_ATLAS_TRANSFORMS_ALL, NT_ATLAS_TRANSFORMS_IDENTITY, 0};
    unsigned rows[DUMP_ROW_CAP][6];

    /* A/B control: unrestricted, the packer orients the folded pair non-identity —
     * otherwise the masked assert below would be vacuous. */
    TEST_ASSERT_TRUE_MESSAGE(build_twin_mask_pack(control_path, "twin_control", both_all), "control twin pack failed");
    collect_twin_rows(control_path, rows);
    TEST_ASSERT_TRUE_MESSAGE(rows[TWIN_A][1] != NT_ATLAS_XFORM_IDENTITY, "control: the packer must orient the folded twins non-identity");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(rows[TWIN_A][1], rows[TWIN_B][1], "an identity-relative alias shares its root's orientation");
    assert_each_in_own_mask(control_path, NT_ATLAS_TRANSFORMS_ALL, both_all, TWIN_SPRITE_COUNT, "region transform outside its own effective mask");

    /* Same art, one twin restricted to identity: the group intersection collapses
     * the shared placement to identity for BOTH members. */
    TEST_ASSERT_TRUE_MESSAGE(build_twin_mask_pack(masked_path, "twin_masked", mixed), "masked twin pack failed");
    collect_twin_rows(masked_path, rows);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(NT_ATLAS_XFORM_IDENTITY, rows[TWIN_A][1], "a group with an identity-only member must pack at identity");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(NT_ATLAS_XFORM_IDENTITY, rows[TWIN_B][1], "a group with an identity-only member must pack at identity");
    assert_each_in_own_mask(masked_path, NT_ATLAS_TRANSFORMS_ALL, mixed, TWIN_SPRITE_COUNT, "region transform outside its own effective mask");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_allowed_transforms);
    RUN_TEST(test_each_d4_mask_bit_selects_its_transform_value);
    RUN_TEST(test_identity_rot90_mask_emits_only_identity_and_rot90);
    RUN_TEST(test_identity_rot90_density_at_least_identity);
    RUN_TEST(test_sprite_mask_can_narrow_atlas_mask);
    RUN_TEST(test_zero_mask_behaves_as_identity);
    RUN_TEST(test_sprite_mask_can_widen_atlas_mask);
    RUN_TEST(test_slice9_emits_identity);
    RUN_TEST(test_alias_region_transform_is_in_its_own_mask);
    RUN_TEST(test_golden_byte_identity_all);
    RUN_TEST(test_golden_byte_identity_identity);
    RUN_TEST(test_golden_byte_identity_dedup_on);
    return UNITY_END();
}
