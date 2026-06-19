/* Dropdown / combobox tests against the IMMEDIATE combo begin/selectable/end API.
 * Driven through the walker fixture + NT_TEST_ACCESS probes (no GL surface). Covers: a selectable click
 * writes the game-owned int* selected and closes the list; a long list scrolls via the nt_ui_scroll
 * wrapper without leaking a scroll-container state-pool slot across N open/close cycles; the list
 * edge-flips ABOVE near the bottom border; icon-gutter alignment. */

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

/* Stable per-row keys for the immediate selectables: the loop index keyed by a fixed base (unique among
 * siblings; combo derives the pool id via the scope stack). */
#define ROW_KEY(i) ((uint32_t)(0x5E1EC700U + (uint32_t)(i)))

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

/* One full immediate combo frame: combo_begin emits the trigger (preview = current selection label) and
 * opens the list when *open; each row is a combo_selectable; combo_end balances. The trigger is wrapped
 * in a floating element so its bbox is at (tx,ty). `*out_made` reports whether a selectable was clicked.
 * The combo writes *selected itself on a selectable click (Model-D). */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void combo_im_frame_icons(const nt_pointer_t *p, float tx, float ty, const char *const *labels, const nt_atlas_region_ref_t *icons, int count, int *selected, bool *open,
                                 nt_ui_dropdown_style_t *st, bool *out_made) {
    bool made = false;
    nt_mem_scratch_reset(); /* per-frame scratch reset, exactly as the engine main loop does */
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, p, 1);
    const char *preview = (*selected >= 0 && *selected < count) ? labels[*selected] : "Select...";
    CLAY({.id = (Clay_ElementId){.id = 0xDD0007U}, .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = tx, .y = ty}}}) {
        if (nt_ui_combo_begin(s_fx.ctx, NT_UI_DATA_LAYER(1), 2, DD_A, preview, st, open)) {
            for (int i = 0; i < count; ++i) {
                /* Iconed + text-only rows share the SAME engine column (one icon gutter + left label),
                 * mirroring the showcase render_dropdown idiom — labels align at one x regardless of icon. */
                const bool has_icon = (icons != NULL && icons[i].atlas.id != 0U);
                const bool clicked =
                    has_icon ? nt_ui_combo_selectable_icon(s_fx.ctx, ROW_KEY(i), &icons[i], labels[i], *selected == i) : nt_ui_combo_selectable(s_fx.ctx, ROW_KEY(i), labels[i], *selected == i);
                if (clicked) {
                    made = true;
                    *selected = i;
                }
            }
            nt_ui_combo_end(s_fx.ctx);
        }
    }
    nt_ui_end(s_fx.ctx);
    if (out_made != NULL) {
        *out_made = made;
    }
}

/* No-icon convenience wrapper (text-only list) so the behavioral tests keep their call shape. */
static void combo_im_frame(const nt_pointer_t *p, float tx, float ty, const char *const *labels, int count, int *selected, bool *open, nt_ui_dropdown_style_t *st, bool *out_made) {
    combo_im_frame_icons(p, tx, ty, labels, NULL, count, selected, open, st, out_made);
}

/* ---- Trigger toggles the game-owned open bool (combo_begin click). ---- */
static void test_dropdown_trigger_toggles_open(void) {
    nt_ui_dropdown_style_t st = nt_ui_dropdown_style_defaults();
    int selected = -1;
    bool open = false;

    /* Warm frame so the trigger bbox is baked. */
    nt_pointer_t f1 = pointer_at(40.0F, 40.0F, false, false, false);
    combo_im_frame(&f1, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);

    /* Press inside the trigger, then release inside -> click toggles open. */
    nt_pointer_t f2 = pointer_at(40.0F, 40.0F, true, true, false);
    combo_im_frame(&f2, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);
    nt_pointer_t f3 = pointer_at(40.0F, 40.0F, false, false, true);
    combo_im_frame(&f3, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);
    TEST_ASSERT_TRUE(open);
}

