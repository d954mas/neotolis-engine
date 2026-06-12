/* Progress bar widget tests (59-04 GREEN).
 *
 * Read-only track + fill: fill ratio = value (clamped [0,1]); STRETCH (slice9,
 * shared slider fill-emit path → plain IMAGE) vs CROP (UV-reveal via a Clay clip →
 * SCISSOR commands); all 4 fill directions (LTR / RTL / bottom-up / top-down) place
 * the fill on the correct edge; value_t eases the fill toward the target; exactly
 * ONE nt_ui_anim call; no interaction. UNITY_EXCLUDE_FLOAT: compare via eps. */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "input/nt_input.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_progress.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

/* Non-origin, ASYMMETRIC track (W != H) pins so a stray axis swap or sign flip on a
 * direction is visible (AGENTS.md asymmetric-data rule). */
#define PG_X 100.0F
#define PG_Y 200.0F
#define PG_W 200.0F
#define PG_H 40.0F

static nt_ui_progress_style_t s_style;

/* Unity float macros are excluded in this build (UNITY_EXCLUDE_FLOAT). */
static bool float_near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

static void init_style(nt_ui_fill_mode_t mode, nt_ui_fill_direction_t dir, float value_speed) {
    const nt_atlas_region_ref_t art = nt_atlas_ref_idx(s_fx.atlas.handle, 0, s_fx.atlas.white_region_idx);
    s_style = nt_ui_progress_style_defaults();
    s_style.track = art;
    s_style.fill = art;
    s_style.track_w = PG_W;
    s_style.track_h = PG_H;
    s_style.fill_mode = mode;
    s_style.fill_direction = dir;
    s_style.value_speed = value_speed; /* 0 = instant snap -> deterministic ratio per frame */
}

void setUp(void) {
    nt_test_assert_install();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
    init_style(NT_UI_FILL_STRETCH, NT_UI_FILL_LTR, 0.0F);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

static const Clay_ElementDeclaration s_track_decl = {
    .layout = {.sizing = {CLAY_SIZING_FIXED(PG_W), CLAY_SIZING_FIXED(PG_H)}},
};

static void progress_frame_dt(float value, float dt) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, dt, &mouse, 1);
    CLAY({.id = CLAY_ID("root"), .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = PG_X, .y = PG_Y}}}) {
        nt_ui_progress(s_fx.ctx, NULL, 0, nt_ui_id("pg"), value, &s_style, &s_track_decl);
    }
    nt_ui_end(s_fx.ctx);
}

/* value_speed-0 cases snap regardless of dt; dt=0 keeps them deterministic. */
static void progress_frame(float value) { progress_frame_dt(value, 0.0F); }

static uint32_t count_cmd_of_type(Clay_RenderCommandType type) {
    uint32_t n = 0;
    for (int32_t i = 0; i < s_fx.ctx->frozen_cmds.length; ++i) {
        if (s_fx.ctx->frozen_cmds.internalArray[i].commandType == type) {
            ++n;
        }
    }
    return n;
}

/* Bounding box of the first IMAGE render command (the fill rect in STRETCH; the
 * full-size revealed image in CROP). found=false if none emitted. */
typedef struct {
    float x, y, w, h;
    bool found;
} probe_bbox_t;

static probe_bbox_t first_image_bbox(void) {
    /* The track container is the FIRST IMAGE (track art, full-track-sized). The fill is
     * the SECOND IMAGE (a child) — at ratio 1.0 it is also full-sized, so skip by ORDER
     * (the first IMAGE), not by size. */
    probe_bbox_t out = {0};
    bool skipped_track = false;
    for (int32_t i = 0; i < s_fx.ctx->frozen_cmds.length; ++i) {
        const Clay_RenderCommand *c = &s_fx.ctx->frozen_cmds.internalArray[i];
        if (c->commandType != CLAY_RENDER_COMMAND_TYPE_IMAGE) {
            continue;
        }
        if (!skipped_track) {
            skipped_track = true; /* the track background image */
            continue;
        }
        const Clay_BoundingBox bb = c->boundingBox;
        out = (probe_bbox_t){.x = bb.x, .y = bb.y, .w = bb.width, .h = bb.height, .found = true};
        return out;
    }
    return out;
}

/* Bounding box of the first SCISSOR_START command (the CROP reveal window). */
static probe_bbox_t first_scissor_bbox(void) {
    probe_bbox_t out = {0};
    for (int32_t i = 0; i < s_fx.ctx->frozen_cmds.length; ++i) {
        const Clay_RenderCommand *c = &s_fx.ctx->frozen_cmds.internalArray[i];
        if (c->commandType != CLAY_RENDER_COMMAND_TYPE_SCISSOR_START) {
            continue;
        }
        const Clay_BoundingBox bb = c->boundingBox;
        out = (probe_bbox_t){.x = bb.x, .y = bb.y, .w = bb.width, .h = bb.height, .found = true};
        return out;
    }
    return out;
}

