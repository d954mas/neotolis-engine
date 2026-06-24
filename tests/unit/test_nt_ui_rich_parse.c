/* Rich-text markup parser + tagset vocabulary. No GL: the parser writes a frame-scratch
 * run-list (read back via the build probes); the tagset is a plain owned struct. */

#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "font/nt_font.h"
#include "hash/nt_hash.h"
#include "memory/nt_mem_scratch.h"
#include "resource/nt_resource.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_rich_fx.h"
#include "ui/nt_ui_rich_tagset.h"
#include "ui/nt_ui_rich_text.h"
#include "unity.h"
#include "utf8/nt_utf8.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

void setUp(void) {
    nt_test_assert_install();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static uint64_t h(const char *s) { return nt_hash64_str(s).value; }

/* (1) register_font then lookup by name returns the family; an unknown name misses. */
static void test_tagset_font_register_lookup(void) {
    nt_ui_rich_tagset_t ts;
    nt_ui_rich_tagset_init(&ts);

    const nt_font_t fam[4] = {{.id = 10}, {.id = 11}, {.id = 12}, {.id = 13}};
    nt_ui_rich_tagset_register_font(&ts, "heading", fam);

    nt_font_t out[4];
    TEST_ASSERT_TRUE(nt_ui_rich_tagset_lookup_font(&ts, h("heading"), out));
    TEST_ASSERT_EQUAL_UINT32(10U, out[0].id);
    TEST_ASSERT_EQUAL_UINT32(13U, out[3].id);

    nt_font_t miss[4];
    TEST_ASSERT_FALSE(nt_ui_rich_tagset_lookup_font(&ts, h("body"), miss));
}

/* (2) register_color / register_atlas / register_effect resolve by name. */
static void test_tagset_color_atlas_effect(void) {
    nt_ui_rich_tagset_t ts;
    nt_ui_rich_tagset_init(&ts);

    nt_ui_rich_tagset_register_color(&ts, "gold", 0xFF00D7FFU);
    nt_ui_rich_tagset_register_atlas(&ts, "icons", (nt_resource_t){.id = 42U});
    nt_ui_rich_tagset_register_effect(&ts, "wave", 3U);

    uint32_t color = 0;
    TEST_ASSERT_TRUE(nt_ui_rich_tagset_lookup_color(&ts, h("gold"), &color));
    TEST_ASSERT_EQUAL_HEX32(0xFF00D7FFU, color);

    nt_resource_t atlas = {0};
    TEST_ASSERT_TRUE(nt_ui_rich_tagset_lookup_atlas(&ts, h("icons"), &atlas));
    TEST_ASSERT_EQUAL_UINT32(42U, atlas.id);

    uint8_t fx = 0;
    TEST_ASSERT_TRUE(nt_ui_rich_tagset_lookup_effect(&ts, h("wave"), &fx));
    TEST_ASSERT_EQUAL_UINT8(3U, fx);
}

/* A stub custom effect fn (identity) used only to prove tagset custom registration/lookup. */
static nt_ui_rich_fx_result_t parse_stub_fx(uint32_t atom_idx, nt_rich_atom_kind_t kind, const float base_xy[2], const float base_wh[2], const float base_color[4], float time, bool hovered,
                                            void *user_data) {
    (void)atom_idx;
    (void)kind;
    (void)base_xy;
    (void)base_wh;
    (void)time;
    (void)hovered;
    (void)user_data;
    return nt_ui_rich_fx_identity(base_color);
}

/* Stub object measure fn (fixed box) used only to prove object-tag registration guards. */
static nt_ui_rich_object_measure_t parse_stub_object_measure(void *user_data) {
    (void)user_data;
    return (nt_ui_rich_object_measure_t){.width = 8.0F, .height = 8.0F, .ascent = 8.0F};
}

/* (2b) register_effect_fn registers a CUSTOM effect: the full lookup returns fn+user_data, and the
 * stock-only lookup MISSES it (so a custom name never resolves to a bogus stock id). */
static void test_tagset_custom_effect_fn(void) {
    nt_ui_rich_tagset_t ts;
    nt_ui_rich_tagset_init(&ts);

    int marker = 0;
    nt_ui_rich_tagset_register_effect_fn(&ts, "customfx", parse_stub_fx, &marker);

    /* Full lookup resolves the custom entry to its fn + user_data. */
    uint8_t id = 0xFFU;
    nt_ui_rich_fx_fn fn = NULL;
    void *user = NULL;
    TEST_ASSERT_TRUE(nt_ui_rich_tagset_lookup_effect_fn(&ts, h("customfx"), &id, &fn, &user));
    TEST_ASSERT_TRUE_MESSAGE(fn == parse_stub_fx, "custom lookup returns the registered fn");
    TEST_ASSERT_TRUE_MESSAGE(user == &marker, "custom lookup returns the registered user_data");

    /* Stock-only lookup misses the custom entry (fn != NULL). */
    uint8_t stock_id = 0;
    TEST_ASSERT_FALSE_MESSAGE(nt_ui_rich_tagset_lookup_effect(&ts, h("customfx"), &stock_id), "stock-only lookup misses a custom entry");

    /* register_effect_fn requires a non-NULL fn. */
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_tagset_register_effect_fn(&ts, "bad", NULL, NULL));
}

/* (2c) a STOCK entry coexists with custom entries: it resolves via BOTH the stock-only lookup and
 * the full lookup (which reports fn==NULL for a stock entry). */
static void test_tagset_stock_effect_coexists(void) {
    nt_ui_rich_tagset_t ts;
    nt_ui_rich_tagset_init(&ts);
    int marker = 0;
    nt_ui_rich_tagset_register_effect_fn(&ts, "customfx", parse_stub_fx, &marker);
    nt_ui_rich_tagset_register_effect(&ts, "stockfx", NT_UI_RICH_FX_ID_PULSE);

    uint8_t stock_id = 0;
    TEST_ASSERT_TRUE(nt_ui_rich_tagset_lookup_effect(&ts, h("stockfx"), &stock_id));
    TEST_ASSERT_EQUAL_UINT8(NT_UI_RICH_FX_ID_PULSE, stock_id);

    uint8_t id = 0;
    nt_ui_rich_fx_fn fn = parse_stub_fx;
    void *user = NULL;
    TEST_ASSERT_TRUE(nt_ui_rich_tagset_lookup_effect_fn(&ts, h("stockfx"), &id, &fn, &user));
    TEST_ASSERT_TRUE_MESSAGE(fn == NULL, "stock entry full-lookup reports fn==NULL");
    TEST_ASSERT_EQUAL_UINT8(NT_UI_RICH_FX_ID_PULSE, id);
}