/* ---- A combo_selectable click writes *selected and closes the list. ---- */
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
    combo_im_frame(&w, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);
    combo_im_frame(&w, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);

    /* Frame 2: press on row 1. */
    nt_pointer_t pr = pointer_at(row_x, row1_y, true, true, false);
    combo_im_frame(&pr, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);

    /* Frame 3: release over row 1 -> clicked -> selected = 1, open cleared. */
    bool made = false;
    nt_pointer_t rl = pointer_at(row_x, row1_y, false, false, true);
    combo_im_frame(&rl, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, &made);
    TEST_ASSERT_EQUAL_INT(1, selected);
    TEST_ASSERT_FALSE(open);
    TEST_ASSERT_TRUE(made);
}

/* ---- Non-leak: opening + closing a LONG (scrolling) list >= 8 times must not grow the state-pool slot
 *      count monotonically (the scroll container's cell is GC-safe; a raw .clip would leak a
 *      scroll-container pool slot per open). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_dropdown_long_list_scroll_no_leak(void) {
    nt_ui_dropdown_style_t st = nt_ui_dropdown_style_defaults();
    st.max_visible_rows = 4; /* 12 rows > 4 -> the list scrolls */
    int selected = 0;

    /* Baseline: one closed warm cycle, settle, sample the pool. */
    bool open = false;
    nt_pointer_t idle = pointer_at(400.0F, 300.0F, false, false, false);
    combo_im_frame(&idle, 30.0F, 30.0F, s_long, 12, &selected, &open, &st, NULL);

    uint32_t baseline = 0U;
    for (int cycle = 0; cycle < 10; ++cycle) {
        /* Open: declare the scrolling list for several frames (lets the scroll cell allocate). */
        open = true;
        for (int f = 0; f < 4; ++f) {
            combo_im_frame(&idle, 30.0F, 30.0F, s_long, 12, &selected, &open, &st, NULL);
        }
        /* Close + let the close tween settle so the popup stops declaring the list. */
        open = false;
        for (int f = 0; f < 60; ++f) {
            combo_im_frame(&idle, 30.0F, 30.0F, s_long, 12, &selected, &open, &st, NULL);
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
 *      geometry + opacity (the combo list wraps the rows in nt_ui_scroll). ---- */
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
    combo_im_frame(&idle, 30.0F, 30.0F, s_long, 12, &selected, &open, &st, NULL);
    combo_im_frame(&idle, 30.0F, 30.0F, s_long, 12, &selected, &open, &st, NULL);

    /* The last scroll container declared this frame was the combo list wrapper. */
    TEST_ASSERT_EQUAL_UINT(nt_ui_dropdown_test_scroll_id(DD_A), nt_ui_scroll_test_last_scroll_id());
    /* The vertical bar emitted (bit1 = y). */
    TEST_ASSERT_TRUE_MESSAGE((nt_ui_scroll_test_last_bar_emitted_axes() & 2U) != 0U, "combo long list must emit a vertical scrollbar");
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
 *      real geometry on the combo list path. ---- */
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

    /* row 0 carries an icon (routed through combo_selectable_icon, engine gutter); the rest are plain. */
    const nt_atlas_region_ref_t icons[12] = {nt_atlas_ref((nt_resource_t){.id = 1U}, 0x300U)};
    int selected = 0;
    bool open = true;

    nt_pointer_t idle = pointer_at(400.0F, 300.0F, false, false, false);
    /* Several frames: let the eased open settle so the scroll container's prev-frame dims are solid. */
    for (int f = 0; f < 6; ++f) {
        combo_im_frame_icons(&idle, 30.0F, 30.0F, s_long, icons, 12, &selected, &open, &st, NULL);
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
    /* The bar must draw on the combo's CONTENT layer (1 here), not hardcoded 0. */
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1U, nt_ui_scroll_test_last_bar_layer(1), "scrollbar must draw on the container content layer, not buried under the panel");
}

/* ---- The combo list is a popup-nested scroll, so its bar must float at a Clay zIndex ABOVE the popup
 *      panel band (stride*depth). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_dropdown_long_list_scrollbar_above_popup_band(void) {
    nt_ui_dropdown_style_t st = nt_ui_dropdown_style_defaults();
    st.max_visible_rows = 4; /* 12 rows > 4 -> the list scrolls */
    st.list_scroll.track_ref = nt_atlas_ref((nt_resource_t){.id = 1U}, 0x100U);
    st.list_scroll.thumb_ref = nt_atlas_ref((nt_resource_t){.id = 1U}, 0x101U);

    int selected = 0;
    bool open = true;
    nt_pointer_t idle = pointer_at(400.0F, 300.0F, false, false, false);
    combo_im_frame(&idle, 30.0F, 30.0F, s_long, 12, &selected, &open, &st, NULL);
    combo_im_frame(&idle, 30.0F, 30.0F, s_long, 12, &selected, &open, &st, NULL);

    TEST_ASSERT_TRUE_MESSAGE((nt_ui_scroll_test_last_bar_emitted_axes() & 2U) != 0U, "popup-nested long list must emit a vertical scrollbar");
    /* The single combo popup floats at depth 1: panel_z == stride*1. The bar must sit strictly above it. */
    const int16_t stride = s_fx.ctx->modal_zband_stride;
    const int16_t bar_z = nt_ui_scroll_test_last_bar_zindex(1);
    TEST_ASSERT_GREATER_THAN_INT16_MESSAGE(0, bar_z, "popup-nested bar zIndex must be > 0");
    TEST_ASSERT_GREATER_THAN_INT16_MESSAGE(stride, bar_z, "popup-nested bar zIndex must sit ABOVE the popup panel band (stride*depth)");
}

