/* Direct-mapped per-id state-transition anim cache (D-56-15/16/17).
 *
 * Plan 02 (Wave 1): turns the Plan-01 RED scaffold GREEN. The cache follows the
 * Phase-51 font-measure-cache precedent: fixed power-of-2 array, key = Clay
 * uint32 id directly (no hashing), replace-on-collision, no LRU/eviction.
 *
 * Test design (RESEARCH 56 New Work Item 3 + CONTEXT D-56-15/16):
 *   - Lerp convergence: cur += (target - cur) * clampf(transition_speed*dt,0,1).
 *       transition_speed == 0 -> instant (cur == target).
 *       transition_speed  > 0 -> cur approaches target over N dt steps
 *       (monotone, never overshoots).
 *   - First touch of a slot initializes cur = target (no animate-from-zero flash).
 *   - Replace-on-collision: two ids mapping to the same slot via
 *       id & (NT_UI_ANIM_SLOTS - 1) -> the second overwrites the first,
 *       no crash, no eviction scan.
 *
 * frame_dt is set directly on the fixture ctx (nt_ui_anim only reads it; no Clay
 * state needed). float_near is used because Unity float macros are excluded
 * (UNITY_EXCLUDE_FLOAT, see TESTING.md). */

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
    /* First touch seeds cur = target (scale 1.0). */
    nt_ui_anim_target_t a = {.scale = 1.0F, .off_x = 0.0F, .off_y = 0.0F, .opacity = 1.0F, .tint_t = 0.0F};
    (void)nt_ui_anim(s_fx.ctx, id, &a, 0.0F);
    /* Now request a different target with speed 0 -> snaps same frame. */
    nt_ui_anim_target_t b = {.scale = 2.0F, .off_x = 5.0F, .off_y = -3.0F, .opacity = 0.25F, .tint_t = 1.0F};
    const nt_ui_anim_interaction_t *r = nt_ui_anim(s_fx.ctx, id, &b, 0.0F);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_TRUE(float_near(r->scale, 2.0F, 1e-6F));
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
    nt_ui_anim_target_t a = {.scale = 1.0F, .off_x = 0.0F, .off_y = 0.0F, .opacity = 1.0F, .tint_t = 0.0F};
    (void)nt_ui_anim(s_fx.ctx, id, &a, 10.0F);
    /* Drive toward target B (scale 0.95) for ~30 frames at speed 10, dt 1/60. */
    nt_ui_anim_target_t b = {.scale = 0.95F, .off_x = 0.0F, .off_y = 0.0F, .opacity = 1.0F, .tint_t = 0.0F};
    float prev = 1.0F;
    for (int i = 0; i < 30; i++) {
        const nt_ui_anim_interaction_t *r = nt_ui_anim(s_fx.ctx, id, &b, 10.0F);
        TEST_ASSERT_NOT_NULL(r);
        /* Monotonic decreasing toward 0.95, never overshoots below it. */
        TEST_ASSERT_TRUE(r->scale <= prev + 1e-6F);
        TEST_ASSERT_TRUE(r->scale >= 0.95F - 1e-6F);
        prev = r->scale;
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
    nt_ui_anim_target_t t = {.scale = 0.95F, .off_x = 7.0F, .off_y = 9.0F, .opacity = 0.5F, .tint_t = 0.75F};
    const nt_ui_anim_interaction_t *r = nt_ui_anim(s_fx.ctx, id, &t, 10.0F);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_TRUE(float_near(r->scale, 0.95F, 1e-6F));
    TEST_ASSERT_TRUE(float_near(r->off_x, 7.0F, 1e-6F));
    TEST_ASSERT_TRUE(float_near(r->off_y, 9.0F, 1e-6F));
    TEST_ASSERT_TRUE(float_near(r->opacity, 0.5F, 1e-6F));
    TEST_ASSERT_TRUE(float_near(r->tint_t, 0.75F, 1e-6F));
    TEST_ASSERT_TRUE(r->valid);
    TEST_ASSERT_EQUAL_UINT32(id, r->id);
}

/* ---- Test 4: replace-on-collision (two ids same slot, no crash) ---- */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_anim_replace_on_collision(void) {
    s_fx.ctx->frame_dt = 1.0F / 60.0F;
    /* id1 and id2 map to the same slot: (1) & 63 == (1 + 64) & 63. */
    const uint32_t id1 = 1U;
    const uint32_t id2 = 1U + NT_UI_ANIM_SLOTS;
    TEST_ASSERT_EQUAL_UINT32(id1 & (NT_UI_ANIM_SLOTS - 1U), id2 & (NT_UI_ANIM_SLOTS - 1U));

    nt_ui_anim_target_t t1 = {.scale = 1.0F, .off_x = 0.0F, .off_y = 0.0F, .opacity = 1.0F, .tint_t = 0.0F};
    const nt_ui_anim_interaction_t *r1 = nt_ui_anim(s_fx.ctx, id1, &t1, 0.0F);
    TEST_ASSERT_NOT_NULL(r1);
    TEST_ASSERT_EQUAL_UINT32(id1, r1->id);
    TEST_ASSERT_TRUE(float_near(r1->scale, 1.0F, 1e-6F));

    /* id2 collides -> slot re-keyed to id2, cur re-seeded to id2's target,
     * no crash, no eviction scan. */
    nt_ui_anim_target_t t2 = {.scale = 0.5F, .off_x = 2.0F, .off_y = 0.0F, .opacity = 1.0F, .tint_t = 0.0F};
    const nt_ui_anim_interaction_t *r2 = nt_ui_anim(s_fx.ctx, id2, &t2, 10.0F);
    TEST_ASSERT_NOT_NULL(r2);
    TEST_ASSERT_EQUAL_PTR(r1, r2); /* same physical slot */
    TEST_ASSERT_EQUAL_UINT32(id2, r2->id);
    TEST_ASSERT_TRUE(float_near(r2->scale, 0.5F, 1e-6F)); /* re-seeded, not eased from 1.0 */
    TEST_ASSERT_TRUE(float_near(r2->off_x, 2.0F, 1e-6F));

    /* Collision is mutual: re-accessing id1 re-seeds the slot to id1 again. */
    const nt_ui_anim_interaction_t *r3 = nt_ui_anim(s_fx.ctx, id1, &t1, 10.0F);
    TEST_ASSERT_NOT_NULL(r3);
    TEST_ASSERT_EQUAL_UINT32(id1, r3->id);
    TEST_ASSERT_TRUE(float_near(r3->scale, 1.0F, 1e-6F));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_anim_instant_when_speed_zero);
    RUN_TEST(test_anim_eases_toward_target);
    RUN_TEST(test_anim_first_touch_no_flash);
    RUN_TEST(test_anim_replace_on_collision);
    return UNITY_END();
}
