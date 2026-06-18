/* Dropdown / combobox tests (WGT-01). Driven through the walker fixture + NT_TEST_ACCESS probes (no GL
 * surface). Covers: a row click writes the game-owned int* selected and closes the list; a long list
 * scrolls via the nt_ui_scroll wrapper without leaking a scroll-container state-pool slot across N
 * open/close cycles (Pitfall 7 / T-65-14); the list edge-flips ABOVE near the bottom border. */

#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>

#include "clay.h"
#include "input/nt_input_internal.h"
#include "memory/nt_mem_scratch.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_anim.h"
#include "ui/nt_ui_dropdown.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_popup.h"
#include "ui/nt_ui_scroll.h"
#include "ui/nt_ui_state.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

#define VIEW_W 800.0F
#define VIEW_H 600.0F

#define DD_A 0x44D001U

static const char *const s_short[] = {"Alpha", "Beta", "Gamma"};
/* >max_visible_rows so the list scrolls. */
static const char *const s_long[] = {"r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11"};

void setUp(void) {
    nt_test_assert_install();
    nt_input_clear_all_keys();
    nt_input_poll(); /* clear sticky edges from a prior test */
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) {
    nt_input_clear_all_keys();
    ui_walker_fixture_shutdown(&s_fx);
}

static nt_pointer_t pointer_at(float x, float y, bool is_down, bool is_pressed, bool is_released) {
    nt_pointer_t p = {0};
    p.x = x;
    p.y = y;
    p.active = true;
    p.buttons[NT_BUTTON_LEFT].is_down = is_down;
    p.buttons[NT_BUTTON_LEFT].is_pressed = is_pressed;
    p.buttons[NT_BUTTON_LEFT].is_released = is_released;
    return p;
}

/* ---- ABI sanity ---- */
static void test_dropdown_abi_size(void) {
    TEST_ASSERT_EQUAL_UINT(400U, (unsigned)sizeof(nt_ui_dropdown_style_t));
    TEST_ASSERT_EQUAL_UINT(32U, (unsigned)sizeof(nt_ui_dd_state_t));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity) — flat TEST_ASSERT chain, not real nesting
static void test_dropdown_defaults_valid(void) {
    nt_ui_dropdown_style_t st = nt_ui_dropdown_style_defaults();
    TEST_ASSERT_TRUE(st.font_size > 0.0F);
    TEST_ASSERT_TRUE(st.row_height > 0U);
    TEST_ASSERT_TRUE(st.min_width > 0U);
    TEST_ASSERT_TRUE(st.max_visible_rows > 0U);
    TEST_ASSERT_TRUE(st.slice9_scale > 0.0F);
    TEST_ASSERT_TRUE(st.state_speed >= 0.0F);
    TEST_ASSERT_TRUE(st.value_speed >= 0.0F);
    TEST_ASSERT_TRUE(st.open_ease_speed == 0.0F); /* plumbed knob; default snaps (0) -> preserves snap-open */
    /* Embedded list scroll = atlas-free scroll defaults (game wires bar sprites). */
    TEST_ASSERT_EQUAL_UINT(0U, st.list_scroll.track_ref.atlas.id);
    TEST_ASSERT_EQUAL_UINT(0U, st.list_scroll.thumb_ref.atlas.id);
    TEST_ASSERT_TRUE(st.list_scroll.scroll_y);
    /* Atlas-free baseline: per-state bgs carry no art (flat fallback path). */
    TEST_ASSERT_EQUAL_UINT(0U, st.trigger_idle.bg.atlas.id);
    TEST_ASSERT_EQUAL_UINT(0U, st.row_idle.bg.atlas.id);
    TEST_ASSERT_TRUE(st.trigger_idle.scale > 0.0F);
    TEST_ASSERT_TRUE(st.row_selected.scale > 0.0F);
    /* Default is text-only (no icon gutter). */
    TEST_ASSERT_EQUAL_UINT(0U, st.icon_size);
}

/* One full dropdown frame: a trigger at a fixed position, then the open list. Returns whether a
 * selection was made this frame. The trigger uses a floating element so its bbox is at (tx,ty). */