/* ---- Test 1: STRETCH fill width = ratio * track_w (value 0 → empty, 0.5 → half, 1 → full). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_stretch_fill_ratio(void) {
    init_style(NT_UI_FILL_STRETCH, NT_UI_FILL_LTR, 0.0F);

    /* value 0 → no fill emitted (ratio 0 → zero-size, helper skips). */
    progress_frame(0.0F);
    probe_bbox_t f0 = first_image_bbox();
    TEST_ASSERT_FALSE(f0.found); /* no fill child for an empty bar */

    /* value 0.5 → half-width fill. */
    progress_frame(0.5F);
    probe_bbox_t f5 = first_image_bbox();
    TEST_ASSERT_TRUE(f5.found);
    TEST_ASSERT_TRUE(float_near(f5.w, 0.5F * PG_W, 1.0F));
    TEST_ASSERT_TRUE(float_near(f5.h, PG_H, 1.0F)); /* full height (horizontal fill) */

    /* value 1 → full-width fill. */
    progress_frame(1.0F);
    probe_bbox_t f1 = first_image_bbox();
    TEST_ASSERT_TRUE(f1.found);
    TEST_ASSERT_TRUE(float_near(f1.w, PG_W, 1.0F));
}

/* ---- Test 2: value clamps to [0,1] — over/under range behave as 1 / 0. ---- */
static void test_value_clamps(void) {
    init_style(NT_UI_FILL_STRETCH, NT_UI_FILL_LTR, 0.0F);
    progress_frame(5.0F); /* over → full */
    probe_bbox_t fhi = first_image_bbox();
    TEST_ASSERT_TRUE(fhi.found);
    TEST_ASSERT_TRUE(float_near(fhi.w, PG_W, 1.0F));

    progress_frame(-3.0F); /* under → empty */
    probe_bbox_t flo = first_image_bbox();
    TEST_ASSERT_FALSE(flo.found);
}

/* ---- Test 3: STRETCH vs CROP take distinct emit paths — CROP wraps a scissor/clip,
 *      STRETCH does not. ---- */
static void test_stretch_vs_crop_command_shape(void) {
    init_style(NT_UI_FILL_STRETCH, NT_UI_FILL_LTR, 0.0F);
    progress_frame(0.5F);
    const uint32_t stretch_scissor = count_cmd_of_type(CLAY_RENDER_COMMAND_TYPE_SCISSOR_START);

    init_style(NT_UI_FILL_CROP, NT_UI_FILL_LTR, 0.0F);
    progress_frame(0.5F);
    const uint32_t crop_scissor = count_cmd_of_type(CLAY_RENDER_COMMAND_TYPE_SCISSOR_START);

    /* CROP reveals via a Clay clip (>=1 scissor); STRETCH has none of its own. */
    TEST_ASSERT_TRUE(crop_scissor > stretch_scissor);
    TEST_ASSERT_EQUAL_UINT32(0U, stretch_scissor);

    /* CROP reveal window width = ratio * track_w. */
    probe_bbox_t sc = first_scissor_bbox();
    TEST_ASSERT_TRUE(sc.found);
    TEST_ASSERT_TRUE(float_near(sc.w, 0.5F * PG_W, 1.0F));
}

