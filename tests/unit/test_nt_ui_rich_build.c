/* Rich-text run-list build + style-stack composition + bold/italic variant select.
 * No GL: the builder writes a frame-scratch SoA; we read it back via the test probes. */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "atlas/nt_atlas.h"
#include "clay.h"
#include "core/nt_assert.h"
#include "font/nt_font.h"
#include "hash/nt_hash.h"
#include "memory/nt_mem_scratch.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_rich_tagset.h"
#include "ui/nt_ui_rich_text.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

void setUp(void) {
    nt_test_assert_install();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static bool approx(float a, float b) { return fabsf(a - b) < 1e-5F; }

/* A four-member family with distinct ids so variant select is observable. */
static void family(nt_font_t out[4], uint32_t r, uint32_t b, uint32_t i, uint32_t bi) {
    out[0] = (nt_font_t){.id = r};
    out[1] = (nt_font_t){.id = b};
    out[2] = (nt_font_t){.id = i};
    out[3] = (nt_font_t){.id = bi};
}

/* (1) push_scale(1.5) then push_scale(2.0) -> composed scale 3.0 (x multiplies). */
static void test_scale_multiplies(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    nt_ui_rich_begin(s_fx.ctx, &base);
    nt_ui_rich_push_scale(s_fx.ctx, 1.5F);
    nt_ui_rich_push_scale(s_fx.ctx, 2.0F);
    nt_ui_rich_text_n(s_fx.ctx, "x", 1);
    nt_ui_rich_end(s_fx.ctx);

    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_rich_test_run_count(s_fx.ctx));
    nt_ui_rich_style_t s = nt_ui_rich_test_run_style(s_fx.ctx, 0);
    TEST_ASSERT_TRUE_MESSAGE(approx(s.scale, 3.0F), "1.5 * 2.0 == 3.0 (multiplicative)");
}

/* (2) innermost color override wins; pop restores the previous. */
static void test_color_override_and_pop(void) {
    const uint32_t color_a = 0xFF112233U;
    const uint32_t color_b = 0xFFAABBCCU;
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    nt_ui_rich_begin(s_fx.ctx, &base);
    nt_ui_rich_push_color(s_fx.ctx, color_a);
    nt_ui_rich_push_color(s_fx.ctx, color_b);
    nt_ui_rich_text_n(s_fx.ctx, "inner", 5); /* run 0: color B */
    nt_ui_rich_pop(s_fx.ctx);                /* back to color A */
    nt_ui_rich_text_n(s_fx.ctx, "outer", 5); /* run 1: color A */
    nt_ui_rich_end(s_fx.ctx);

    TEST_ASSERT_EQUAL_UINT32(2U, nt_ui_rich_test_run_count(s_fx.ctx));
    TEST_ASSERT_EQUAL_HEX32(color_b, nt_ui_rich_test_run_style(s_fx.ctx, 0).color_abgr);
    TEST_ASSERT_EQUAL_HEX32(color_a, nt_ui_rich_test_run_style(s_fx.ctx, 1).color_abgr);
}

/* (3a) bold selects font_id[1]; bold+italic selects font_id[3]. */
static void test_variant_selects_family_member(void) {
    nt_font_t fam[4];
    family(fam, 10, 11, 12, 13);
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    nt_ui_rich_begin(s_fx.ctx, &base);
    nt_ui_rich_push_font(s_fx.ctx, fam);

    nt_ui_rich_push_bold(s_fx.ctx);
    nt_ui_rich_text_n(s_fx.ctx, "B", 1); /* run 0: bold -> font_id[1] */
    nt_ui_rich_push_italic(s_fx.ctx);
    nt_ui_rich_text_n(s_fx.ctx, "BI", 2); /* run 1: bold+italic -> font_id[3] */
    nt_ui_rich_pop(s_fx.ctx);             /* drop italic */
    nt_ui_rich_pop(s_fx.ctx);             /* drop bold */
    nt_ui_rich_text_n(s_fx.ctx, "R", 1);  /* run 2: regular -> font_id[0] */
    nt_ui_rich_end(s_fx.ctx);

    TEST_ASSERT_EQUAL_UINT32(3U, nt_ui_rich_test_run_count(s_fx.ctx));
    TEST_ASSERT_EQUAL_UINT32(11U, nt_ui_rich_test_run_font(s_fx.ctx, 0).id);
    TEST_ASSERT_EQUAL_UINT32(13U, nt_ui_rich_test_run_font(s_fx.ctx, 1).id);
    TEST_ASSERT_EQUAL_UINT32(10U, nt_ui_rich_test_run_font(s_fx.ctx, 2).id);
}