static bool dropdown_frame_icons(const nt_pointer_t *p, float tx, float ty, const char *const *labels, const nt_atlas_region_ref_t *icons, int count, int *selected, bool *open,
                                 nt_ui_dropdown_style_t *st, bool *out_made) {
    bool toggled = false;
    bool made = false;
    nt_mem_scratch_reset(); /* per-frame scratch reset, exactly as the engine main loop does */
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, p, 1);
    CLAY({.id = (Clay_ElementId){.id = 0xDD0007U}, .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = tx, .y = ty}}}) {
        toggled = nt_ui_dropdown_trigger(s_fx.ctx, NT_UI_DATA_LAYER(1), 2U, DD_A, labels, count, *selected, "Select...", st,
                                         &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(160), CLAY_SIZING_FIXED(32)}}}, open);
    }
    made = nt_ui_dropdown_list(s_fx.ctx, NT_UI_DATA_LAYER(1), 2U, DD_A, labels, icons, count, selected, st, open);
    nt_ui_end(s_fx.ctx);
    if (out_made != NULL) {
        *out_made = made;
    }
    return toggled;
}

/* No-icon convenience wrapper (text-only list) so existing tests keep their call shape. */
static bool dropdown_frame(const nt_pointer_t *p, float tx, float ty, const char *const *labels, int count, int *selected, bool *open, nt_ui_dropdown_style_t *st, bool *out_made) {
    return dropdown_frame_icons(p, tx, ty, labels, NULL, count, selected, open, st, out_made);
}

/* ---- Trigger toggles the game-owned open bool. ---- */
static void test_dropdown_trigger_toggles_open(void) {
    nt_ui_dropdown_style_t st = nt_ui_dropdown_style_defaults();
    int selected = -1;
    bool open = false;

    /* Warm frame so the trigger bbox is baked. */
    nt_pointer_t f1 = pointer_at(40.0F, 40.0F, false, false, false);
    dropdown_frame(&f1, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);

    /* Press inside the trigger, then release inside -> click toggles open. */
    nt_pointer_t f2 = pointer_at(40.0F, 40.0F, true, true, false);
    dropdown_frame(&f2, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);
    nt_pointer_t f3 = pointer_at(40.0F, 40.0F, false, false, true);
    dropdown_frame(&f3, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);
    TEST_ASSERT_TRUE(open);
}

/* ---- A row click writes *selected and closes the list (Model D). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_dropdown_row_select_sets_int_and_closes(void) {
    nt_ui_dropdown_style_t st = nt_ui_dropdown_style_defaults();
    int selected = -1;
    bool open = true; /* list already open */

    /* The list opens below the trigger at (30,30)+(0,32). Row i center ~ y = 62 + pad + i*row_height. */
    const float row_x = 60.0F;
    const float row1_y = 30.0F + 32.0F + (float)st.pad + (1.5F * (float)st.row_height);

    /* Two warm frames: the list anchors to the trigger's PREVIOUS-frame bbox (1-frame IM lag), so the
     * rows are only at their final screen position from the 2nd frame on. */
    nt_pointer_t w = pointer_at(row_x, row1_y, false, false, false);
    dropdown_frame(&w, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);
    dropdown_frame(&w, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);

    /* Frame 2: press on row 1. */
    nt_pointer_t pr = pointer_at(row_x, row1_y, true, true, false);
    dropdown_frame(&pr, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);

    /* Frame 3: release over row 1 -> clicked -> selected = 1, open cleared. */
    bool made = false;
    nt_pointer_t rl = pointer_at(row_x, row1_y, false, false, true);
    dropdown_frame(&rl, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, &made);
    TEST_ASSERT_EQUAL_INT(1, selected);
    TEST_ASSERT_FALSE(open);
    TEST_ASSERT_TRUE(made);
}