/* ---- Test 4: all 4 directions place the fill on the correct edge (ASYMMETRIC track +
 *      ratio 0.25 so each direction lands in a clearly different position). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_directions_place_fill_edge(void) {
    const float r = 0.25F;

    /* LTR: anchored at the LEFT edge, quarter width. */
    init_style(NT_UI_FILL_STRETCH, NT_UI_FILL_LTR, 0.0F);
    progress_frame(r);
    probe_bbox_t ltr = first_image_bbox();
    TEST_ASSERT_TRUE(ltr.found);
    TEST_ASSERT_TRUE(float_near(ltr.x, PG_X, 1.0F));     /* left edge */
    TEST_ASSERT_TRUE(float_near(ltr.w, r * PG_W, 1.0F)); /* quarter width */
    TEST_ASSERT_TRUE(float_near(ltr.h, PG_H, 1.0F));     /* full height */

    /* RTL: anchored at the RIGHT edge — its right side = track right; left = right - r*W. */
    init_style(NT_UI_FILL_STRETCH, NT_UI_FILL_RTL, 0.0F);
    progress_frame(r);
    probe_bbox_t rtl = first_image_bbox();
    TEST_ASSERT_TRUE(rtl.found);
    TEST_ASSERT_TRUE(float_near(rtl.x + rtl.w, PG_X + PG_W, 1.0F)); /* right edge */
    TEST_ASSERT_TRUE(float_near(rtl.w, r * PG_W, 1.0F));

    /* BOTTOM_UP: anchored at the BOTTOM edge — quarter height, full width. */
    init_style(NT_UI_FILL_STRETCH, NT_UI_FILL_BOTTOM_UP, 0.0F);
    progress_frame(r);
    probe_bbox_t bu = first_image_bbox();
    TEST_ASSERT_TRUE(bu.found);
    TEST_ASSERT_TRUE(float_near(bu.y + bu.h, PG_Y + PG_H, 1.0F)); /* bottom edge */
    TEST_ASSERT_TRUE(float_near(bu.h, r * PG_H, 1.0F));           /* quarter height */
    TEST_ASSERT_TRUE(float_near(bu.w, PG_W, 1.0F));               /* full width */

    /* TOP_DOWN: anchored at the TOP edge — quarter height from the top. */
    init_style(NT_UI_FILL_STRETCH, NT_UI_FILL_TOP_DOWN, 0.0F);
    progress_frame(r);
    probe_bbox_t td = first_image_bbox();
    TEST_ASSERT_TRUE(td.found);
    TEST_ASSERT_TRUE(float_near(td.y, PG_Y, 1.0F)); /* top edge */
    TEST_ASSERT_TRUE(float_near(td.h, r * PG_H, 1.0F));
}

/* ---- Test 4b: CROP mode — the revealed (clip/scissor) window hugs the correct edge of
 *      the track for all 4 directions (ratio 0.3, ASYMMETRIC track). The wrapper anchors
 *      the clip; without it the clip sat top-left and revealed the wrong end. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_crop_directions_reveal_edge(void) {
    const float r = 0.3F;

    /* LTR: clip hugs the LEFT edge, quarter-ish width, full height. */
    init_style(NT_UI_FILL_CROP, NT_UI_FILL_LTR, 0.0F);
    progress_frame(r);
    probe_bbox_t ltr = first_scissor_bbox();
    TEST_ASSERT_TRUE(ltr.found);
    TEST_ASSERT_TRUE(float_near(ltr.x, PG_X, 1.0F));     /* left edge */
    TEST_ASSERT_TRUE(float_near(ltr.w, r * PG_W, 1.0F)); /* revealed width */
    TEST_ASSERT_TRUE(float_near(ltr.h, PG_H, 1.0F));     /* full height */

    /* RTL: clip hugs the RIGHT edge. */
    init_style(NT_UI_FILL_CROP, NT_UI_FILL_RTL, 0.0F);
    progress_frame(r);
    probe_bbox_t rtl = first_scissor_bbox();
    TEST_ASSERT_TRUE(rtl.found);
    TEST_ASSERT_TRUE(float_near(rtl.x + rtl.w, PG_X + PG_W, 1.0F)); /* right edge */
    TEST_ASSERT_TRUE(float_near(rtl.w, r * PG_W, 1.0F));

    /* BOTTOM_UP: clip hugs the BOTTOM edge, quarter-ish height, full width. */
    init_style(NT_UI_FILL_CROP, NT_UI_FILL_BOTTOM_UP, 0.0F);
    progress_frame(r);
    probe_bbox_t bu = first_scissor_bbox();
    TEST_ASSERT_TRUE(bu.found);
    TEST_ASSERT_TRUE(float_near(bu.y + bu.h, PG_Y + PG_H, 1.0F)); /* bottom edge */
    TEST_ASSERT_TRUE(float_near(bu.h, r * PG_H, 1.0F));           /* revealed height */
    TEST_ASSERT_TRUE(float_near(bu.w, PG_W, 1.0F));               /* full width */

    /* TOP_DOWN: clip hugs the TOP edge. */
    init_style(NT_UI_FILL_CROP, NT_UI_FILL_TOP_DOWN, 0.0F);
    progress_frame(r);
    probe_bbox_t td = first_scissor_bbox();
    TEST_ASSERT_TRUE(td.found);
    TEST_ASSERT_TRUE(float_near(td.y, PG_Y, 1.0F)); /* top edge */
    TEST_ASSERT_TRUE(float_near(td.h, r * PG_H, 1.0F));
    TEST_ASSERT_TRUE(float_near(td.w, PG_W, 1.0F));
}