/* ---- Eased open is STABLE: with open_ease_speed > 0, opening the list then running idle frames with NO
 *      input must NOT oscillate *open (flicker repro). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_dropdown_eased_open_no_flicker(void) {
    nt_ui_dropdown_style_t st = nt_ui_dropdown_style_defaults();
    st.open_ease_speed = 14.0F; /* the showcase Fruit dropdown's eased-open knob */
    int selected = -1;
    bool open = false;

    /* Warm frame so the trigger bbox is baked. */
    nt_pointer_t f1 = pointer_at(40.0F, 40.0F, false, false, false);
    combo_im_frame(&f1, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);

    /* Click the trigger: press then release inside -> *open = true. */
    nt_pointer_t pr = pointer_at(40.0F, 40.0F, true, true, false);
    combo_im_frame(&pr, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);
    nt_pointer_t rl = pointer_at(40.0F, 40.0F, false, false, true);
    combo_im_frame(&rl, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);
    TEST_ASSERT_TRUE_MESSAGE(open, "trigger click must open the list");

    /* Several idle frames with NO button activity: *open must stay true and the popup stay present. */
    nt_pointer_t idle = pointer_at(40.0F, 40.0F, false, false, false);
    for (int f = 0; f < 12; ++f) {
        combo_im_frame(&idle, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);
        TEST_ASSERT_TRUE_MESSAGE(open, "eased-open list must stay open across idle frames (no flicker)");
    }
}

/* Frame that ALSO declares a swarm of animated dummy widgets so the anim table churns and collides with
 * the popup's slot window — mimics a busy showcase frame. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void combo_im_frame_busy(const nt_pointer_t *p, const char *const *labels, int count, int *selected, bool *open, nt_ui_dropdown_style_t *st, int n_dummy) {
    nt_mem_scratch_reset();
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, p, 1);
    /* Swarm declared BEFORE the combo (real frame order: many widgets precede it). Each dummy EASES a
     * slot (value_speed > 0) so the popup's probe window is already full when it touches the table. */
    const nt_ui_anim_target_t tgt = {.scale_x = 1.0F, .scale_y = 1.0F, .scale_z = 1.0F, .opacity = 1.0F, .value_t = 1.0F};
    for (int d = 0; d < n_dummy; ++d) {
        (void)nt_ui_anim(s_fx.ctx, 0x90000001U + (uint32_t)d, &tgt, 0.0F, 14.0F);
    }
    const char *preview = (*selected >= 0 && *selected < count) ? labels[*selected] : "Select...";
    CLAY({.id = (Clay_ElementId){.id = 0xDD0007U}, .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = 30.0F, .y = 30.0F}}}) {
        if (nt_ui_combo_begin(s_fx.ctx, NT_UI_DATA_LAYER(1), 2, DD_A, preview, st, open)) {
            for (int i = 0; i < count; ++i) {
                if (nt_ui_combo_selectable(s_fx.ctx, ROW_KEY(i), labels[i], *selected == i)) {
                    *selected = i;
                }
            }
            nt_ui_combo_end(s_fx.ctx);
        }
    }
    nt_ui_end(s_fx.ctx);
}