/* ---- Non-leak: opening + closing a LONG (scrolling) list >= 8 times must not grow the state-pool
 *      slot count monotonically (the scroll container's cell is GC-safe; a raw .clip would leak). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_dropdown_long_list_scroll_no_leak(void) {
    nt_ui_dropdown_style_t st = nt_ui_dropdown_style_defaults();
    st.max_visible_rows = 4; /* 12 rows > 4 -> the list scrolls */
    int selected = 0;

    /* Baseline: one closed-then-open-then-closed warm cycle, settle, sample the pool. */
    bool open = false;
    nt_pointer_t idle = pointer_at(400.0F, 300.0F, false, false, false);
    dropdown_frame(&idle, 30.0F, 30.0F, s_long, 12, &selected, &open, &st, NULL);

    uint32_t baseline = 0U;
    for (int cycle = 0; cycle < 10; ++cycle) {
        /* Open: declare the scrolling list for several frames (lets the scroll cell allocate). */
        open = true;
        for (int f = 0; f < 4; ++f) {
            dropdown_frame(&idle, 30.0F, 30.0F, s_long, 12, &selected, &open, &st, NULL);
        }
        /* Close + let the close tween settle so the popup stops declaring the list. */
        open = false;
        for (int f = 0; f < 60; ++f) {
            dropdown_frame(&idle, 30.0F, 30.0F, s_long, 12, &selected, &open, &st, NULL);
        }
        const uint32_t used = nt_ui_state_used_slots(s_fx.ctx);
        if (cycle == 1) {
            baseline = used; /* after cycle 0/1 the working set is established */
        } else if (cycle > 1) {
            /* No monotonic growth: the per-cycle scroll cell is reused, not leaked. */
            TEST_ASSERT_TRUE_MESSAGE(used <= baseline, "scroll-container pool slot leaked across open/close cycles");
        }
    }
}

/* ---- Long list with bar sprites wired shows a visible scrollbar: the y-bar emits with non-zero
 *      geometry + opacity (FIX: defaults leave scroll refs {0} so scroll draws no bar). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_dropdown_long_list_shows_scrollbar(void) {
    nt_ui_dropdown_style_t st = nt_ui_dropdown_style_defaults();
    st.max_visible_rows = 4; /* 12 rows > 4 -> the list scrolls */
    /* Wire bar sprites exactly as the showcase does (via the embedded list_scroll); non-zero refs let scroll draw the bar. */
    st.list_scroll.track_ref = nt_atlas_ref((nt_resource_t){.id = 1U}, 0x100U);
    st.list_scroll.thumb_ref = nt_atlas_ref((nt_resource_t){.id = 1U}, 0x101U);

    int selected = 0;
    bool open = true;

    /* Two warm frames: the scroll container's bar reads prev-frame solved dims (1-frame lag), so the bar
     * geometry is only valid from the 2nd frame on. */
    nt_pointer_t idle = pointer_at(400.0F, 300.0F, false, false, false);
    dropdown_frame(&idle, 30.0F, 30.0F, s_long, 12, &selected, &open, &st, NULL);
    dropdown_frame(&idle, 30.0F, 30.0F, s_long, 12, &selected, &open, &st, NULL);

    /* The last scroll container declared this frame was the dropdown's list wrapper. */
    TEST_ASSERT_EQUAL_UINT(nt_ui_dropdown_test_scroll_id(DD_A), nt_ui_scroll_test_last_scroll_id());
    /* The vertical bar emitted (bit1 = y). */
    TEST_ASSERT_TRUE_MESSAGE((nt_ui_scroll_test_last_bar_emitted_axes() & 2U) != 0U, "dropdown long list must emit a vertical scrollbar");
    /* Non-zero geometry + visible opacity -> a real bar is drawn, not skipped. */
    float thumb_len = 0.0F;
    float thumb_off = 0.0F;
    float track_len = 0.0F;
    float opacity = 0.0F;
    nt_ui_scroll_test_last_bar_geometry(1, &thumb_len, &thumb_off, &track_len, &opacity);
    TEST_ASSERT_TRUE_MESSAGE(track_len > 0.0F, "scrollbar track must have non-zero length");
    TEST_ASSERT_TRUE_MESSAGE(thumb_len > 0.0F, "scrollbar thumb must have non-zero length");
    TEST_ASSERT_TRUE_MESSAGE(opacity > 0.0F, "ALWAYS-visible scrollbar must be opaque");
}

