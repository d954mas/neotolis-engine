/* Per-id state-transition anim cache. Open addressing with linear probing
 * (NT_UI_ANIM_PROBE_MAX from base bucket); collision coexists in adjacent
 * slots, eviction only when all probes are taken by other ids.
 *
 * Tests: lerp convergence (cur += (target - cur) * k), first-touch no-flash
 * snap, open-address coexistence, eviction-after-probes. frame_dt is set
 * directly on the fixture; no Clay state needed. */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>

#include "clay.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_anim.h"
#include "ui/nt_ui_internal.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

void setUp(void) {
    nt_test_assert_install();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

/* Unity float macros are excluded in this build (UNITY_EXCLUDE_FLOAT). */
static bool float_near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

/* ---- Test 1: transition_speed == 0 -> instant snap to target ---- */
static void test_anim_instant_when_speed_zero(void) {
    s_fx.ctx->frame_dt = 1.0F / 60.0F;
    const uint32_t id = 0x1234U;
    /* First touch seeds cur = target (uniform scale 1.0). */
    nt_ui_anim_target_t a = {.scale_x = 1.0F, .scale_y = 1.0F, .scale_z = 1.0F, .opacity = 1.0F};
    (void)nt_ui_anim(s_fx.ctx, id, &a, 0.0F, 0.0F);
    /* Now request a different target with speed 0 -> snaps same frame. */
    nt_ui_anim_target_t b = {.scale_x = 2.0F, .scale_y = 2.0F, .scale_z = 1.0F, .off_x = 5.0F, .off_y = -3.0F, .opacity = 0.25F, .tint_t = 1.0F};
    const nt_ui_anim_interaction_t *r = nt_ui_anim(s_fx.ctx, id, &b, 0.0F, 0.0F);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_TRUE(float_near(r->scale_x, 2.0F, 1e-6F));
    TEST_ASSERT_TRUE(float_near(r->off_x, 5.0F, 1e-6F));
    TEST_ASSERT_TRUE(float_near(r->off_y, -3.0F, 1e-6F));
    TEST_ASSERT_TRUE(float_near(r->opacity, 0.25F, 1e-6F));
    TEST_ASSERT_TRUE(float_near(r->tint_t, 1.0F, 1e-6F));
}

/* ---- Test 2: transition_speed > 0 -> eased convergence over dt steps ---- */
static void test_anim_eases_toward_target(void) {
    s_fx.ctx->frame_dt = 1.0F / 60.0F;
    const uint32_t id = 0x2222U;
    /* Seed at target A (scale 1.0). */
    nt_ui_anim_target_t a = {.scale_x = 1.0F, .scale_y = 1.0F, .scale_z = 1.0F, .opacity = 1.0F};
    (void)nt_ui_anim(s_fx.ctx, id, &a, 10.0F, 0.0F);
    /* Drive toward target B (scale 0.95) for ~30 frames at speed 10, dt 1/60. */
    nt_ui_anim_target_t b = {.scale_x = 0.95F, .scale_y = 0.95F, .scale_z = 1.0F, .opacity = 1.0F};
    float prev = 1.0F;
    for (int i = 0; i < 30; i++) {
        const nt_ui_anim_interaction_t *r = nt_ui_anim(s_fx.ctx, id, &b, 10.0F, 0.0F);
        TEST_ASSERT_NOT_NULL(r);
        /* Monotonic decreasing toward 0.95, never overshoots below it. */
        TEST_ASSERT_TRUE(r->scale_x <= prev + 1e-6F);
        TEST_ASSERT_TRUE(r->scale_x >= 0.95F - 1e-6F);
        prev = r->scale_x;
    }
    /* Converged within eps after 30 frames. */
    TEST_ASSERT_TRUE(float_near(prev, 0.95F, 0.01F));
}

/* ---- Test 3: first touch initializes cur = target (no flash) ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_anim_first_touch_no_flash(void) {
    s_fx.ctx->frame_dt = 1.0F / 60.0F;
    const uint32_t id = 0x3333U;
    /* A freshly-seen id with speed > 0 must still START at target (no ramp
     * from zero/one), so the very first frame shows no flash. */
    nt_ui_anim_target_t t = {.scale_x = 0.95F, .scale_y = 0.95F, .scale_z = 1.0F, .off_x = 7.0F, .off_y = 9.0F, .opacity = 0.5F, .tint_t = 0.75F};
    const nt_ui_anim_interaction_t *r = nt_ui_anim(s_fx.ctx, id, &t, 10.0F, 0.0F);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_TRUE(float_near(r->scale_x, 0.95F, 1e-6F));
    TEST_ASSERT_TRUE(float_near(r->off_x, 7.0F, 1e-6F));
    TEST_ASSERT_TRUE(float_near(r->off_y, 9.0F, 1e-6F));
    TEST_ASSERT_TRUE(float_near(r->opacity, 0.5F, 1e-6F));
    TEST_ASSERT_TRUE(float_near(r->tint_t, 0.75F, 1e-6F));
    TEST_ASSERT_TRUE(r->valid);
    TEST_ASSERT_EQUAL_UINT32(id, r->id);
}