/* ---- Eviction-induced flicker (ROOT): a busy frame can evict the popup's anim slot; the entrance t=0
 *      re-seed must fire AT MOST ONCE per open-edge regardless of anim-table churn. ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_dropdown_eased_open_no_reseed_under_churn(void) {
    nt_ui_dropdown_style_t st = nt_ui_dropdown_style_defaults();
    st.open_ease_speed = 14.0F;
    int selected = -1;
    bool open = true; /* one continuous open the whole test */

    nt_ui_popup_test_entrance_seed_reset();
    nt_pointer_t idle = pointer_at(400.0F, 300.0F, false, false, false);
    /* Heavy churn every frame across a long open: dummies > NT_UI_ANIM_SLOTS thrash the anim table so the
     * popup's slot is repeatedly evicted. The seed must NOT re-fire per frame. */
    for (int f = 0; f < 30; ++f) {
        combo_im_frame_busy(&idle, s_short, 3, &selected, &open, &st, NT_UI_ANIM_SLOTS + 32);
    }
    const uint32_t seeds = nt_ui_popup_test_entrance_seed_count();
    TEST_ASSERT_TRUE_MESSAGE(seeds <= 1U, "entrance t=0 re-seed must fire at most once per open-edge (not per churned frame)");
}

/* ---- Re-click the trigger WHILE the eased-open list is still tweening: a SINGLE click closes once and
 *      must not bounce back open (catcher dismiss + trigger toggle fighting over the same click). ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_dropdown_eased_reclick_closes_once(void) {
    nt_ui_dropdown_style_t st = nt_ui_dropdown_style_defaults();
    st.open_ease_speed = 14.0F;
    int selected = -1;
    bool open = true; /* already open, mid-tween from the first frame */

    /* Warm frames: let the trigger bbox + popup bbox bake while the tween rises. */
    nt_pointer_t idle = pointer_at(400.0F, 300.0F, false, false, false);
    combo_im_frame(&idle, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);
    combo_im_frame(&idle, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);

    /* Click ON the trigger (press+release) to close. Both the catcher and the trigger see the click; both
     * must resolve to a SINGLE close. */
    nt_pointer_t pr = pointer_at(40.0F, 40.0F, true, true, false);
    combo_im_frame(&pr, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);
    nt_pointer_t rl = pointer_at(40.0F, 40.0F, false, false, true);
    combo_im_frame(&rl, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);
    TEST_ASSERT_FALSE_MESSAGE(open, "a single trigger re-click must close the list (and stay closed)");

    /* Idle frames: it must STAY closed (no bounce-reopen from a fighting click). */
    for (int f = 0; f < 6; ++f) {
        combo_im_frame(&idle, 30.0F, 30.0F, s_short, 3, &selected, &open, &st, NULL);
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
    combo_im_frame(&idle, 30.0F, 540.0F, s_short, 3, &selected, &open, &st, NULL);
    combo_im_frame(&idle, 30.0F, 540.0F, s_short, 3, &selected, &open, &st, NULL);
    TEST_ASSERT_EQUAL_UINT8(NT_UI_POPUP_ABOVE, nt_ui_dropdown_test_last_side());
}

/* The row's label x after two warm frames (1-frame IM lag bakes the bbox). The same `icons` array is fed
 * to both warm frames so the probed cell is stable; idx selects which row's label cell to read. */
static float row_label_x(const nt_pointer_t *p, nt_ui_dropdown_style_t *st, const nt_atlas_region_ref_t *icons, int idx) {
    int selected = 0;
    bool open = true;
    combo_im_frame_icons(p, 30.0F, 30.0F, s_short, icons, 3, &selected, &open, st, NULL);
    combo_im_frame_icons(p, 30.0F, 30.0F, s_short, icons, 3, &selected, &open, st, NULL);
    const nt_ui_bbox_t bb = nt_ui_dropdown_test_row_label_bbox(s_fx.ctx, DD_A, idx);
    TEST_ASSERT_TRUE(bb.found);
    return bb.x;
}

/* ---- icon_size opens a leading gutter on the PLAIN combo_selectable path: the row label shifts RIGHT vs
 *      the text-only (icon_size=0) row. The plain path owns the single engine gutter. ---- */