/* ---- Showcase-fidelity scrollbar: replicate the City dropdown EXACTLY (panel_art slice9 bg, icon
 *      gutter, eased open, ALWAYS bar via embedded list_scroll) and assert the y-bar still emits with
 *      real geometry. Catches a regression where the IMAGE scroll container or the open tween suppresses
 *      the bar even though the simpler test passes. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_dropdown_long_list_scrollbar_showcase_fidelity(void) {
    nt_ui_dropdown_style_t st = nt_ui_dropdown_style_defaults();
    st.max_visible_rows = 6;
    st.row_height = 30U;
    st.icon_size = 22U;
    st.open_ease_speed = 14.0F; /* eased open, like the showcase */
    /* IMAGE panel + bar sprites, exactly mirroring s_dropdown_dark. */
    st.panel_bg = nt_atlas_ref((nt_resource_t){.id = 1U}, 0x200U);
    st.panel_tint = 0xFFB0AAA4U;
    st.list_scroll = nt_ui_scroll_style_defaults();
    st.list_scroll.track_ref = nt_atlas_ref((nt_resource_t){.id = 1U}, 0x100U);
    st.list_scroll.thumb_ref = nt_atlas_ref((nt_resource_t){.id = 1U}, 0x101U);
    st.list_scroll.bar_visibility = NT_UI_SCROLLBAR_ALWAYS;

    int selected = 0;
    bool open = true;

    nt_pointer_t idle = pointer_at(400.0F, 300.0F, false, false, false);
    /* Several frames: let the eased open settle so the scroll container's prev-frame dims are solid. */
    for (int f = 0; f < 6; ++f) {
        dropdown_frame(&idle, 30.0F, 30.0F, s_long, 12, &selected, &open, &st, NULL);
    }

    TEST_ASSERT_EQUAL_UINT(nt_ui_dropdown_test_scroll_id(DD_A), nt_ui_scroll_test_last_scroll_id());
    TEST_ASSERT_TRUE_MESSAGE((nt_ui_scroll_test_last_bar_emitted_axes() & 2U) != 0U, "showcase-fidelity long list must emit a vertical scrollbar");
    float thumb_len = 0.0F;
    float thumb_off = 0.0F;
    float track_len = 0.0F;
    float opacity = 0.0F;
    nt_ui_scroll_test_last_bar_geometry(1, &thumb_len, &thumb_off, &track_len, &opacity);
    TEST_ASSERT_TRUE_MESSAGE(track_len > 0.0F, "showcase scrollbar track must have non-zero length");
    TEST_ASSERT_TRUE_MESSAGE(thumb_len > 0.0F, "showcase scrollbar thumb must have non-zero length");
    TEST_ASSERT_TRUE_MESSAGE(opacity > 0.0F, "ALWAYS-visible showcase scrollbar must be opaque");
    /* ROOT of BUG2: the bar must draw on the dropdown's CONTENT layer (1 here), not hardcoded 0 — else
     * an opaque panel on layer 1 sorts OVER the bar and hides it (visible "no scrollbar"). */
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1U, nt_ui_scroll_test_last_bar_layer(1), "scrollbar must draw on the container content layer, not buried under the panel");
}

/* ---- ROOT of the Wave-2.9 BUG: the dropdown list is a popup-nested scroll, so its bar must float at a
 *      Clay zIndex ABOVE the popup panel band (stride*depth) — else the (settled-opaque) panel sorts over
 *      the bar globally and the bar is only seen through the translucent open/close tween. The Wave-2.8
 *      content-LAYER fix is necessary but not sufficient (layer orders intra-segment; Clay zIndex orders
 *      floating roots globally). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_dropdown_long_list_scrollbar_above_popup_band(void) {
    nt_ui_dropdown_style_t st = nt_ui_dropdown_style_defaults();
    st.max_visible_rows = 4; /* 12 rows > 4 -> the list scrolls */
    st.list_scroll.track_ref = nt_atlas_ref((nt_resource_t){.id = 1U}, 0x100U);
    st.list_scroll.thumb_ref = nt_atlas_ref((nt_resource_t){.id = 1U}, 0x101U);

    int selected = 0;
    bool open = true;
    nt_pointer_t idle = pointer_at(400.0F, 300.0F, false, false, false);
    dropdown_frame(&idle, 30.0F, 30.0F, s_long, 12, &selected, &open, &st, NULL);
    dropdown_frame(&idle, 30.0F, 30.0F, s_long, 12, &selected, &open, &st, NULL);

    TEST_ASSERT_TRUE_MESSAGE((nt_ui_scroll_test_last_bar_emitted_axes() & 2U) != 0U, "popup-nested long list must emit a vertical scrollbar");
    /* The single dropdown popup floats at depth 1: panel_z == stride*1. The bar must sit strictly above it. */
    const int16_t stride = s_fx.ctx->modal_zband_stride;
    const int16_t bar_z = nt_ui_scroll_test_last_bar_zindex(1);
    TEST_ASSERT_GREATER_THAN_INT16_MESSAGE(0, bar_z, "popup-nested bar zIndex must be > 0");
    TEST_ASSERT_GREATER_THAN_INT16_MESSAGE(stride, bar_z, "popup-nested bar zIndex must sit ABOVE the popup panel band (stride*depth)");
}

