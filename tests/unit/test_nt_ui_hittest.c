/* RED scaffold for the transform-aware hit-test (D-56-07, supersedes D-54-08).
 *
 * Wave 0 (Plan 01): registration + RED status only. The real assertions land
 * in Plan 03 once the hit-test entry point exists (it inverse-transforms the
 * pointer by the declaration-time accumulated transform, then point-in-rect
 * against Clay's stable layout bbox). Each test body is a TEST_FAIL_MESSAGE
 * placeholder.
 *
 * Test design (RESEARCH 56 "Concrete unit-test design for the transform
 * hit-test", per AGENTS.md "asymmetric data that breaks on axis swap or flip"):
 *   - NON-square bbox 200x60 at a NON-origin position (x=150, y=80).
 *   - ASYMMETRIC transform: rotation = 30 deg, offset_x != offset_y (and/or
 *     non-uniform scale about the bbox center).
 *   - 4 probe points:
 *       (a) center                              → inside
 *       (b) just-inside a rotated corner        → inside
 *       (c) just-outside that corner            → outside
 *       (d) inside the AXIS-ALIGNED bbox but
 *           outside the ROTATED one             → outside
 *     (d) proves the inverse-transform actually runs (not plain AABB).
 *   Hit-test stays in Clay Y-DOWN space with NON-negated rotation
 *   (the walker's Y-flip / -rotation are render-only — do NOT copy them). */

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

/* ---- Test 1: untransformed AABB hit (baseline before transforms) ---- */
static void test_hittest_axis_aligned_baseline(void) {
    /* Plan 03: declare 200x60 bbox at (150,80); probe center inside, far
     * corner outside; assert hovered matches plain AABB with no transform.
     * References the hit-test entry point (not yet implemented). */
    (void)float_near;
    TEST_FAIL_MESSAGE("Plan 03 (Wave 1): transform hit-test not implemented");
}

/* ---- Test 2: rotated + offset transform, 4 asymmetric probe points ---- */
static void test_hittest_rotated_asymmetric_probes(void) {
    /* Plan 03: push asymmetric transform (rot=30deg, offset_x!=offset_y) about
     * the prev-frame bbox center; assert probes (a),(b) hovered and (c),(d) not.
     * (d) = inside axis-aligned bbox / outside rotated → proves inverse-affine.
     * References the hit-test entry point (not yet implemented). */
    TEST_FAIL_MESSAGE("Plan 03 (Wave 1): transform hit-test not implemented");
}

/* ---- Test 3: hit-test stays in Clay Y-down space (no walker Y-flip) ---- */
static void test_hittest_no_render_y_flip(void) {
    /* Plan 03: offset_y-only transform; a probe above/below the element must
     * follow Clay Y-down, NOT the walker's GL Y-up flip. References the
     * hit-test entry point (not yet implemented). */
    TEST_FAIL_MESSAGE("Plan 03 (Wave 1): transform hit-test not implemented");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_hittest_axis_aligned_baseline);
    RUN_TEST(test_hittest_rotated_asymmetric_probes);
    RUN_TEST(test_hittest_no_render_y_flip);
    return UNITY_END();
}