/* (3) re-registering a name overrides it in place (no duplicate entry). */
static void test_tagset_override_in_place(void) {
    nt_ui_rich_tagset_t ts;
    nt_ui_rich_tagset_init(&ts);

    nt_ui_rich_tagset_register_color(&ts, "accent", 0xFF112233U);
    nt_ui_rich_tagset_register_color(&ts, "accent", 0xFFAABBCCU);
    TEST_ASSERT_EQUAL_UINT32(1U, ts.color_count);

    uint32_t color = 0;
    TEST_ASSERT_TRUE(nt_ui_rich_tagset_lookup_color(&ts, h("accent"), &color));
    TEST_ASSERT_EQUAL_HEX32(0xFFAABBCCU, color);
}

/* (4) reset empties every table; lookups then miss. */
static void test_tagset_reset(void) {
    nt_ui_rich_tagset_t ts;
    nt_ui_rich_tagset_init(&ts);
    nt_ui_rich_tagset_register_color(&ts, "gold", 0xFF00D7FFU);
    nt_ui_rich_tagset_reset(&ts);

    uint32_t color = 0;
    TEST_ASSERT_EQUAL_UINT32(0U, ts.color_count);
    TEST_ASSERT_FALSE(nt_ui_rich_tagset_lookup_color(&ts, h("gold"), &color));
}

/* (4b) an empty registration name ("") is a dead slot the parser can never address (value
 * parsers guard vlen>0) -> fail-early NT_ASSERT in every register_* (including the fn/object-tag
 * variants, which guard name[0]!='\0' too). */
static void test_tagset_empty_name_asserts(void) {
    nt_ui_rich_tagset_t ts;
    nt_ui_rich_tagset_init(&ts);
    const nt_font_t fam[4] = {{.id = 1}, {.id = 2}, {.id = 3}, {.id = 4}};
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_tagset_register_font(&ts, "", fam));
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_tagset_register_atlas(&ts, "", (nt_resource_t){.id = 1U}));
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_tagset_register_color(&ts, "", 0xFFFFFFFFU));
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_tagset_register_effect(&ts, "", 1U));
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_tagset_register_effect_fn(&ts, "", parse_stub_fx, NULL));
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_tagset_register_object_tag(&ts, "", parse_stub_object_measure, NULL, NULL));
}

/* ---- parser malformed-markup death tests ---- */
static void parse_lit(const char *m) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, NULL, NULL, m, strlen(m));
}

/* (5) unclosed tag at end of string -> NT_ASSERT. */
static void test_parse_unclosed_tag_asserts(void) { NT_TEST_EXPECT_ASSERT(parse_lit("<b>HP")); }

/* (6) mismatched close (<b>..</i>) -> NT_ASSERT. */
static void test_parse_mismatched_close_asserts(void) { NT_TEST_EXPECT_ASSERT(parse_lit("<b>HP</i>")); }

/* (7) unknown CORE-shaped tag -> NT_ASSERT. */
static void test_parse_unknown_tag_asserts(void) { NT_TEST_EXPECT_ASSERT(parse_lit("<bogus>x</bogus>")); }

/* (8) malformed hex in <color=#..> -> NT_ASSERT. */
static void test_parse_bad_hex_asserts(void) { NT_TEST_EXPECT_ASSERT(parse_lit("<color=#zzz>x</color>")); }

/* (9) a close tag with no matching open -> NT_ASSERT. In NT_ASSERT OFF the close-tag hard guard
 * early-returns before --depth, so depth never wraps to UINT_MAX (no open_stack[UINT_MAX] OOB). */
static void test_parse_orphan_close_asserts(void) { NT_TEST_EXPECT_ASSERT(parse_lit("HP</b>")); }

/* (9b) a mismatched link/style close (<link=1></b>): top==LINK but the close kind==BOLD. In FULL this
 * traps on the mismatch assert; the fix also makes OFF pop on `top` (LINK -> clear pending), never
 * nt_ui_rich_pop (which would pop the base style past 0). */
static void test_parse_mismatched_link_close_asserts(void) { NT_TEST_EXPECT_ASSERT(parse_lit("<link=1></b>")); }

/* (9c) an empty numeric value (<scale=>) -> NT_ASSERT. In OFF the rich_parse_float n==0 hard guard
 * returns a bounded 0.0F before any s[0] read (the push_scale>0 assert is the scale dev guard). */
static void test_parse_empty_scale_value_asserts(void) { NT_TEST_EXPECT_ASSERT(parse_lit("<scale=>x</scale>")); }

/* Parse a <fx=...> markup against a tagset that knows the stock "wave" + a custom "fade". */
static void parse_fx(const char *m) {
    nt_ui_rich_tagset_t ts;
    nt_ui_rich_tagset_init(&ts);
    nt_ui_rich_tagset_register_effect(&ts, "wave", NT_UI_RICH_FX_ID_WAVE);
    nt_ui_rich_tagset_register_effect_fn(&ts, "fade", parse_stub_fx, NULL);
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    nt_ui_rich_parse(s_fx.ctx, &ts, &base, m, strlen(m));
}

/* (8b) a well-formed <fx=wave amp=8 speed=3> parses without an assert (the happy path). */
static void test_parse_fx_params_ok(void) {
    parse_fx("<fx=wave amp=8 speed=3>hi</fx>");
    parse_fx("<fx=wave speed=2.5>hi</fx>"); /* a single param + a float value */
    TEST_ASSERT_TRUE_MESSAGE(true, "tuned <fx=...> markup parsed without an assert");
}

/* (8c) a bad float value in an fx param -> NT_ASSERT (fail-early). */
static void test_parse_fx_bad_float_asserts(void) { NT_TEST_EXPECT_ASSERT(parse_fx("<fx=wave amp=8x>hi</fx>")); }

/* (8d) an unknown fx param key -> NT_ASSERT. */
static void test_parse_fx_unknown_key_asserts(void) { NT_TEST_EXPECT_ASSERT(parse_fx("<fx=wave bogus=3>hi</fx>")); }

/* (8e) a param token with no '=' -> NT_ASSERT. */
static void test_parse_fx_no_equals_asserts(void) { NT_TEST_EXPECT_ASSERT(parse_fx("<fx=wave amp>hi</fx>")); }