/* ---- Eased open is STABLE: with open_ease_speed > 0, opening the list then running idle frames with
 *      NO input must NOT oscillate *open (the Wave-2.7 flicker repro). The trigger sits under the
 *      full-viewport catcher when open; a frame with no click must never raise a dismiss-close. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_dropdown_eased_open_no_flicker(void) {
    nt_ui_dropdown_style_t st = nt_ui_dropdown_style_defaults();
    st.open_ease_speed = 14.0F; /* the showcase Fruit dropdown's eased-open knob */
    int selected = -1;
    bool open = false;

    /* Warm frame so the trigger bbox is baked. */
    nt_pointer_t f1 = pointer_at(40.0F, 40.0F, false, false, false);
    dropdown_frame(&f1, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);

    /* Click the trigger: press then release inside -> *open = true. */
    nt_pointer_t pr = pointer_at(40.0F, 40.0F, true, true, false);
    dropdown_frame(&pr, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);
    nt_pointer_t rl = pointer_at(40.0F, 40.0F, false, false, true);
    dropdown_frame(&rl, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);
    TEST_ASSERT_TRUE_MESSAGE(open, "trigger click must open the list");

    /* Several idle frames with NO button activity: *open must stay true and the popup stay present.
     * A flicker shows up here as *open dropping to false (catcher dismiss) on some frame. */
    nt_pointer_t idle = pointer_at(40.0F, 40.0F, false, false, false);
    for (int f = 0; f < 12; ++f) {
        dropdown_frame(&idle, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);
        TEST_ASSERT_TRUE_MESSAGE(open, "eased-open list must stay open across idle frames (no flicker)");
    }
}

/* Frame that ALSO declares a swarm of animated dummy widgets so the 64-slot anim table churns and
 * collides with the popup's slot window — mimics a busy showcase frame. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void dropdown_frame_busy(const nt_pointer_t *p, const char *const *labels, int count, int *selected, bool *open, nt_ui_dropdown_style_t *st, int n_dummy) {
    nt_mem_scratch_reset();
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, p, 1);
    /* Swarm declared BEFORE the dropdown (real frame order: many widgets precede it). Each dummy eases a
     * slot; the ids sweep the whole 64-slot table so the popup's probe window is already full when the
     * popup touches it — forcing the popup to evict a slot to host itself (the busy-frame condition). */
    const nt_ui_anim_target_t tgt = {.scale_x = 1.0F, .scale_y = 1.0F, .scale_z = 1.0F, .opacity = 1.0F, .value_t = 1.0F};
    for (int d = 0; d < n_dummy; ++d) {
        (void)nt_ui_anim(s_fx.ctx, 0x90000001U + (uint32_t)d, &tgt, 0.0F, 14.0F);
    }
    CLAY({.id = (Clay_ElementId){.id = 0xDD0007U}, .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = 30.0F, .y = 30.0F}}}) {
        (void)nt_ui_dropdown_trigger(s_fx.ctx, NT_UI_DATA_LAYER(1), 2U, DD_A, labels, count, *selected, "Select...", st,
                                     &(Clay_ElementDeclaration){.layout = {.sizing = {CLAY_SIZING_FIXED(160), CLAY_SIZING_FIXED(32)}}}, open);
    }
    (void)nt_ui_dropdown_list(s_fx.ctx, NT_UI_DATA_LAYER(1), 2U, DD_A, labels, NULL, count, selected, st, open);
    nt_ui_end(s_fx.ctx);
}