/* (3b) a family MISSING the bold-italic member falls back B->R, and italic with no
 * italic member raises the synthetic-shear flag. */
static void test_variant_fallback_and_synth_italic(void) {
    nt_font_t fam[4];
    family(fam, 10, 11, 0, 0); /* only R + B; no italic, no bold-italic */
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    nt_ui_rich_begin(s_fx.ctx, &base);
    nt_ui_rich_push_font(s_fx.ctx, fam);

    nt_ui_rich_push_bold(s_fx.ctx);
    nt_ui_rich_push_italic(s_fx.ctx);
    nt_ui_rich_text_n(s_fx.ctx, "BI", 2); /* BI missing -> fall back to B (11), shear flagged */
    nt_ui_rich_end(s_fx.ctx);

    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_rich_test_run_count(s_fx.ctx));
    TEST_ASSERT_EQUAL_UINT32(11U, nt_ui_rich_test_run_font(s_fx.ctx, 0).id); /* B fallback */
    TEST_ASSERT_TRUE_MESSAGE((nt_ui_rich_test_run_flags(s_fx.ctx, 0) & NT_UI_RICH_RUN_SYNTH_ITALIC) != 0U, "missing italic member -> synthetic shear flag");
}

/* (4) two adjacent text_n with the SAME composed style share ONE run (dedup); a
 * color change between them starts a NEW run. */
static void test_dedup_and_split_on_style_change(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    nt_ui_rich_begin(s_fx.ctx, &base);
    nt_ui_rich_text_n(s_fx.ctx, "ab", 2);
    nt_ui_rich_text_n(s_fx.ctx, "cd", 2); /* same style -> extends run 0 */
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_rich_test_run_count(s_fx.ctx));

    nt_ui_rich_push_color(s_fx.ctx, 0xFF00FF00U);
    nt_ui_rich_text_n(s_fx.ctx, "ef", 2); /* color change -> new run */
    nt_ui_rich_end(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32(2U, nt_ui_rich_test_run_count(s_fx.ctx));
}

/* ---- builder == parser identity (MARK-67-04) ---- */
/* Snapshot the per-call run-list into a flat array so we can compare two independent calls
 * (a frame begin/end resets the scratch, so we cannot hold pointers across them). */
#define MAX_SNAP_RUNS 16
#define MAX_SNAP_BYTES 256
typedef struct {
    uint32_t count;
    nt_rich_atom_kind_t kind[MAX_SNAP_RUNS];
    uint32_t font[MAX_SNAP_RUNS];
    uint32_t color[MAX_SNAP_RUNS];
    uint8_t flags[MAX_SNAP_RUNS];
    uint32_t link[MAX_SNAP_RUNS];
    uint64_t img_hash[MAX_SNAP_RUNS];
    uint32_t img_atlas[MAX_SNAP_RUNS];
    uint32_t text_off[MAX_SNAP_RUNS];
    uint32_t text_len[MAX_SNAP_RUNS];
    uint32_t total_bytes;
    char bytes[MAX_SNAP_BYTES];
} run_snapshot_t;