/* (8f) k=v params on a CUSTOM-fn effect name -> NT_ASSERT (params apply to STOCK effects only). */
static void test_parse_fx_params_on_custom_asserts(void) { NT_TEST_EXPECT_ASSERT(parse_fx("<fx=fade amp=8>hi</fx>")); }

/* (8g) an empty fx param value (amp=) -> NT_ASSERT in FULL (rich_parse_float n>0). In OFF the n==0
 * hard guard returns 0.0F (= "use default") with no OOB read. */
static void test_parse_fx_empty_value_asserts(void) { NT_TEST_EXPECT_ASSERT(parse_fx("<fx=wave amp=>hi</fx>")); }

/* Parse markup against a tagset that registers an OBJECT tag "widget". The parse-side name hash
 * (nt_hash64(val, vlen)) MUST equal the register-side hash (nt_hash64_str) or lookup_object misses. */
static void parse_obj(const char *m) {
    nt_ui_rich_tagset_t ts;
    nt_ui_rich_tagset_init(&ts);
    nt_ui_rich_tagset_register_object_tag(&ts, "widget", parse_stub_object_measure, NULL, NULL);
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    nt_ui_rich_parse(s_fx.ctx, &ts, &base, m, strlen(m));
}

/* (8h) <obj=name/> self-closes and dispatches via the tagset -> a NT_RICH_ATOM_OBJECT run exists
 * (proves the parse-side hash matches the register-side hash and the run is appended). */
static void test_parse_obj_self_close_emits_object_run(void) {
    parse_obj("a <obj=widget/> b");
    const uint32_t runs = nt_ui_rich_test_run_count(s_fx.ctx);
    bool found_object = false;
    for (uint32_t i = 0; i < runs; i++) {
        if (nt_ui_rich_test_run_kind(s_fx.ctx, i) == NT_RICH_ATOM_OBJECT) {
            found_object = true;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(found_object, "<obj=widget/> produced a NT_RICH_ATOM_OBJECT run");
}

/* (8i) <obj=nope/> with a name not in the tagset -> NT_ASSERT (unknown object), and no bogus run. */
static void test_parse_obj_unknown_asserts(void) { NT_TEST_EXPECT_ASSERT(parse_obj("<obj=nope/>")); }

/* (8j) <obj/> with an empty name -> NT_ASSERT; the hard guard returns without pushing a run. */
static void test_parse_obj_empty_name_asserts(void) { NT_TEST_EXPECT_ASSERT(parse_obj("<obj/>")); }

/* (8k) <obj=x/> with a NULL tagset (a legit text-only config) -> NT_ASSERT in FULL. The OFF hard guard
 * (if (tagset == NULL) return; before the lookup) can't be unit-tested -- no OFF ctest preset exists --
 * but it is OFF-safe by construction (early-return BEFORE nt_ui_rich_tagset_lookup_object derefs ts). */
static void test_parse_obj_null_tagset_asserts(void) { NT_TEST_EXPECT_ASSERT(parse_lit("<obj=x/>")); }

/* (8l) <img=alias:region/> with a NULL tagset -> NT_ASSERT in FULL (alias needs a tagset). Same OFF
 * hard guard (early-return inside the colon branch before nt_ui_rich_tagset_lookup_atlas derefs ts);
 * by-construction OFF-safe, not unit-testable without an OFF preset. A VALID base is supplied so the
 * trap is the tagset assert (the colon branch), not the base-NULL guard at the top of rich_parse_img. */
static void parse_img_null_tagset(const char *m) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.default_atlas.atlas = s_fx.atlas.handle;
    nt_ui_rich_parse(s_fx.ctx, NULL, &base, m, strlen(m));
}
static void test_parse_img_alias_null_tagset_asserts(void) { NT_TEST_EXPECT_ASSERT(parse_img_null_tagset("<img=a:b/>")); }

/* (8m) named <color>/<font>/<fx> with a NULL tagset -> NT_ASSERT in FULL (the lookup needs a tagset).
 * The OFF hard guard (if (tagset == NULL) break; before the lookup) can't be unit-tested -- no OFF
 * ctest preset -- but is OFF-safe by construction (break BEFORE the lookup derefs ts). */
static void test_parse_color_null_tagset_asserts(void) { NT_TEST_EXPECT_ASSERT(parse_lit("<color=gold>x</color>")); }
static void test_parse_font_null_tagset_asserts(void) { NT_TEST_EXPECT_ASSERT(parse_lit("<font=mono>x</font>")); }
static void test_parse_fx_null_tagset_asserts(void) { NT_TEST_EXPECT_ASSERT(parse_lit("<fx=wave>x</fx>")); }

/* (8n) <img=alias:region/> with a NON-null tagset that lacks the alias -> NT_ASSERT (unknown alias).
 * The OFF hard guard (if (!alias_ok) return;) skips the image instead of falling back to the default
 * atlas with the wrong region -- by-construction OFF-safe, not unit-testable without an OFF preset. */
static void parse_img_no_alias(const char *m) {
    nt_ui_rich_tagset_t ts;
    nt_ui_rich_tagset_init(&ts); /* a valid tagset that registers NO atlas alias -> the alias lookup misses */
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.default_atlas.atlas = s_fx.atlas.handle;
    nt_ui_rich_parse(s_fx.ctx, &ts, &base, m, strlen(m));
}
static void test_parse_img_unknown_alias_asserts(void) { NT_TEST_EXPECT_ASSERT(parse_img_no_alias("<img=nope:region/>")); }

/* (img-attrs) markup `<img=region scale=1.8 oy=-4 valign=middle/>` produces a run BYTE-IDENTICAL to the
 * builder nt_ui_rich_image(ref, MIDDLE, -4, 1.8): same kind, same region ref, same scale/oy/valign. */
static void test_parse_img_attrs_match_builder(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.default_atlas.atlas = s_fx.atlas.handle;

    /* BUILDER: a single inline image with explicit scale/oy/valign. */
    nt_ui_rich_begin(s_fx.ctx, &base);
    nt_ui_rich_image(s_fx.ctx, nt_atlas_ref(s_fx.atlas.handle, h("heart")), NT_RICH_VALIGN_MIDDLE, -4.0F, 1.8F);
    nt_ui_rich_end(s_fx.ctx);

    const uint32_t b_runs = nt_ui_rich_test_run_count(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, b_runs, "builder: one <img> -> one run");
    const nt_atlas_region_ref_t b_ref = nt_ui_rich_test_run_image_ref(s_fx.ctx, 0);
    const nt_rich_valign_t b_valign = nt_ui_rich_test_run_image_valign(s_fx.ctx, 0);
    /* The bit-pattern of the run's scale/oy: byte-identity means the SAME float bits, so compare ints
     * (UNITY_EXCLUDE_FLOAT precludes TEST_ASSERT_EQUAL_FLOAT). */
    uint32_t b_scale_bits = 0;
    uint32_t b_oy_bits = 0;
    {
        const float bs = nt_ui_rich_test_run_image_scale(s_fx.ctx, 0);
        const float bo = nt_ui_rich_test_run_image_offset_y(s_fx.ctx, 0);
        memcpy(&b_scale_bits, &bs, sizeof bs);
        memcpy(&b_oy_bits, &bo, sizeof bo);
    }

    /* PARSER: byte-identical markup with the attr tail. */
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    const char *m = "<img=heart scale=1.8 oy=-4 valign=middle/>";
    nt_ui_rich_parse(s_fx.ctx, NULL, &base, m, strlen(m));

    const uint32_t p_runs = nt_ui_rich_test_run_count(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(b_runs, p_runs, "parser run count == builder");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(NT_RICH_ATOM_IMAGE, nt_ui_rich_test_run_kind(s_fx.ctx, 0), "parser run is an IMAGE");
    const nt_atlas_region_ref_t p_ref = nt_ui_rich_test_run_image_ref(s_fx.ctx, 0);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(b_ref.atlas.id, p_ref.atlas.id, "parser image atlas == builder");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(b_ref.region, p_ref.region, "parser image region == builder");
    uint32_t p_scale_bits = 0;
    uint32_t p_oy_bits = 0;
    {
        const float ps = nt_ui_rich_test_run_image_scale(s_fx.ctx, 0);
        const float po = nt_ui_rich_test_run_image_offset_y(s_fx.ctx, 0);
        memcpy(&p_scale_bits, &ps, sizeof ps);
        memcpy(&p_oy_bits, &po, sizeof po);
    }
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(b_scale_bits, p_scale_bits, "parser image scale bits == builder (1.8)");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(b_oy_bits, p_oy_bits, "parser image oy bits == builder (-4)");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)b_valign, (uint8_t)nt_ui_rich_test_run_image_valign(s_fx.ctx, 0), "parser image valign == builder (MIDDLE)");
}