/* ---- Test 5: value_t eases the fill monotonically toward the target over frames
 *      (value_speed > 0 → the fill grows toward, never overshoots, value). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_value_ease_monotonic(void) {
    init_style(NT_UI_FILL_STRETCH, NT_UI_FILL_LTR, 8.0F); /* eased */
    const float dt = 1.0F / 60.0F;

    /* Settle at 0 first. */
    for (int i = 0; i < 30; ++i) {
        progress_frame_dt(0.0F, dt);
    }
    /* Now drive toward 1.0 and watch the fill width climb monotonically toward PG_W. */
    float prev_w = 0.0F;
    bool grew = false;
    for (int i = 0; i < 120; ++i) {
        progress_frame_dt(1.0F, dt);
        probe_bbox_t f = first_image_bbox();
        const float w = f.found ? f.w : 0.0F;
        TEST_ASSERT_TRUE(w <= PG_W + 1.0F);   /* never overshoots the track */
        TEST_ASSERT_TRUE(w >= prev_w - 1.0F); /* monotonic up (eps for sub-px) */
        if (w > prev_w + 0.5F) {
            grew = true;
        }
        prev_w = w;
    }
    TEST_ASSERT_TRUE(grew);                           /* it actually moved */
    TEST_ASSERT_TRUE(float_near(prev_w, PG_W, 2.0F)); /* converged to full */
}

/* ---- Test 6: value_speed 0 = instant snap (fill jumps to the value the same frame). ---- */
static void test_instant_when_speed_zero(void) {
    init_style(NT_UI_FILL_STRETCH, NT_UI_FILL_LTR, 0.0F);
    progress_frame(0.0F);  /* warm */
    progress_frame(0.75F); /* jump */
    probe_bbox_t f = first_image_bbox();
    TEST_ASSERT_TRUE(f.found);
    TEST_ASSERT_TRUE(float_near(f.w, 0.75F * PG_W, 1.0F)); /* no lag */
}

/* ---- Test 7: style defaults are a valid baseline (STRETCH/LTR, opacity 1, sized). ---- */
static void test_style_defaults_valid_baseline(void) {
    nt_ui_progress_style_t s = nt_ui_progress_style_defaults();
    TEST_ASSERT_TRUE(s.track_w > 0.0F && s.track_h > 0.0F);
    TEST_ASSERT_EQUAL_INT(NT_UI_FILL_STRETCH, s.fill_mode);
    TEST_ASSERT_EQUAL_INT(NT_UI_FILL_LTR, s.fill_direction);
    TEST_ASSERT_TRUE(float_near(s.opacity, 1.0F, 0.001F));
    TEST_ASSERT_TRUE(s.value_speed >= 0.0F);
}

/* ---- Death tests (NT_ASSERT_FULL only) ---- */
#if NT_ASSERT_MODE == NT_ASSERT_FULL

/* track_w == 0 -> assert. */
static void test_assert_track_w_zero(void) {
    nt_ui_progress_style_t bad = s_style;
    bad.track_w = 0.0F;
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) { NT_TEST_EXPECT_ASSERT(nt_ui_progress(s_fx.ctx, NULL, 0, nt_ui_id("pg"), 0.5F, &bad, &s_track_decl)); }
    nt_ui_end(s_fx.ctx);
}

/* value not finite -> assert. */
static void test_assert_value_not_finite(void) {
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) { NT_TEST_EXPECT_ASSERT(nt_ui_progress(s_fx.ctx, NULL, 0, nt_ui_id("pg"), INFINITY, &s_style, &s_track_decl)); }
    nt_ui_end(s_fx.ctx);
}

/* data->flags must NOT set HAS_TRANSFORM/HAS_OPACITY -- the widget owns those. */
static void test_assert_data_flags_transform(void) {
    nt_ui_transform_t t = nt_ui_transform_defaults();
    nt_pointer_t mouse = {0};
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 0.0F, &mouse, 1);
    CLAY({.id = CLAY_ID("root")}) {
        const nt_ui_element_data_t *bad = NT_UI_DATA_XFORM(0U, &t, 1.0F); /* sets HAS_TRANSFORM|HAS_OPACITY */
        NT_TEST_EXPECT_ASSERT(nt_ui_progress(s_fx.ctx, bad, 0, nt_ui_id("pg"), 0.5F, &s_style, &s_track_decl));
    }
    nt_ui_end(s_fx.ctx);
}

#endif /* NT_ASSERT_MODE == NT_ASSERT_FULL */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_stretch_fill_ratio);
    RUN_TEST(test_value_clamps);
    RUN_TEST(test_stretch_vs_crop_command_shape);
    RUN_TEST(test_directions_place_fill_edge);
    RUN_TEST(test_crop_directions_reveal_edge);
    RUN_TEST(test_value_ease_monotonic);
    RUN_TEST(test_instant_when_speed_zero);
    RUN_TEST(test_style_defaults_valid_baseline);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_assert_track_w_zero);
    RUN_TEST(test_assert_value_not_finite);
    RUN_TEST(test_assert_data_flags_transform);
#endif
    return UNITY_END();
}