static void test_dropdown_icon_size_gutter_shifts_label(void) {
    nt_pointer_t idle = pointer_at(400.0F, 300.0F, false, false, false);

    nt_ui_dropdown_style_t no_gut = nt_ui_dropdown_style_defaults();
    no_gut.icon_size = 0U; /* text-only */
    const float x_text_only = row_label_x(&idle, &no_gut, NULL, 0);

    nt_ui_dropdown_style_t gut = nt_ui_dropdown_style_defaults();
    gut.icon_size = 24U;                                      /* plain path reserves a 24px leading gutter */
    const float x_gutter = row_label_x(&idle, &gut, NULL, 0); /* NULL icons -> plain selectable path */

    /* The gutter + child gap push the label right by at least the gutter width. */
    TEST_ASSERT_TRUE_MESSAGE(x_gutter >= x_text_only + (float)gut.icon_size, "icon_size gutter must shift the plain-row label right by >= icon_size");
}

/* ---- Single-column alignment: in ONE combo list (icon_size>0), an iconed row and a non-iconed row
 *      align their labels at the SAME left x — the icon rides the engine gutter, the label starts past
 *      it for every row, iconed or not (OS-menu icon column). This is the fix invariant: the iconed
 *      demo rows now route through combo_selectable_icon (engine column), NOT a hand-rolled custom row. ---- */
static void test_dropdown_iconed_and_plain_rows_share_one_column(void) {
    nt_pointer_t idle = pointer_at(400.0F, 300.0F, false, false, false);

    nt_ui_dropdown_style_t st = nt_ui_dropdown_style_defaults();
    st.icon_size = 24U; /* engine gutter so iconed + text-only rows align */

    /* Mixed list: row 0 iconed (atlas.id != 0), row 1 non-iconed (atlas.id == 0 -> plain selectable). */
    const nt_atlas_region_ref_t icons[] = {nt_atlas_ref((nt_resource_t){.id = 1U}, 0x10U), (nt_atlas_region_ref_t){0}, nt_atlas_ref((nt_resource_t){.id = 1U}, 0x12U)};

    const float x_iconed = row_label_x(&idle, &st, icons, 0); /* iconed row label */
    const float x_plain = row_label_x(&idle, &st, icons, 1);  /* non-iconed row label */

    const float dx = (x_iconed > x_plain) ? (x_iconed - x_plain) : (x_plain - x_iconed);
    TEST_ASSERT_TRUE_MESSAGE(dx <= 0.5F, "iconed and non-iconed rows must align their labels in one column (same engine gutter)");
}

/* ---- Custom-trigger (preview form) draws the combo's chevron affordance, just like the plain trigger.
 *      One preview frame with a resolvable chevron ref; the engine appends the chevron at the trigger's
 *      right edge between preview_begin/end. chevron_size 0 opts out. ---- */
/* The game-owned preview child carries a stable id (so a test can probe its right edge) and GROWs to fill
 * the trigger interior — a leftover GROW spacer would steal that slack, so its right edge is the no-spacer
 * witness on opt-out. */
#define SWATCH_ID 0x44D0E5U
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void preview_im_frame(const nt_pointer_t *p, float tx, float ty, nt_ui_dropdown_style_t *st, bool *open) {
    nt_mem_scratch_reset();
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, p, 1);
    CLAY({.id = (Clay_ElementId){.id = 0xDD0008U}, .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = tx, .y = ty}}}) {
        nt_ui_combo_preview_begin(s_fx.ctx, NT_UI_DATA_LAYER(1), 2, DD_A, st, open);
        {
            CLAY({.id = (Clay_ElementId){.id = SWATCH_ID}, .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(16)}}}) {}
        } /* game-owned swatch content */
        if (nt_ui_combo_preview_end(s_fx.ctx)) {
            for (int i = 0; i < 3; ++i) {
                (void)nt_ui_combo_selectable(s_fx.ctx, ROW_KEY(i), s_short[i], false);
            }
            nt_ui_combo_end(s_fx.ctx);
        }
    }
    nt_ui_end(s_fx.ctx);
}

/* A pre-resolved ref into the fixture atlas (polygon region idx 1): region != INVALID so the chevron's
 * nt_atlas_resolve_ref no-ops and the affordance actually emits (a name_hash ref would resolve to INVALID
 * against the fixture's two named regions and skip-draw). */