/* (img-attrs) a bare <img=heart/> (no attr tail) keeps the historical defaults: scale=1, oy=0, MIDDLE. */
static void test_parse_img_no_attrs_defaults(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.default_atlas.atlas = s_fx.atlas.handle;
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    const char *m = "<img=heart/>";
    nt_ui_rich_parse(s_fx.ctx, NULL, &base, m, strlen(m));

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, nt_ui_rich_test_run_count(s_fx.ctx), "bare <img=heart/> -> one run");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(NT_RICH_ATOM_IMAGE, nt_ui_rich_test_run_kind(s_fx.ctx, 0), "bare <img> run is an IMAGE");
    uint32_t scale_bits = 0;
    uint32_t oy_bits = 0;
    const float one = 1.0F;
    const float zero = 0.0F;
    uint32_t one_bits = 0;
    uint32_t zero_bits = 0;
    {
        const float s = nt_ui_rich_test_run_image_scale(s_fx.ctx, 0);
        const float o = nt_ui_rich_test_run_image_offset_y(s_fx.ctx, 0);
        memcpy(&scale_bits, &s, sizeof s);
        memcpy(&oy_bits, &o, sizeof o);
        memcpy(&one_bits, &one, sizeof one);
        memcpy(&zero_bits, &zero, sizeof zero);
    }
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(one_bits, scale_bits, "bare <img> default scale == 1");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(zero_bits, oy_bits, "bare <img> default oy == 0");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)NT_RICH_VALIGN_MIDDLE, (uint8_t)nt_ui_rich_test_run_image_valign(s_fx.ctx, 0), "bare <img> default valign == MIDDLE");
}

/* (img-attrs) a malformed attr float (<img=heart scale=abc/>) -> NT_ASSERT (fail-early). In OFF the
 * rich_parse_float hard path returns a bounded 0.0F (the rich_image scale>0 assert is the dev guard);
 * neither path reads OOB. */
static void test_parse_img_bad_attr_float_asserts(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.default_atlas.atlas = s_fx.atlas.handle;
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_parse(s_fx.ctx, NULL, &base, "<img=heart scale=abc/>", 22U));
}

/* (img-attrs) an unknown valign keyword (<img=heart valign=sideways/>) -> NT_ASSERT; OFF keeps MIDDLE. */
static void test_parse_img_bad_valign_asserts(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.default_atlas.atlas = s_fx.atlas.handle;
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_parse(s_fx.ctx, NULL, &base, "<img=heart valign=sideways/>", 28U));
}

/* (img-attrs) an unknown attr key (<img=heart bogus=3/>) -> NT_ASSERT. */
static void test_parse_img_unknown_attr_key_asserts(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.default_atlas.atlas = s_fx.atlas.handle;
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_parse(s_fx.ctx, NULL, &base, "<img=heart bogus=3/>", 20U));
}

/* (10) over-deep <b> nesting -> NT_ASSERT before overflow. NOTE: each <b> pushes BOTH the parser
 * tag stack (NT_UI_RICH_PARSE_TAG_DEPTH) and the builder style stack (now PARSE_TAG_DEPTH+1); the
 * parser tag cap (the lower of the two) trips first. 40 > either cap, so the assert fires. */
static void test_parse_over_deep_style_stack_asserts(void) {
    char buf[256];
    uint32_t n = 0;
    for (uint32_t i = 0; i < 40U; i++) { /* 40 > NT_UI_RICH_PARSE_TAG_DEPTH (32) */
        buf[n++] = '<';
        buf[n++] = 'b';
        buf[n++] = '>';
    }
    buf[n] = '\0';
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_parse(s_fx.ctx, NULL, NULL, buf, n));
}

/* (10c) cap-parity regression (#1+#2): a BALANCED <b>xN + </b>xN at N == the parser tag cap must
 * parse WITHOUT a trap (no over-cap), and the trailing text must carry the BASE style -- proof the
 * style stack balanced exactly back to base (depth never desynced from the tag stack, so no pop
 * underflow / OOB in OFF). Sweeps N up to NT_UI_RICH_PARSE_TAG_DEPTH. */
