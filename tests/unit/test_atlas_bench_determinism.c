/* Default packing keeps density and hull metrics stable; wall-clock time is intentionally excluded. */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <math.h>
#include <stdint.h>
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
#include "ntpack_parse.h"
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

void setUp(void) {}
void tearDown(void) {}

/* --- Deterministic mini-corpus ---
 * Every sprite's bytes are produced by a pure integer function of (x, y), so the
 * corpus is byte-identical on every run with zero fixture-file / LFS dependency.
 * All sprites carry opaque content (never fully transparent — that trips the
 * empty-mask-after-trim abort, see test_atlas_transparent_trim). */

typedef enum {
    SPR_SOLID,     /* fully opaque coloured rectangle */
    SPR_TRI_LOWER, /* opaque where x >= y (lower-right triangle) */
    SPR_TRI_RAMP,  /* diagonal alpha ramp — AA-ish triangular silhouette */
    SPR_BORDERED,  /* opaque square inset in a transparent border (trim variety) */
} spr_kind_t;

typedef struct {
    const char *name;
    uint32_t w, h;
    spr_kind_t kind;
    uint8_t r, g, b;
} spr_spec_t;

/* 7 sprites: solid rects of varied aspect, two triangles (hard + ramp edge),
 * one fully-opaque square, one inset-border square. Fixed, order-stable. */
/* clang-format off */
static const spr_spec_t k_corpus[] = {
    {"solid_a",   16, 16, SPR_SOLID,     200,  40,  40},
    {"solid_b",   24, 16, SPR_SOLID,      40, 200,  40},
    {"solid_c",   16, 24, SPR_SOLID,      40,  40, 200},
    {"opaque_sq", 20, 20, SPR_SOLID,     220, 220, 220},
    {"tri_lower", 32, 32, SPR_TRI_LOWER, 240, 180,  20},
    {"tri_ramp",  28, 20, SPR_TRI_RAMP,   20, 200, 220},
    {"bordered",  18, 18, SPR_BORDERED,  180,  20, 180},
};
/* clang-format on */
#define CORPUS_COUNT ((int)(sizeof(k_corpus) / sizeof(k_corpus[0])))

/* Fill an RGBA buffer from a spec — pure, no globals, byte-identical each call. */
static void gen_sprite(uint8_t *px, const spr_spec_t *s) {
    for (uint32_t y = 0; y < s->h; ++y) {
        for (uint32_t x = 0; x < s->w; ++x) {
            uint8_t a = 0;
            switch (s->kind) {
            case SPR_SOLID:
                a = 255;
                break;
            case SPR_TRI_LOWER:
                a = (x >= y) ? 255 : 0;
                break;
            case SPR_TRI_RAMP: {
                /* Opaque triangle x >= y; a 3px diagonal band ramps 64->255 for an
                 * AA-ish edge. Silhouette stays triangular (alpha >= threshold). */
                int32_t d = (int32_t)x - (int32_t)y;
                if (d >= 3) {
                    a = 255;
                } else if (d >= 0) {
                    a = (uint8_t)(64 + d * 64); /* 64,128,192 across the band */
                } else {
                    a = 0;
                }
                break;
            }
            case SPR_BORDERED: {
                bool inside = (x >= 4 && x < s->w - 4 && y >= 4 && y < s->h - 4);
                a = inside ? 255 : 0;
                break;
            }
            }
            uint8_t *p = px + (size_t)(y * s->w + x) * 4;
            p[0] = s->r;
            p[1] = s->g;
            p[2] = s->b;
            p[3] = a;
        }
    }
}

/* Pack the whole mini-corpus with nt_atlas_opts_defaults() (NO cache dir → real
 * default path) to `path`, then parse the produced .ntpack into `out`.
 * Returns true on a clean pack + parse. */