static void snapshot_runs(nt_ui_context_t *ctx, run_snapshot_t *snap) {
    memset(snap, 0, sizeof *snap);
    snap->count = nt_ui_rich_test_run_count(ctx);
    TEST_ASSERT_TRUE(snap->count <= MAX_SNAP_RUNS);
    for (uint32_t i = 0; i < snap->count; i++) {
        snap->kind[i] = nt_ui_rich_test_run_kind(ctx, i);
        snap->font[i] = nt_ui_rich_test_run_font(ctx, i).id;
        snap->color[i] = nt_ui_rich_test_run_style(ctx, i).color_abgr;
        snap->flags[i] = nt_ui_rich_test_run_flags(ctx, i);
        snap->link[i] = nt_ui_rich_test_run_link(ctx, i);
        const nt_atlas_region_ref_t ref = nt_ui_rich_test_run_image_ref(ctx, i);
        snap->img_hash[i] = ref.name_hash;
        snap->img_atlas[i] = ref.atlas.id;
        const uint32_t tl = nt_ui_rich_test_run_text_len(ctx, i);
        snap->text_off[i] = snap->total_bytes;
        snap->text_len[i] = tl;
        if (tl > 0U) {
            const char *t = nt_ui_rich_test_run_text(ctx, i);
            TEST_ASSERT_TRUE(snap->total_bytes + tl <= MAX_SNAP_BYTES);
            memcpy(snap->bytes + snap->total_bytes, t, tl);
            snap->total_bytes += tl;
        }
    }
}

static void assert_snapshots_identical(const run_snapshot_t *a, const run_snapshot_t *b) {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(a->count, b->count, "run count differs");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(a->total_bytes, b->total_bytes, "total text bytes differ");
    if (a->total_bytes > 0U) {
        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(a->bytes, b->bytes, a->total_bytes, "text bytes differ");
    }
    for (uint32_t i = 0; i < a->count; i++) {
        TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)a->kind[i], (uint32_t)b->kind[i], "run kind differs");
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(a->font[i], b->font[i], "run font differs");
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(a->color[i], b->color[i], "run color differs");
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(a->flags[i], b->flags[i], "run flags differ");
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(a->link[i], b->link[i], "run link differs");
        TEST_ASSERT_EQUAL_HEX64_MESSAGE(a->img_hash[i], b->img_hash[i], "image name_hash differs");
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(a->img_atlas[i], b->img_atlas[i], "image atlas differs");
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(a->text_len[i], b->text_len[i], "run text len differs");
    }
}

/* The base style carrying a default atlas so <img=heart/> resolves against it. */
static nt_ui_rich_style_t base_with_atlas(nt_resource_t atlas) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.default_atlas = nt_atlas_ref(atlas, 0U); /* name_hash filled per-image by the parser/builder */
    return base;
}

/* "<b>HP</b> <img=heart/> full" == begin; push_bold; text("HP"); pop; text(" "); image(heart); text(" full"); end. */
static void test_markup_equals_builder_core(void) {
    const nt_resource_t atlas = {.id = 7U};
    const nt_ui_rich_style_t base = base_with_atlas(atlas);
    const uint64_t heart = nt_hash64_str("heart").value;

    /* Builder path. */
    nt_ui_rich_begin(s_fx.ctx, &base);
    nt_ui_rich_push_bold(s_fx.ctx);
    nt_ui_rich_text_n(s_fx.ctx, "HP", 2);
    nt_ui_rich_pop(s_fx.ctx);
    nt_ui_rich_text_n(s_fx.ctx, " ", 1);
    nt_ui_rich_image(s_fx.ctx, nt_atlas_ref(atlas, heart), NT_RICH_VALIGN_MIDDLE, 0.0F, 1.0F);
    nt_ui_rich_text_n(s_fx.ctx, " full", 5);
    nt_ui_rich_end(s_fx.ctx);
    run_snapshot_t builder;
    snapshot_runs(s_fx.ctx, &builder);

    /* Parser path (fresh scratch). */
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    const char *markup = "<b>HP</b> <img=heart/> full";
    nt_ui_rich_parse(s_fx.ctx, NULL, &base, markup, strlen(markup));
    run_snapshot_t parser;
    snapshot_runs(s_fx.ctx, &parser);

    assert_snapshots_identical(&builder, &parser);
}