static void test_parse_balanced_at_cap_stays_synced(void) {
    /* Read the REAL NT_UI_RICH_PARSE_TAG_DEPTH (private to nt_ui_rich_text.c) via the test surface so the
     * sweep always lands on the true cap. The engine _Static_assert ties NT_UI_RICH_STACK_DEPTH == this + 1,
     * so a balanced nest at this depth must NOT over-cap the style stack. */
    const uint32_t parse_tag_depth = nt_ui_rich_test_parse_tag_depth();
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.color_abgr = 0xFF112233U; /* a base tint distinguishable from any <b> push (bold doesn't change color) */
    for (uint32_t depth = 1U; depth <= parse_tag_depth; depth++) {
        char buf[512];
        uint32_t n = 0;
        for (uint32_t i = 0; i < depth; i++) { /* depth opens */
            buf[n++] = '<';
            buf[n++] = 'b';
            buf[n++] = '>';
        }
        for (uint32_t i = 0; i < depth; i++) { /* depth balanced closes */
            buf[n++] = '<';
            buf[n++] = '/';
            buf[n++] = 'b';
            buf[n++] = '>';
        }
        buf[n++] = 'x'; /* trailing text -> must carry the BASE style (stack back at base) */

        nt_mem_scratch_reset();
        s_fx.ctx->pending_rich = NULL;
        s_fx.ctx->rich_session_open = false;
        nt_ui_rich_parse(s_fx.ctx, NULL, &base, buf, n); /* no trap in FULL: balanced, never over-cap */

        const uint32_t runs = nt_ui_rich_test_run_count(s_fx.ctx);
        TEST_ASSERT_TRUE_MESSAGE(runs >= 1U, "balanced-at-cap markup produced the trailing-text run");
        /* The LAST run is the trailing "x": its composed style must equal base (variant cleared, color base). */
        const nt_ui_rich_style_t last = nt_ui_rich_test_run_style(s_fx.ctx, runs - 1U);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0U, last.variant, "trailing text after balanced <b> nesting must be NON-bold (stack back at base)");
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(0xFF112233U, last.color_abgr, "trailing text must carry the BASE color (style stack balanced back to base)");
    }
}

/* (10d) mixed-tag stack-sync: interleave style-stack PUSH tags (<color>/<scale>/<font>) with <link>
 * (which does NOT push -- it only sets the pending link field) in a nested+balanced markup. Because
 * rich_close_tag branches on top==RICH_TAG_LINK to NOT pop the style stack, the trailing text after
 * all closes must carry the BASE style -- proof the style stack and the tag stack stay in sync across
 * the link asymmetry (a desync here would leave the trailing run mid-stack: wrong color/scale/font). */
static void test_parse_mixed_tag_stack_sync(void) {
    nt_ui_rich_tagset_t ts;
    nt_ui_rich_tagset_init(&ts);
    nt_ui_rich_tagset_register_color(&ts, "accent", 0xFFAABBCCU);
    const nt_font_t hdr[4] = {{.id = 21}, {.id = 22}, {.id = 23}, {.id = 24}};
    nt_ui_rich_tagset_register_font(&ts, "hdr", hdr);

    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.color_abgr = 0xFF112233U; /* distinguishable from the accent push */
    base.scale = 1.0F;
    const nt_font_t base_face = {.id = 7};
    for (uint32_t i = 0; i < 4U; i++) {
        base.font_id[i] = base_face; /* a base family distinct from "hdr" */
    }

    /* <link> wraps the pushes; its </link> must NOT pop the style stack. Balanced, well-formed. */
    const char *m = "<color=accent><link=q1><scale=2><font=hdr>inner</font></scale></link></color>tail";
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, &ts, &base, m, strlen(m));

    const uint32_t runs = nt_ui_rich_test_run_count(s_fx.ctx);
    TEST_ASSERT_TRUE_MESSAGE(runs >= 1U, "mixed-tag markup produced the trailing-text run");
    const nt_ui_rich_style_t last = nt_ui_rich_test_run_style(s_fx.ctx, runs - 1U);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(0xFF112233U, last.color_abgr, "trailing text carries the BASE color (style stack synced past <link>)");
    const float scale_err = (last.scale > 1.0F) ? (last.scale - 1.0F) : (1.0F - last.scale);
    TEST_ASSERT_TRUE_MESSAGE(scale_err < 1e-3F, "trailing text carries the BASE scale (no leaked <scale> push)");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(7U, last.font_id[0].id, "trailing text carries the BASE font (no leaked <font> push)");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0U, last.variant, "trailing text variant back at base");
}

/* (10e) named-tag MISS (#1): a tagset that knows OTHER names but not the requested one. An unknown
 * <font=typo>/<color=xyz>/<fx=bad> trips the lookup-miss NT_ASSERT in rich_open_tag (the DEBUG dev
 * signal -- kept). These are the no-push branches: in OFF the hard guard skips the tag WITHOUT a
 * style push, and the matching close (now pop-iff-pushed) must NOT pop the enclosing style. The OFF
 * graceful skip is verified by code review (the suite runs asserts-ON, exercising the trap side). */
static void parse_named_miss(const char *m) {
    nt_ui_rich_tagset_t ts;
    nt_ui_rich_tagset_init(&ts); /* valid tagset; registers names DISTINCT from the misses below */
    nt_ui_rich_tagset_register_color(&ts, "accent", 0xFFAABBCCU);
    const nt_font_t hdr[4] = {{.id = 21}, {.id = 22}, {.id = 23}, {.id = 24}};
    nt_ui_rich_tagset_register_font(&ts, "hdr", hdr);
    nt_ui_rich_tagset_register_effect(&ts, "wave", NT_UI_RICH_FX_ID_WAVE);
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    nt_ui_rich_parse(s_fx.ctx, &ts, &base, m, strlen(m));
}
static void test_parse_unknown_named_tags_assert(void) {
    NT_TEST_EXPECT_ASSERT(parse_named_miss("<font=typo>x</font>"));  /* lookup_font miss -> no push */
    NT_TEST_EXPECT_ASSERT(parse_named_miss("<color=xyz>x</color>")); /* lookup_color miss -> no push */
    NT_TEST_EXPECT_ASSERT(parse_named_miss("<fx=bad>x</fx>"));       /* lookup_effect miss -> no push */
}