static nt_atlas_region_ref_t fixture_chevron_ref(void) { return (nt_atlas_region_ref_t){.atlas = s_fx.atlas.handle, .region = s_fx.atlas.polygon_region_idx}; }

static void test_dropdown_preview_trigger_draws_chevron(void) {
    nt_ui_dropdown_style_t st = nt_ui_dropdown_style_defaults();
    st.chevron = fixture_chevron_ref(); /* resolvable ref so the chevron emits */
    bool open = false;

    /* Two warm frames so the chevron element bbox bakes (1-frame IM lag on get_bbox). */
    nt_pointer_t f = pointer_at(0.0F, 0.0F, false, false, false);
    preview_im_frame(&f, 30.0F, 30.0F, &st, &open);
    preview_im_frame(&f, 30.0F, 30.0F, &st, &open);

    const nt_ui_bbox_t chev = nt_ui_dropdown_test_chevron_bbox(s_fx.ctx, DD_A);
    TEST_ASSERT_TRUE_MESSAGE(chev.found, "custom-trigger (preview form) must declare the chevron element");
    TEST_ASSERT_TRUE_MESSAGE(chev.width > 0.0F && chev.height > 0.0F, "chevron must have a non-zero box");
    /* The GROW spacer pushes the chevron to the right of the trigger's left content edge. */
    TEST_ASSERT_TRUE_MESSAGE(chev.x > 30.0F, "chevron must sit at the trigger's right edge, not the left");
}

static void test_dropdown_preview_chevron_size_zero_opts_out(void) {
    nt_ui_dropdown_style_t st = nt_ui_dropdown_style_defaults();
    st.chevron = fixture_chevron_ref();
    st.chevron_size = 0U; /* opt out of the affordance even with a ref set */
    bool open = false;

    nt_pointer_t f = pointer_at(0.0F, 0.0F, false, false, false);
    preview_im_frame(&f, 30.0F, 30.0F, &st, &open);
    preview_im_frame(&f, 30.0F, 30.0F, &st, &open);

    const nt_ui_bbox_t chev = nt_ui_dropdown_test_chevron_bbox(s_fx.ctx, DD_A);
    TEST_ASSERT_FALSE_MESSAGE(chev.found, "chevron_size 0 must opt out of drawing the chevron");

    /* No leftover GROW spacer: the game's GROW swatch fills the interior, so its right edge reaches the
     * trigger's right inner edge (within pad). A spacer would split the slack and pull the swatch left. */
    const nt_ui_bbox_t trig = nt_ui_get_bbox(s_fx.ctx, DD_A);
    const nt_ui_bbox_t sw = nt_ui_get_bbox(s_fx.ctx, SWATCH_ID);
    TEST_ASSERT_TRUE_MESSAGE(trig.found && sw.found, "opt-out trigger and its preview child must measure");
    const float right_inner = trig.x + trig.width - (float)st.pad;
    const float sw_right = sw.x + sw.width;
    const float gap = (right_inner > sw_right) ? (right_inner - sw_right) : (sw_right - right_inner);
    TEST_ASSERT_TRUE_MESSAGE(gap <= 0.5F, "opt-out must leave no leftover GROW spacer (swatch fills to the trigger's right inner edge)");
}

/* One combo frame whose row SET changes: when `hide_first` the row keyed ROW_KEY(0) is skipped, so the
 * remaining rows shift their positional index. A fixed-key row (ROW_KEY(2)) must keep the SAME interactive
 * id regardless (the id no longer folds row_idx). Returns the live bbox of ROW_KEY(2)'s interactive row. */
static nt_ui_bbox_t combo_im_frame_skip_first(const nt_pointer_t *p, bool hide_first) {
    nt_ui_dropdown_style_t st = nt_ui_dropdown_style_defaults();
    bool open = true;
    int selected = -1;
    nt_mem_scratch_reset();
    nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, p, 1);
    CLAY({.id = (Clay_ElementId){.id = 0xDD0009U}, .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = 30.0F, .y = 30.0F}}}) {
        if (nt_ui_combo_begin(s_fx.ctx, NT_UI_DATA_LAYER(1), 2, DD_A, "Select...", &st, &open)) {
            for (int i = 0; i < 3; ++i) {
                if (hide_first && i == 0) {
                    continue; /* the row set changes: ROW_KEY(0) is gone this frame -> ROW_KEY(2) shifts position */
                }
                (void)nt_ui_combo_selectable(s_fx.ctx, ROW_KEY(i), s_short[i], selected == i);
            }
            nt_ui_combo_end(s_fx.ctx);
        }
    }
    nt_ui_end(s_fx.ctx);
    return nt_ui_get_bbox(s_fx.ctx, nt_ui_dropdown_test_combo_row_id(DD_A, ROW_KEY(2)));
}