/* ---- Test 4: open addressing coexists two ids that hash to same base ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_anim_open_address_coexists(void) {
    s_fx.ctx->frame_dt = 1.0F / 60.0F;
    /* id1, id2 same base bucket — linear probe gives them adjacent slots. */
    const uint32_t id1 = 1U;
    const uint32_t id2 = 1U + NT_UI_ANIM_SLOTS;
    TEST_ASSERT_EQUAL_UINT32(id1 & (NT_UI_ANIM_SLOTS - 1U), id2 & (NT_UI_ANIM_SLOTS - 1U));

    nt_ui_anim_target_t t1 = {.scale_x = 1.0F, .scale_y = 1.0F, .scale_z = 1.0F, .opacity = 1.0F};
    const nt_ui_anim_interaction_t *r1 = nt_ui_anim(s_fx.ctx, id1, &t1, 0.0F, 0.0F);
    TEST_ASSERT_EQUAL_UINT32(id1, r1->id);

    nt_ui_anim_target_t t2 = {.scale_x = 0.5F, .scale_y = 0.5F, .scale_z = 1.0F, .off_x = 2.0F, .opacity = 1.0F};
    const nt_ui_anim_interaction_t *r2 = nt_ui_anim(s_fx.ctx, id2, &t2, 10.0F, 0.0F);
    TEST_ASSERT_EQUAL_UINT32(id2, r2->id);
    TEST_ASSERT_TRUE_MESSAGE(r1 != r2, "open addressing must land id2 in a different slot than id1");
    TEST_ASSERT_TRUE(float_near(r1->scale_x, 1.0F, 1e-6F)); /* id1 untouched */
    TEST_ASSERT_TRUE(float_near(r2->scale_x, 0.5F, 1e-6F));

    /* Re-access id1: must keep its eased state, NOT snap. */
    const nt_ui_anim_interaction_t *r3 = nt_ui_anim(s_fx.ctx, id1, &t1, 10.0F, 0.0F);
    TEST_ASSERT_EQUAL_PTR(r1, r3);
    TEST_ASSERT_EQUAL_UINT32(id1, r3->id);
    TEST_ASSERT_TRUE(float_near(r3->scale_x, 1.0F, 1e-6F));
}

/* All NT_UI_ANIM_PROBE_MAX consecutive slots full with other ids → evict base. */
static void test_anim_evicts_when_probes_exhausted(void) {
    s_fx.ctx->frame_dt = 1.0F / 60.0F;
    nt_ui_anim_target_t t = {.scale_x = 1.0F, .scale_y = 1.0F, .scale_z = 1.0F, .opacity = 1.0F};
    /* Fill NT_UI_ANIM_PROBE_MAX consecutive slots starting at bucket 1. */
    for (uint32_t k = 0; k < NT_UI_ANIM_PROBE_MAX; ++k) {
        const uint32_t id = 1U + (k * NT_UI_ANIM_SLOTS);
        (void)nt_ui_anim(s_fx.ctx, id, &t, 0.0F, 0.0F);
    }
    /* New id hashing to same base: all probes occupied → evict base. */
    const uint32_t bumped = 1U + (NT_UI_ANIM_PROBE_MAX * NT_UI_ANIM_SLOTS);
    nt_ui_anim_target_t t_new = {.scale_x = 0.25F, .scale_y = 0.25F, .scale_z = 1.0F, .opacity = 1.0F};
    const nt_ui_anim_interaction_t *r = nt_ui_anim(s_fx.ctx, bumped, &t_new, 10.0F, 0.0F);
    TEST_ASSERT_EQUAL_UINT32(bumped, r->id);
    TEST_ASSERT_TRUE(float_near(r->scale_x, 0.25F, 1e-6F));
}