/* ---- Eviction-induced flicker (ROOT): a busy frame can evict the popup's anim slot. The OLD code
 *      keyed the entrance t=0 re-seed off "anim slot absent", so an evicted-then-missing slot re-seeded
 *      EVERY churned frame -> the eased open never climbed (continuous fade-from-0 flicker). The fix
 *      gates the seed on a NON-evicting tween-state edge, so for ONE continuous open the entrance seed
 *      fires AT MOST ONCE regardless of anim-table churn. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_dropdown_eased_open_no_reseed_under_churn(void) {
    nt_ui_dropdown_style_t st = nt_ui_dropdown_style_defaults();
    st.open_ease_speed = 14.0F;
    int selected = -1;
    bool open = true; /* one continuous open the whole test */

    nt_ui_popup_test_entrance_seed_reset();
    nt_pointer_t idle = pointer_at(400.0F, 300.0F, false, false, false);
    /* Heavy churn every frame across a long open: 70 dummies thrash the 64-slot anim table so the popup's
     * slot is repeatedly evicted. The seed must NOT re-fire per frame. */
    for (int f = 0; f < 30; ++f) {
        dropdown_frame_busy(&idle, s_short, 3, &selected, &open, &st, 70);
    }
    const uint32_t seeds = nt_ui_popup_test_entrance_seed_count();
    TEST_ASSERT_TRUE_MESSAGE(seeds <= 1U, "entrance t=0 re-seed must fire at most once per open-edge (not per churned frame)");
}

/* ---- Re-click the trigger WHILE the eased-open list is still tweening: the trigger toggles open->false
 *      once, and a SINGLE click must not bounce it back open (catcher dismiss + trigger toggle fighting
 *      over the same click during the open tween). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_dropdown_eased_reclick_closes_once(void) {
    nt_ui_dropdown_style_t st = nt_ui_dropdown_style_defaults();
    st.open_ease_speed = 14.0F;
    int selected = -1;
    bool open = true; /* already open, mid-tween from the first frame */

    /* Warm frames: let the trigger bbox + popup bbox bake while the tween rises. */
    nt_pointer_t idle = pointer_at(400.0F, 300.0F, false, false, false);
    dropdown_frame(&idle, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);
    dropdown_frame(&idle, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);

    /* Click ON the trigger (press+release) to close. The catcher (topmost) sees the same click as an
     * outside-the-panel dismiss; the trigger sees it as a toggle. Both must resolve to a SINGLE close. */
    nt_pointer_t pr = pointer_at(40.0F, 40.0F, true, true, false);
    dropdown_frame(&pr, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);
    nt_pointer_t rl = pointer_at(40.0F, 40.0F, false, false, true);
    dropdown_frame(&rl, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);
    TEST_ASSERT_FALSE_MESSAGE(open, "a single trigger re-click must close the list (and stay closed)");

    /* Idle frames: it must STAY closed (no bounce-reopen from a fighting click). */
    for (int f = 0; f < 6; ++f) {
        dropdown_frame(&idle, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);
        TEST_ASSERT_FALSE_MESSAGE(open, "closed list must stay closed (no reopen flicker)");
    }
}

/* ---- Edge-flip: a trigger near the bottom border opens its list ABOVE (popup-core side probe). ---- */
static void test_dropdown_edge_flip_up_near_bottom(void) {
    nt_ui_dropdown_style_t st = nt_ui_dropdown_style_defaults();
    int selected = 0;
    bool open = true;

    /* Trigger near the bottom: the list would overflow downward -> flips ABOVE. Two frames so the list's
     * prev-frame bbox feeds the edge-flip projection (frame 1 measures, frame 2 flips). */
    nt_pointer_t idle = pointer_at(400.0F, 300.0F, false, false, false);
    dropdown_frame(&idle, 30.0F, 540.0F, s_short, 3, &selected, &open, &st, NULL);
    dropdown_frame(&idle, 30.0F, 540.0F, s_short, 3, &selected, &open, &st, NULL);
    TEST_ASSERT_EQUAL_UINT8(NT_UI_POPUP_ABOVE, nt_ui_dropdown_test_last_side());
}

/* The row's label x after two warm frames (1-frame IM lag bakes the bbox). */
static float row_label_x(const nt_pointer_t *p, nt_ui_dropdown_style_t *st, const nt_atlas_region_ref_t *icons, int idx) {
    int selected = 0;
    bool open = true;
    dropdown_frame_icons(p, 30.0F, 30.0F, s_short, icons, 3, &selected, &open, st, NULL);
    dropdown_frame_icons(p, 30.0F, 30.0F, s_short, icons, 3, &selected, &open, st, NULL);
    const nt_ui_bbox_t bb = nt_ui_dropdown_test_row_label_bbox(s_fx.ctx, DD_A, idx);
    TEST_ASSERT_TRUE(bb.found);
    return bb.x;
}