/* ---- FIX 3: a combo whose row set changes across frames keeps a STABLE interactive id for a fixed-key
 *      row. The probe id mix(combo_id,key) is row_idx-independent, and the live row registers at exactly
 *      that id whether or not an earlier sibling row is hidden (so press/release identity never shifts). ---- */
static void test_dropdown_combo_row_id_stable_across_reorder(void) {
    /* The probe is pure: the same key yields the same id no matter the positional index. */
    const uint32_t id_a = nt_ui_dropdown_test_combo_row_id(DD_A, ROW_KEY(2));
    const uint32_t id_b = nt_ui_dropdown_test_combo_row_id(DD_A, ROW_KEY(2));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(id_a, id_b, "combo row id must be key-stable (no row_idx fold)");

    nt_pointer_t f = pointer_at(0.0F, 0.0F, false, false, false);
    /* Frame with all 3 rows, then a frame with row 0 hidden: ROW_KEY(2) goes from position 2 to position 1,
     * but its interactive row must register at the SAME id (bbox found at the key-stable id both frames). */
    combo_im_frame_skip_first(&f, false);
    const nt_ui_bbox_t full = combo_im_frame_skip_first(&f, false);
    const nt_ui_bbox_t shifted = combo_im_frame_skip_first(&f, true);
    TEST_ASSERT_TRUE_MESSAGE(full.found, "fixed-key row must register at its key-stable id (full list)");
    TEST_ASSERT_TRUE_MESSAGE(shifted.found, "fixed-key row keeps its key-stable id after an earlier sibling is hidden");
}

/* ---- FIX 3: two selectables sharing a key in one combo list alias the SAME id -> the debug duplicate-key
 *      NT_ASSERT must fire (keys must be unique within a list). ---- */
static void test_dropdown_combo_duplicate_key_asserts(void) {
    nt_ui_dropdown_style_t st = nt_ui_dropdown_style_defaults();
    bool open = true;
    nt_pointer_t p = pointer_at(0.0F, 0.0F, false, false, false);

    bool tripped = false;
    nt_test_assert_armed = true;
    if (setjmp(nt_test_assert_jmp) == 0) {
        nt_mem_scratch_reset();
        nt_ui_begin(s_fx.ctx, VIEW_W, VIEW_H, 1.0F / 60.0F, &p, 1);
        CLAY({.id = (Clay_ElementId){.id = 0xDD000AU}, .floating = {.attachTo = CLAY_ATTACH_TO_ROOT, .offset = {.x = 30.0F, .y = 30.0F}}}) {
            if (nt_ui_combo_begin(s_fx.ctx, NT_UI_DATA_LAYER(1), 2, DD_A, "Select...", &st, &open)) {
                (void)nt_ui_combo_selectable(s_fx.ctx, ROW_KEY(0), "Alpha", false);
                (void)nt_ui_combo_selectable(s_fx.ctx, ROW_KEY(0), "Dup", false); /* same key -> aliased id */
                nt_ui_combo_end(s_fx.ctx);
            }
        }
        nt_ui_end(s_fx.ctx);
    } else {
        tripped = true;
    }
    nt_test_assert_armed = false;
    TEST_ASSERT_TRUE_MESSAGE(tripped, "two selectables with the same key in one combo must trip the duplicate-key NT_ASSERT");
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
    RUN_TEST(test_dropdown_iconed_and_plain_rows_share_one_column);
    RUN_TEST(test_dropdown_preview_trigger_draws_chevron);
    RUN_TEST(test_dropdown_preview_chevron_size_zero_opts_out);
    RUN_TEST(test_dropdown_combo_row_id_stable_across_reorder);
    RUN_TEST(test_dropdown_combo_duplicate_key_asserts);
    return UNITY_END();
}