/* ---- Test 6: 3D rotation_y eased convergence (single-axis dodges gimbal lock per nt_ui_anim.h doc). ---- */
static void test_anim_rot_y_eases_to_target(void) {
    s_fx.ctx->frame_dt = 1.0F / 60.0F;
    const uint32_t id = 0x4444U;
    nt_ui_anim_target_t seed = {.scale_x = 1.0F, .scale_y = 1.0F, .scale_z = 1.0F, .opacity = 1.0F};
    (void)nt_ui_anim(s_fx.ctx, id, &seed, 10.0F, 0.0F);
    /* Drive rot_y toward π/2 (card-flip half-turn). */
    nt_ui_anim_target_t flip = {.scale_x = 1.0F, .scale_y = 1.0F, .scale_z = 1.0F, .rot_y = 1.5707963F, .opacity = 1.0F};
    float prev = 0.0F;
    for (int i = 0; i < 40; ++i) {
        const nt_ui_anim_interaction_t *r = nt_ui_anim(s_fx.ctx, id, &flip, 10.0F, 0.0F);
        TEST_ASSERT_TRUE(r->rot_y >= prev - 1e-6F);       /* monotonic increasing */
        TEST_ASSERT_TRUE(r->rot_y <= 1.5707963F + 1e-3F); /* no overshoot */
        prev = r->rot_y;
    }
    TEST_ASSERT_TRUE(float_near(prev, 1.5707963F, 0.05F));
}

/* ---- Test 7: 3D off_z + scale_z snap on instant + read back. ---- */
static void test_anim_z_axis_fields_snap(void) {
    s_fx.ctx->frame_dt = 1.0F / 60.0F;
    const uint32_t id = 0x5555U;
    nt_ui_anim_target_t t = {.scale_x = 1.0F, .scale_y = 1.0F, .scale_z = 2.5F, .off_z = -5.0F, .opacity = 1.0F};
    const nt_ui_anim_interaction_t *r = nt_ui_anim(s_fx.ctx, id, &t, 0.0F, 0.0F);
    TEST_ASSERT_TRUE(float_near(r->scale_z, 2.5F, 1e-6F));
    TEST_ASSERT_TRUE(float_near(r->off_z, -5.0F, 1e-6F));
}

/* ---- Test 8: value_speed > 0 -> value_t eases across frames (not instant). ---- */
static void test_anim_value_t_eases_when_value_speed_positive(void) {
    s_fx.ctx->frame_dt = 1.0F / 60.0F;
    const uint32_t id = 0x6666U;
    /* Seed at value_t = 0 (first touch snaps both state + value). */
    nt_ui_anim_target_t seed = {.scale_x = 1.0F, .scale_y = 1.0F, .scale_z = 1.0F, .opacity = 1.0F, .value_t = 0.0F};
    (void)nt_ui_anim(s_fx.ctx, id, &seed, 10.0F, 10.0F);
    /* Drive value_t toward 1.0; must ramp, not jump. */
    nt_ui_anim_target_t drive = {.scale_x = 1.0F, .scale_y = 1.0F, .scale_z = 1.0F, .opacity = 1.0F, .value_t = 1.0F};
    const nt_ui_anim_interaction_t *r1 = nt_ui_anim(s_fx.ctx, id, &drive, 10.0F, 10.0F);
    TEST_ASSERT_TRUE_MESSAGE(r1->value_t > 0.0F, "value_speed>0: value_t must advance off 0");
    TEST_ASSERT_TRUE_MESSAGE(r1->value_t < 1.0F, "value_speed>0: value_t must NOT snap to target in one frame");
    float prev = r1->value_t;
    for (int i = 0; i < 40; ++i) {
        const nt_ui_anim_interaction_t *r = nt_ui_anim(s_fx.ctx, id, &drive, 10.0F, 10.0F);
        TEST_ASSERT_TRUE(r->value_t >= prev - 1e-6F); /* monotonic increasing */
        TEST_ASSERT_TRUE(r->value_t <= 1.0F + 1e-6F); /* no overshoot */
        prev = r->value_t;
    }
    TEST_ASSERT_TRUE(float_near(prev, 1.0F, 0.01F));
}