/* <color=#FF8800>, <scale=1.5>, <link=quest1> intrinsic; <font=heading> via the tagset. */
static void test_markup_equals_builder_full(void) {
    nt_font_t fam[4];
    family(fam, 20, 21, 22, 23);
    nt_ui_rich_tagset_t ts;
    nt_ui_rich_tagset_init(&ts);
    nt_ui_rich_tagset_register_font(&ts, "heading", fam);

    const nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    const uint32_t color = 0xFF0088FFU; /* <color=#FF8800> -> packed AABBGGRR (RGB FF8800) */

    /* Builder path. */
    nt_ui_rich_begin(s_fx.ctx, &base);
    nt_ui_rich_push_color(s_fx.ctx, color);
    nt_ui_rich_text_n(s_fx.ctx, "hot", 3);
    nt_ui_rich_pop(s_fx.ctx);
    nt_ui_rich_push_scale(s_fx.ctx, 1.5F);
    nt_ui_rich_text_n(s_fx.ctx, "big", 3);
    nt_ui_rich_pop(s_fx.ctx);
    nt_ui_rich_push_font(s_fx.ctx, fam);
    nt_ui_rich_text_n(s_fx.ctx, "head", 4);
    nt_ui_rich_pop(s_fx.ctx);
    nt_ui_rich_link(s_fx.ctx, nt_hash32_str("quest1").value);
    nt_ui_rich_text_n(s_fx.ctx, "go", 2);
    nt_ui_rich_link(s_fx.ctx, 0U);
    nt_ui_rich_end(s_fx.ctx);
    run_snapshot_t builder;
    snapshot_runs(s_fx.ctx, &builder);

    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    const char *markup = "<color=#FF8800>hot</color><scale=1.5>big</scale><font=heading>head</font><link=quest1>go</link>";
    nt_ui_rich_parse(s_fx.ctx, &ts, &base, markup, strlen(markup));
    run_snapshot_t parser;
    snapshot_runs(s_fx.ctx, &parser);

    assert_snapshots_identical(&builder, &parser);
    /* scale folds into font size, not a run field -- assert the composed scale matches. */
    TEST_ASSERT_TRUE(approx(nt_ui_rich_test_run_style(s_fx.ctx, 1).scale, 1.5F));
}

/* <img=alias:region/> resolves the alias via the tagset to a different atlas than the default. */
static void test_markup_img_alias(void) {
    const nt_resource_t default_atlas = {.id = 7U};
    const nt_resource_t icons_atlas = {.id = 9U};
    nt_ui_rich_tagset_t ts;
    nt_ui_rich_tagset_init(&ts);
    nt_ui_rich_tagset_register_atlas(&ts, "icons", icons_atlas);

    const nt_ui_rich_style_t base = base_with_atlas(default_atlas);
    const uint64_t gold = nt_hash64_str("gold").value;

    nt_ui_rich_begin(s_fx.ctx, &base);
    nt_ui_rich_image(s_fx.ctx, nt_atlas_ref(icons_atlas, gold), NT_RICH_VALIGN_MIDDLE, 0.0F, 1.0F);
    nt_ui_rich_end(s_fx.ctx);
    run_snapshot_t builder;
    snapshot_runs(s_fx.ctx, &builder);

    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    const char *markup = "<img=icons:gold/>";
    nt_ui_rich_parse(s_fx.ctx, &ts, &base, markup, strlen(markup));
    run_snapshot_t parser;
    snapshot_runs(s_fx.ctx, &parser);

    assert_snapshots_identical(&builder, &parser);
    TEST_ASSERT_EQUAL_UINT32(9U, parser.img_atlas[0]);
    TEST_ASSERT_EQUAL_HEX64(gold, parser.img_hash[0]);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_scale_multiplies);
    RUN_TEST(test_color_override_and_pop);
    RUN_TEST(test_variant_selects_family_member);
    RUN_TEST(test_variant_fallback_and_synth_italic);
    RUN_TEST(test_dedup_and_split_on_style_change);
    RUN_TEST(test_markup_equals_builder_core);
    RUN_TEST(test_markup_equals_builder_full);
    RUN_TEST(test_markup_img_alias);
    return UNITY_END();
}