static bool pack_and_parse_corpus(const char *path, nt_bench_atlas_metrics_t *out) {
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);

    NtBuilderContext *ctx = nt_builder_start_pack(path);
    if (!ctx) {
        return false;
    }

    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    nt_builder_begin_atlas(ctx, "det_corpus", &opts);

    uint8_t *bufs[CORPUS_COUNT] = {0};
    for (int i = 0; i < CORPUS_COUNT; ++i) {
        const spr_spec_t *s = &k_corpus[i];
        bufs[i] = (uint8_t *)malloc((size_t)s->w * s->h * 4);
        TEST_ASSERT_NOT_NULL(bufs[i]);
        gen_sprite(bufs[i], s);
        /* raw sprites require an explicit name (no path to derive one from). */
        nt_builder_atlas_add_raw(ctx, bufs[i], s->w, s->h, &(nt_atlas_sprite_opts_t){.name = s->name, .origin_x = 0.5F, .origin_y = 0.5F});
    }

    nt_builder_end_atlas(ctx);
    nt_build_result_t r = nt_builder_finish_pack(ctx);
    nt_builder_free_pack(ctx);

    for (int i = 0; i < CORPUS_COUNT; ++i) {
        free(bufs[i]);
    }
    if (r != NT_BUILD_OK) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    return nt_bench_parse_ntpack(path, out) == 0;
}

/* Round a density to 1e-6 fixed point — exact-integer equality across two packs
 * avoids raw double-bit comparison flagging benign last-ULP noise as regression. */
static int64_t density_fixed(double d) { return llround(d * 1000000.0); }

/* Two in-process packs of the SAME corpus at default opts must produce identical
 * region/page/vertex/hull counts and a bit-stable density. */
void test_metrics_stable_across_two_packs(void) {
    nt_bench_atlas_metrics_t a, b;
    TEST_ASSERT_TRUE_MESSAGE(pack_and_parse_corpus(TMP_DIR "/det_corpus_a.ntpack", &a), "pack/parse A failed");
    TEST_ASSERT_TRUE_MESSAGE(pack_and_parse_corpus(TMP_DIR "/det_corpus_b.ntpack", &b), "pack/parse B failed");

    TEST_ASSERT_EQUAL_UINT16(a.region_count, b.region_count);
    TEST_ASSERT_EQUAL_UINT16(a.page_count, b.page_count);
    TEST_ASSERT_EQUAL_UINT32(a.total_vertex_count, b.total_vertex_count);
    TEST_ASSERT_EQUAL_UINT32(a.hull_vert_total, b.hull_vert_total);
    TEST_ASSERT_EQUAL_INT64(density_fixed(a.density_fill_texture), density_fixed(b.density_fill_texture));
    TEST_ASSERT_EQUAL_INT64(density_fixed(a.density_fill_frontier), density_fixed(b.density_fill_frontier));
}

/* These pins move only with an intentional default-output or cache-key change. */
#define PIN_REGION_COUNT 7
#define PIN_PAGE_COUNT 1
#define PIN_HULL_VERT_TOTAL 36
#define PIN_DENSITY_FILL_TEXTURE 0.363644
#define PIN_DENSITY_FILL_FRONTIER 0.636539
#define PIN_DENSITY_TOL 1e-4

void test_metrics_match_pinned_baseline(void) {
    nt_bench_atlas_metrics_t m;
    TEST_ASSERT_TRUE_MESSAGE(pack_and_parse_corpus(TMP_DIR "/det_corpus_pin.ntpack", &m), "pack/parse failed");

    TEST_ASSERT_EQUAL_UINT16(PIN_REGION_COUNT, m.region_count);
    TEST_ASSERT_EQUAL_UINT16(PIN_PAGE_COUNT, m.page_count);
    TEST_ASSERT_EQUAL_UINT32(PIN_HULL_VERT_TOTAL, m.hull_vert_total);
    TEST_ASSERT_TRUE_MESSAGE(fabs(m.density_fill_texture - PIN_DENSITY_FILL_TEXTURE) < PIN_DENSITY_TOL, "density_fill_texture drifted from pinned baseline");
    TEST_ASSERT_TRUE_MESSAGE(fabs(m.density_fill_frontier - PIN_DENSITY_FILL_FRONTIER) < PIN_DENSITY_TOL, "density_fill_frontier drifted from pinned baseline");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_metrics_stable_across_two_packs);
    RUN_TEST(test_metrics_match_pinned_baseline);
    return UNITY_END();
}