/* (10f) pop-iff-pushed correctness (#1): a WELL-FORMED <b><i>x</i>y</b> still composes byte-identically
 * -- every real push pops, the trailing 'y' is bold-not-italic, the final 'z' is back at base. Proves
 * the pushed-flag refactor did not break well-formed nesting (no over-pop, no leaked push). */
static void test_parse_wellformed_nesting_pops_correctly(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.color_abgr = 0xFF112233U;
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    /* x: bold+italic ; y: bold (italic popped) ; z: base (bold popped). */
    const char *m = "<b><i>x</i>y</b>z";
    nt_ui_rich_parse(s_fx.ctx, NULL, &base, m, strlen(m));

    const uint32_t runs = nt_ui_rich_test_run_count(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3U, runs, "<b><i>x</i>y</b>z -> three distinct-style runs");
    const uint8_t bold_italic = (uint8_t)(NT_UI_RICH_VARIANT_BOLD | NT_UI_RICH_VARIANT_ITALIC);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(bold_italic, nt_ui_rich_test_run_style(s_fx.ctx, 0).variant, "x is bold+italic");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)NT_UI_RICH_VARIANT_BOLD, nt_ui_rich_test_run_style(s_fx.ctx, 1).variant, "y is bold only (</i> popped italic)");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0U, nt_ui_rich_test_run_style(s_fx.ctx, 2).variant, "z is base (</b> popped bold)");
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(0xFF112233U, nt_ui_rich_test_run_style(s_fx.ctx, 2).color_abgr, "z carries the BASE color (stack balanced back to base)");
}

/* (16b) <scale=0> / <scale=-1>: the push_scale>0 assert traps in FULL. In OFF the hard clamp falls
 * back to identity (no <=0 font size into nt_font_measure_n). */
static void test_parse_scale_nonpositive_asserts(void) {
    NT_TEST_EXPECT_ASSERT(parse_lit("<scale=0>x</scale>"));
    NT_TEST_EXPECT_ASSERT(parse_lit("<scale=-1>x</scale>"));
}

/* (10b) nested <link> is rejected (HTML's no-nested-anchor rule): pending_link is a single scalar,
 * so an inner </link> would zero the outer's id and silently drop the enclosing link. A SINGLE link
 * around text parses fine; a second link opened inside the first traps in rich_open_tag. */
static void test_parse_nested_link_asserts(void) {
    /* One link parses fine. */
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, NULL, NULL, "<link=a>hi</link>", 17U);
    TEST_ASSERT_TRUE_MESSAGE(nt_ui_rich_test_run_count(s_fx.ctx) == 1U, "single <link> around text -> one run");

    /* A nested <link> inside an open <link> -> NT_ASSERT (loud, not silently wrong). */
    NT_TEST_EXPECT_ASSERT(parse_lit("<link=a><link=b>hi</link></link>"));
}

/* ---- bounded-scan robustness ---- */

/* (11) a pathological non-terminating '<' string asserts on the unterminated tag and does NOT
 * loop forever or read past `len` -- the test completing proves the scan is bounded. */
static void test_parse_non_terminating_bounded(void) {
    char buf[64];
    memset(buf, '<', sizeof buf); /* "<<<<...<" -- no '>' anywhere */
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_parse(s_fx.ctx, NULL, NULL, buf, sizeof buf));
}

/* (12) the scan respects `len`: a '<' at the very end with bytes BEYOND len must not be read.
 * We pass a len that stops before any '>' so the parser asserts on the bounded buffer, never
 * scanning into the trailing (out-of-range) bytes. */
static void test_parse_len_bounded(void) {
    /* The full buffer is well-formed, but we lie about the length: only "<b" is in-bounds. */
    const char *full = "<b>ok</b>";
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_parse(s_fx.ctx, NULL, NULL, full, 2U)); /* only "<b" visible -> unterminated */
}

/* (13) a literal-< escape (\<) emits a real '<' in the text, not a tag start. */
static void test_parse_escape_literal_lt(void) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    const char *m = "a\\<b"; /* a, literal '<', b */
    nt_ui_rich_parse(s_fx.ctx, NULL, NULL, m, strlen(m));
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_rich_test_run_count(s_fx.ctx));
    TEST_ASSERT_EQUAL_UINT32(3U, nt_ui_rich_test_run_text_len(s_fx.ctx, 0));
    const char *t = nt_ui_rich_test_run_text(s_fx.ctx, 0);
    TEST_ASSERT_EQUAL_MEMORY("a<b", t, 3);
}

/* (14) invalid UTF-8 in a literal text segment -> NT_ASSERT. */
static void test_parse_invalid_utf8_asserts(void) {
    const char bad[] = {'h', 'i', (char)0xFF, 'x'}; /* 0xFF is never valid UTF-8 */
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_parse(s_fx.ctx, NULL, NULL, bad, sizeof bad));
}

/* (14b) a buffer-full truncation that lands MID multibyte sequence must not sever a codepoint:
 * rich_parse_append_byte trims the partial trailing UTF-8 so rich_flush_text's validator never sees
 * a lone lead/continuation byte (which would TRAP in FULL on otherwise-valid input). Fill the text
 * buffer so a 2-byte 'é' (0xC3 0xA9) straddles the cap: the lead lands at the last slot, the
 * continuation is dropped, and the trim rolls the lone lead back -> kept text is valid UTF-8. */