/* ---- icon_size opens a leading gutter: the row label shifts RIGHT vs the text-only (icon_size=0) row. ---- */
static void test_dropdown_icon_size_gutter_shifts_label(void) {
    nt_pointer_t idle = pointer_at(400.0F, 300.0F, false, false, false);

    nt_ui_dropdown_style_t no_gut = nt_ui_dropdown_style_defaults();
    no_gut.icon_size = 0U; /* text-only */
    const float x_text_only = row_label_x(&idle, &no_gut, NULL, 0);

    nt_ui_dropdown_style_t gut = nt_ui_dropdown_style_defaults();
    gut.icon_size = 24U; /* reserves a 24px leading gutter */
    const float x_gutter = row_label_x(&idle, &gut, NULL, 0);

    /* The gutter + child gap push the label right by at least the gutter width. */
    TEST_ASSERT_TRUE_MESSAGE(x_gutter >= x_text_only + (float)gut.icon_size, "icon_size gutter must shift the label right by >= icon_size");
}

/* ---- NULL-icon alignment: with the gutter open, a row whose icon ref is set and a row whose icon is
 *      absent both keep the SAME label x (the gutter holds alignment either way). ---- */
static void test_dropdown_null_icon_aligns(void) {
    nt_pointer_t idle = pointer_at(400.0F, 300.0F, false, false, false);

    nt_ui_dropdown_style_t st = nt_ui_dropdown_style_defaults();
    st.icon_size = 24U;

    /* Row 0 carries an icon ref (atlas.id != 0); rows 1/2 are unset ({0} = aligned-empty gutter). */
    const nt_atlas_region_ref_t icons[] = {nt_atlas_ref((nt_resource_t){.id = 1U}, 0x1234U), {0}, {0}};

    int selected = 0;
    bool open = true;
    dropdown_frame_icons(&idle, 30.0F, 30.0F, s_short, icons, 3, &selected, &open, &st, NULL);
    dropdown_frame_icons(&idle, 30.0F, 30.0F, s_short, icons, 3, &selected, &open, &st, NULL);

    const nt_ui_bbox_t iconed = nt_ui_dropdown_test_row_label_bbox(s_fx.ctx, DD_A, 0);
    const nt_ui_bbox_t empty = nt_ui_dropdown_test_row_label_bbox(s_fx.ctx, DD_A, 1);
    TEST_ASSERT_TRUE(iconed.found);
    TEST_ASSERT_TRUE(empty.found);
    /* Same gutter width -> same label x regardless of whether the icon is present (Unity float asserts
     * are disabled project-wide, so compare a manual epsilon). */
    const float dx = (iconed.x > empty.x) ? (iconed.x - empty.x) : (empty.x - iconed.x);
    TEST_ASSERT_TRUE_MESSAGE(dx <= 0.5F, "NULL icon must keep the same aligned label x as an iconed row");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_dropdown_abi_size);
    RUN_TEST(test_dropdown_defaults_valid);
    RUN_TEST(test_dropdown_trigger_toggles_open);
    RUN_TEST(test_dropdown_row_select_sets_int_and_closes);
    RUN_TEST(test_dropdown_long_list_scroll_no_leak);
    RUN_TEST(test_dropdown_long_list_shows_scrollbar);
    RUN_TEST(test_dropdown_long_list_scrollbar_showcase_fidelity);
    RUN_TEST(test_dropdown_long_list_scrollbar_above_popup_band);
    RUN_TEST(test_dropdown_eased_open_no_flicker);
    RUN_TEST(test_dropdown_eased_open_no_reseed_under_churn);
    RUN_TEST(test_dropdown_eased_reclick_closes_once);
    RUN_TEST(test_dropdown_edge_flip_up_near_bottom);
    RUN_TEST(test_dropdown_icon_size_gutter_shifts_label);
    RUN_TEST(test_dropdown_null_icon_aligns);
    return UNITY_END();
}