/* ---- Test 9: value_speed == 0 -> value_t snaps to target in one call. ---- */
static void test_anim_value_t_snaps_when_value_speed_zero(void) {
    s_fx.ctx->frame_dt = 1.0F / 60.0F;
    const uint32_t id = 0x7777U;
    nt_ui_anim_target_t seed = {.scale_x = 1.0F, .scale_y = 1.0F, .scale_z = 1.0F, .opacity = 1.0F, .value_t = 0.0F};
    (void)nt_ui_anim(s_fx.ctx, id, &seed, 10.0F, 0.0F);
    /* value_speed 0 with a moved target -> instant snap even on an existing slot. */
    nt_ui_anim_target_t jump = {.scale_x = 1.0F, .scale_y = 1.0F, .scale_z = 1.0F, .opacity = 1.0F, .value_t = 0.8F};
    const nt_ui_anim_interaction_t *r = nt_ui_anim(s_fx.ctx, id, &jump, 10.0F, 0.0F);
    TEST_ASSERT_TRUE(float_near(r->value_t, 0.8F, 1e-6F));
}

/* ---- Test 10: value_t and state fields ease on independent speeds. ----
 *      state_speed 0 (snap) + value_speed > 0 (ramp): scale snaps, value_t ramps. */
static void test_anim_value_t_independent_of_state_speed(void) {
    s_fx.ctx->frame_dt = 1.0F / 60.0F;
    const uint32_t id = 0x8888U;
    nt_ui_anim_target_t seed = {.scale_x = 1.0F, .scale_y = 1.0F, .scale_z = 1.0F, .opacity = 1.0F, .value_t = 0.0F};
    (void)nt_ui_anim(s_fx.ctx, id, &seed, 0.0F, 5.0F);
    nt_ui_anim_target_t drive = {.scale_x = 1.5F, .scale_y = 1.5F, .scale_z = 1.0F, .opacity = 1.0F, .value_t = 1.0F};
    const nt_ui_anim_interaction_t *r = nt_ui_anim(s_fx.ctx, id, &drive, 0.0F, 5.0F);
    /* state_speed 0 -> scale snaps to 1.5; value_speed>0 -> value_t still ramping. */
    TEST_ASSERT_TRUE(float_near(r->scale_x, 1.5F, 1e-6F));
    TEST_ASSERT_TRUE_MESSAGE(r->value_t > 0.0F && r->value_t < 1.0F, "value_t must ramp while state snaps");
}

/* ---- Test 11: button regression -- value_t=0, value_speed=0 leaves value_t==0
 *      and the existing 11-field eased behavior unchanged. ---- */
static void test_anim_button_value_t_zero_regression(void) {
    s_fx.ctx->frame_dt = 1.0F / 60.0F;
    const uint32_t id = 0x9999U;
    /* Mirror the button caller: value_t left 0 in tgt, value_speed 0. */
    nt_ui_anim_target_t seed = {.scale_x = 1.0F, .scale_y = 1.0F, .scale_z = 1.0F, .opacity = 1.0F};
    (void)nt_ui_anim(s_fx.ctx, id, &seed, 12.0F, 0.0F);
    nt_ui_anim_target_t pressed = {.scale_x = 0.95F, .scale_y = 0.95F, .scale_z = 1.0F, .opacity = 1.0F};
    float prev = 1.0F;
    for (int i = 0; i < 30; ++i) {
        const nt_ui_anim_interaction_t *r = nt_ui_anim(s_fx.ctx, id, &pressed, 12.0F, 0.0F);
        TEST_ASSERT_TRUE(float_near(r->value_t, 0.0F, 1e-6F)); /* value_t stays 0 */
        TEST_ASSERT_TRUE(r->scale_x <= prev + 1e-6F);          /* scale still eases */
        prev = r->scale_x;
    }
    TEST_ASSERT_TRUE(float_near(prev, 0.95F, 0.01F));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_anim_instant_when_speed_zero);
    RUN_TEST(test_anim_eases_toward_target);
    RUN_TEST(test_anim_first_touch_no_flash);
    RUN_TEST(test_anim_open_address_coexists);
    RUN_TEST(test_anim_evicts_when_probes_exhausted);
    RUN_TEST(test_anim_rot_y_eases_to_target);
    RUN_TEST(test_anim_z_axis_fields_snap);
    RUN_TEST(test_anim_value_t_eases_when_value_speed_positive);
    RUN_TEST(test_anim_value_t_snaps_when_value_speed_zero);
    RUN_TEST(test_anim_value_t_independent_of_state_speed);
    RUN_TEST(test_anim_button_value_t_zero_regression);
    return UNITY_END();
}