static void test_parse_utf8_truncation_codepoint_aware(void) {
    static char buf[NT_UI_RICH_MAX_TEXT_BYTES + 64];
    uint32_t n = 0;
    for (uint32_t i = 0; i < NT_UI_RICH_MAX_TEXT_BYTES - 1U; i++) {
        buf[n++] = 'a'; /* fills text_len to cap-1 */
    }
    buf[n++] = (char)0xC3; /* 2-byte lead -> lands at the last slot (text_len -> cap, buffer now full) */
    buf[n++] = (char)0xA9; /* continuation -> dropped; the trim must roll the lone lead back too */
    buf[n++] = (char)0xC3; /* a SECOND straddling sequence past the cap -> entirely dropped */
    buf[n++] = (char)0xA9;
    buf[n++] = 'z'; /* trailing ASCII past the cap -> dropped */

    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    /* No NT_TEST_EXPECT_ASSERT: a trap here is a FAILURE (the whole point is no trap on valid input). */
    nt_ui_rich_parse(s_fx.ctx, NULL, NULL, buf, n);

    const uint32_t runs = nt_ui_rich_test_run_count(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, runs, "one text run from the truncated literal");
    const uint32_t kept = nt_ui_rich_test_run_text_len(s_fx.ctx, 0);
    /* Kept text stops at the last COMPLETE codepoint: the cap-1 'a' bytes, lone lead trimmed away. */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(NT_UI_RICH_MAX_TEXT_BYTES - 1U, kept, "partial trailing UTF-8 lead trimmed (no severed codepoint)");
    /* Re-validate the kept bytes are well-formed UTF-8 (no lone lead/continuation survived). */
    const char *t = nt_ui_rich_test_run_text(s_fx.ctx, 0);
    uint32_t state = NT_UTF8_ACCEPT;
    uint32_t cp = 0;
    for (uint32_t i = 0; i < kept; i++) {
        nt_utf8_decode(&state, &cp, (uint8_t)t[i]);
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(NT_UTF8_ACCEPT, state, "kept truncated text is valid UTF-8 (ends on a codepoint boundary)");
}

/* (14c) the SAME cap-straddle trim, but the truncated trailing sequence is 3-byte (0xE2 0x80 0x99 ’)
 * and 4-byte (0xF0 0x9F 0x98 0x80 😀). Exercises rich_utf8_seq_len's 3/4-byte branches and
 * rich_utf8_complete_prefix's multi-continuation drop: a lead at the cap whose continuations spill
 * past the buffer must roll the WHOLE partial sequence back, so the kept prefix decodes clean. */
static void utf8_truncation_lead_case(const char *seq, uint32_t seq_len) {
    static char buf[NT_UI_RICH_MAX_TEXT_BYTES + 64];
    uint32_t n = 0;
    for (uint32_t i = 0; i < NT_UI_RICH_MAX_TEXT_BYTES - 1U; i++) {
        buf[n++] = 'a'; /* fills text_len to cap-1; the lead below lands at the last slot */
    }
    for (uint32_t i = 0; i < seq_len; i++) {
        buf[n++] = seq[i]; /* lead at cap, continuations spill past -> whole sequence must roll back */
    }
    buf[n++] = 'z'; /* trailing ASCII past the cap -> dropped */

    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_parse(s_fx.ctx, NULL, NULL, buf, n); /* a trap here is a FAILURE (valid input) */

    const uint32_t runs = nt_ui_rich_test_run_count(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1U, runs, "one text run from the truncated literal");
    const uint32_t kept = nt_ui_rich_test_run_text_len(s_fx.ctx, 0);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(NT_UI_RICH_MAX_TEXT_BYTES - 1U, kept, "partial multibyte lead trimmed whole (no severed codepoint)");
    const char *t = nt_ui_rich_test_run_text(s_fx.ctx, 0);
    uint32_t state = NT_UTF8_ACCEPT;
    uint32_t cp = 0;
    for (uint32_t i = 0; i < kept; i++) {
        nt_utf8_decode(&state, &cp, (uint8_t)t[i]);
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(NT_UTF8_ACCEPT, state, "kept truncated text is valid UTF-8 (ends on a codepoint boundary)");
}

static void test_parse_utf8_truncation_3byte_lead(void) {
    const char seq[3] = {(char)0xE2, (char)0x80, (char)0x99}; /* ’ U+2019 */
    utf8_truncation_lead_case(seq, 3U);
}

static void test_parse_utf8_truncation_4byte_lead(void) {
    const char seq[4] = {(char)0xF0, (char)0x9F, (char)0x98, (char)0x80}; /* 😀 U+1F600 */
    utf8_truncation_lead_case(seq, 4U);
}

/* ---- fail-early domain-input asserts (rich API) ---- */

/* Open a fresh rich builder session (no frame needed; the builder is scratch-only). */
static void rich_session_begin(void) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    nt_ui_rich_begin(s_fx.ctx, &base);
}

/* (15) <img=.../> with a NULL base (no default_atlas source) -> NT_ASSERT in the parser. */
static void test_parse_img_null_base_asserts(void) {
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    const char *m = "<img=icon/>";
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_parse(s_fx.ctx, NULL, NULL, m, strlen(m)));
}

/* (16) push_scale with a non-finite-or-non-positive multiplier -> NT_ASSERT (negative font size guard). */
static void test_push_scale_bad_mult_asserts(void) {
    rich_session_begin();
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_push_scale(s_fx.ctx, 0.0F));
    rich_session_begin();
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_push_scale(s_fx.ctx, -1.5F));
}

/* (17) rich_image with a non-positive scale -> NT_ASSERT (negative Clay fixed-size guard). */
static void test_rich_image_bad_scale_asserts(void) {
    rich_session_begin();
    const nt_atlas_region_ref_t ref = nt_atlas_ref(s_fx.atlas.handle, 0xDEADBEEFULL);
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_image(s_fx.ctx, ref, NT_RICH_VALIGN_MIDDLE, 0.0F, 0.0F));
    rich_session_begin();
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_image(s_fx.ctx, ref, NT_RICH_VALIGN_MIDDLE, 0.0F, -2.0F));
}

/* (18) the public widget + markup entries with id==0 -> NT_ASSERT (id drives bbox/link origin). */
static void test_rich_text_zero_id_asserts(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;

    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    nt_ui_rich_begin(s_fx.ctx, &base);
    nt_ui_rich_text_n(s_fx.ctx, "x", 1);
    nt_ui_rich_end(s_fx.ctx);
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_text(s_fx.ctx, 0U, NULL, &base, 400.0F, NT_RICH_ALIGN_LEFT, 0.0F, NULL));
    nt_ui_end(s_fx.ctx);

    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    NT_TEST_EXPECT_ASSERT(nt_ui_rich_text_markup(s_fx.ctx, 0U, NULL, NULL, &base, "x", 1U, 400.0F, NT_RICH_ALIGN_LEFT, 0.0F, NULL));
    nt_ui_end(s_fx.ctx);
}

/* (layer) parser <layer=N> produces a run-list BYTE-IDENTICAL to the builder push_layer(N): same run
 * count, same per-run kind, and the same composed style.layer on every run. A <layer=5> wrap around mixed
 * text must mint the same intervening runs the builder does (the parser drives the SAME builder). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity) -- two authoring fronts built then compared run-by-run
static void test_parse_layer_matches_builder(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.color_abgr = 0xFF112233U;
    const nt_font_t base_face = {.id = 7};
    for (uint32_t i = 0; i < 4U; i++) {
        base.font_id[i] = base_face;
    }

    /* BUILDER: "a " then <layer=5>"b "</layer> then "c". */
    nt_ui_rich_begin(s_fx.ctx, &base);
    nt_ui_rich_text_n(s_fx.ctx, "a ", 2);
    nt_ui_rich_push_layer(s_fx.ctx, 5U);
    nt_ui_rich_text_n(s_fx.ctx, "b ", 2);
    nt_ui_rich_pop(s_fx.ctx);
    nt_ui_rich_text_n(s_fx.ctx, "c", 1);
    nt_ui_rich_end(s_fx.ctx);

    const uint32_t b_runs = nt_ui_rich_test_run_count(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3U, b_runs, "builder: a | <layer=5>b | c -> 3 runs");
    uint8_t b_layer[8];
    nt_rich_atom_kind_t b_kind[8];
    for (uint32_t i = 0; i < b_runs; i++) {
        b_layer[i] = nt_ui_rich_test_run_style(s_fx.ctx, i).layer;
        b_kind[i] = nt_ui_rich_test_run_kind(s_fx.ctx, i);
    }
    TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)NT_UI_RICH_LAYER_AUTO, b_layer[0], "run 0 outside <layer> is AUTO");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(5U, b_layer[1], "run 1 inside <layer=5> is 5");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE((uint8_t)NT_UI_RICH_LAYER_AUTO, b_layer[2], "run 2 after </layer> is AUTO again");

    /* PARSER: byte-identical markup of the same builder sequence. */
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
    s_fx.ctx->rich_session_open = false;
    const char *m = "a <layer=5>b </layer>c";
    nt_ui_rich_parse(s_fx.ctx, NULL, &base, m, strlen(m));

    const uint32_t p_runs = nt_ui_rich_test_run_count(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(b_runs, p_runs, "parser run count == builder run count");
    for (uint32_t i = 0; i < p_runs; i++) {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(b_kind[i], nt_ui_rich_test_run_kind(s_fx.ctx, i), "parser run kind == builder");
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(b_layer[i], nt_ui_rich_test_run_style(s_fx.ctx, i).layer, "parser run layer == builder (byte-identity)");
    }
}

/* (layer) <layer=N> hard guards survive: >254 traps in FULL; the empty/non-numeric forms trap too. */
static void test_parse_layer_out_of_range_asserts(void) {
    NT_TEST_EXPECT_ASSERT(parse_lit("<layer=255>x</layer>")); /* 255 is the AUTO sentinel, not a valid explicit layer */
    NT_TEST_EXPECT_ASSERT(parse_lit("<layer=300>x</layer>")); /* > 254 */
    NT_TEST_EXPECT_ASSERT(parse_lit("<layer=>x</layer>"));    /* empty value */
    NT_TEST_EXPECT_ASSERT(parse_lit("<layer=ab>x</layer>"));  /* non-numeric */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_tagset_font_register_lookup);
    RUN_TEST(test_tagset_color_atlas_effect);
    RUN_TEST(test_tagset_custom_effect_fn);
    RUN_TEST(test_tagset_stock_effect_coexists);
    RUN_TEST(test_tagset_override_in_place);
    RUN_TEST(test_tagset_reset);
    RUN_TEST(test_tagset_empty_name_asserts);
    RUN_TEST(test_parse_unclosed_tag_asserts);
    RUN_TEST(test_parse_mismatched_close_asserts);
    RUN_TEST(test_parse_unknown_tag_asserts);
    RUN_TEST(test_parse_bad_hex_asserts);
    RUN_TEST(test_parse_orphan_close_asserts);
    RUN_TEST(test_parse_mismatched_link_close_asserts);
    RUN_TEST(test_parse_empty_scale_value_asserts);
    RUN_TEST(test_parse_fx_params_ok);
    RUN_TEST(test_parse_fx_bad_float_asserts);
    RUN_TEST(test_parse_fx_unknown_key_asserts);
    RUN_TEST(test_parse_fx_no_equals_asserts);
    RUN_TEST(test_parse_fx_params_on_custom_asserts);
    RUN_TEST(test_parse_fx_empty_value_asserts);
    RUN_TEST(test_parse_obj_self_close_emits_object_run);
    RUN_TEST(test_parse_obj_unknown_asserts);
    RUN_TEST(test_parse_obj_empty_name_asserts);
    RUN_TEST(test_parse_obj_null_tagset_asserts);
    RUN_TEST(test_parse_img_alias_null_tagset_asserts);
    RUN_TEST(test_parse_color_null_tagset_asserts);
    RUN_TEST(test_parse_font_null_tagset_asserts);
    RUN_TEST(test_parse_fx_null_tagset_asserts);
    RUN_TEST(test_parse_img_unknown_alias_asserts);
    RUN_TEST(test_parse_img_attrs_match_builder);
    RUN_TEST(test_parse_img_no_attrs_defaults);
    RUN_TEST(test_parse_img_bad_attr_float_asserts);
    RUN_TEST(test_parse_img_bad_valign_asserts);
    RUN_TEST(test_parse_img_unknown_attr_key_asserts);
    RUN_TEST(test_parse_over_deep_style_stack_asserts);
    RUN_TEST(test_parse_balanced_at_cap_stays_synced);
    RUN_TEST(test_parse_mixed_tag_stack_sync);
    RUN_TEST(test_parse_unknown_named_tags_assert);
    RUN_TEST(test_parse_wellformed_nesting_pops_correctly);
    RUN_TEST(test_parse_scale_nonpositive_asserts);
    RUN_TEST(test_parse_nested_link_asserts);
    RUN_TEST(test_parse_non_terminating_bounded);
    RUN_TEST(test_parse_len_bounded);
    RUN_TEST(test_parse_escape_literal_lt);
    RUN_TEST(test_parse_invalid_utf8_asserts);
    RUN_TEST(test_parse_utf8_truncation_codepoint_aware);
    RUN_TEST(test_parse_utf8_truncation_3byte_lead);
    RUN_TEST(test_parse_utf8_truncation_4byte_lead);
    RUN_TEST(test_parse_img_null_base_asserts);
    RUN_TEST(test_push_scale_bad_mult_asserts);
    RUN_TEST(test_rich_image_bad_scale_asserts);
    RUN_TEST(test_rich_text_zero_id_asserts);
    RUN_TEST(test_parse_layer_matches_builder);
    RUN_TEST(test_parse_layer_out_of_range_asserts);
    return UNITY_END();
}
