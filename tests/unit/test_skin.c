/* System and engine headers before Unity: <stdnoreturn.h> and the Windows SDK
 * clash over __declspec(noreturn) in the other order. */
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "anim/nt_anim.h"
#include "math/nt_math.h"
#include "test_helpers/anim_rig.h"

#include "unity.h"

#include "test_helpers/nt_assert_trap.h"

enum { JOINT_ARM = 3, JOINT_HAND = 4, JOINT_TAIL1 = 7 };
/* hand is palette entry 3 of binding A ({0,1,3,4,5}) and of binding B ({6,7,8,4}). */
enum { HAND_ENTRY_A = 3, HAND_ENTRY_B = 3 };

static anim_rig_t g_rig;
static nt_skin_binding_t g_a;
static nt_skin_binding_t g_b;

void setUp(void) {
    anim_rig_asymmetric(&g_rig);
    anim_rig_bindings(&g_rig, &g_a, &g_b);
}

void tearDown(void) {}

/* Bind pose with arm and tail1 rotated: a pose no inverse bind cancels. */
static void make_edited_pose(nt_anim_trs_t *local) {
    memcpy(local, g_rig.bind, (size_t)ANIM_RIG_JOINT_COUNT * sizeof(nt_anim_trs_t));
    anim_rig_set_axis_angle(&local[JOINT_ARM], 0.0F, 0.0F, 1.0F, 47.0F);
    anim_rig_set_axis_angle(&local[JOINT_TAIL1], 0.0F, 1.0F, 0.0F, -80.0F);
}

static bool is_identity(const nt_anim_mat34_t *m, float tol) {
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 4; ++c) {
            const float expected = (r == c) ? 1.0F : 0.0F;
            if (!(fabsf(m->r[r][c] - expected) <= tol)) {
                return false;
            }
        }
    }
    return true;
}

/* ---- Palette build ---- */

void test_palette_at_bind_pose_is_identity(void) {
    nt_anim_mat34_t model[ANIM_RIG_JOINT_COUNT];
    nt_anim_fk(&g_rig.skel, g_rig.bind, model, 0, ANIM_RIG_JOINT_COUNT);

    /* The fixture's binds must be non-identity, or identity palettes prove nothing. */
    bool any_non_identity = false;
    for (uint16_t p = 0; p < g_a.palette_count; ++p) {
        any_non_identity = any_non_identity || !is_identity(&g_a.inverse_bind[p], 1e-5F);
    }
    TEST_ASSERT_TRUE(any_non_identity);

    nt_anim_mat34_t palette_a[ANIM_RIG_PALETTE_A_COUNT];
    nt_skin_palette_build(&g_a, model, ANIM_RIG_JOINT_COUNT, palette_a, ANIM_RIG_PALETTE_A_COUNT);
    for (uint16_t p = 0; p < g_a.palette_count; ++p) {
        TEST_ASSERT_TRUE(is_identity(&palette_a[p], 1e-5F));
    }

    nt_anim_mat34_t palette_b[ANIM_RIG_PALETTE_B_COUNT];
    nt_skin_palette_build(&g_b, model, ANIM_RIG_JOINT_COUNT, palette_b, ANIM_RIG_PALETTE_B_COUNT);
    for (uint16_t p = 0; p < g_b.palette_count; ++p) {
        TEST_ASSERT_TRUE(is_identity(&palette_b[p], 1e-5F));
    }
}

void test_shared_joint_matches_in_both_bindings(void) {
    nt_anim_trs_t local[ANIM_RIG_JOINT_COUNT];
    make_edited_pose(local);

    nt_anim_mat34_t model[ANIM_RIG_JOINT_COUNT];
    nt_anim_fk(&g_rig.skel, local, model, 0, ANIM_RIG_JOINT_COUNT);

    nt_anim_mat34_t palette_a[ANIM_RIG_PALETTE_A_COUNT];
    nt_skin_palette_build(&g_a, model, ANIM_RIG_JOINT_COUNT, palette_a, ANIM_RIG_PALETTE_A_COUNT);
    nt_anim_mat34_t palette_b[ANIM_RIG_PALETTE_B_COUNT];
    nt_skin_palette_build(&g_b, model, ANIM_RIG_JOINT_COUNT, palette_b, ANIM_RIG_PALETTE_B_COUNT);

    TEST_ASSERT_EQUAL_UINT16(JOINT_HAND, g_a.remap[HAND_ENTRY_A]);
    TEST_ASSERT_EQUAL_UINT16(JOINT_HAND, g_b.remap[HAND_ENTRY_B]);
    /* Same inputs through the same kernel: bit-identical, not just close. */
    TEST_ASSERT_EQUAL_MEMORY(&palette_a[HAND_ENTRY_A], &palette_b[HAND_ENTRY_B], sizeof(nt_anim_mat34_t));

    /* Equal to each other proves nothing unless both are also the right matrix. */
    mat4 ref_pose[ANIM_RIG_JOINT_COUNT];
    anim_rig_ref_fk(local, ref_pose);
    mat4 ref_bind[ANIM_RIG_JOINT_COUNT];
    anim_rig_ref_fk(g_rig.bind, ref_bind);

    mat4 inv;
    glm_mat4_inv(ref_bind[JOINT_HAND], inv);
    mat4 expected;
    glm_mat4_mul(ref_pose[JOINT_HAND], inv, expected);

    anim_rig_assert_mat34_equals_mat4(&palette_a[HAND_ENTRY_A], expected, 1e-5F);
    anim_rig_assert_mat34_equals_mat4(&palette_b[HAND_ENTRY_B], expected, 1e-5F);
}

/* An empty binding is a legal binding: it writes nothing and must not trap. */
void test_empty_palette_writes_nothing(void) {
    nt_anim_mat34_t model[ANIM_RIG_JOINT_COUNT];
    nt_anim_fk(&g_rig.skel, g_rig.bind, model, 0, ANIM_RIG_JOINT_COUNT);

    nt_anim_mat34_t out[1];
    memset(out, 0x5A, sizeof(out));
    nt_anim_mat34_t poison[1];
    memset(poison, 0x5A, sizeof(poison));

    nt_skin_binding_t empty = g_a;
    empty.palette_count = 0;
    nt_skin_palette_build(&empty, model, ANIM_RIG_JOINT_COUNT, out, 0);

    TEST_ASSERT_EQUAL_MEMORY(poison, out, sizeof(out));
}

void test_palette_matches_cglm_reference(void) {
    nt_anim_trs_t local[ANIM_RIG_JOINT_COUNT];
    make_edited_pose(local);

    nt_anim_mat34_t model[ANIM_RIG_JOINT_COUNT];
    nt_anim_fk(&g_rig.skel, local, model, 0, ANIM_RIG_JOINT_COUNT);

    mat4 ref_pose[ANIM_RIG_JOINT_COUNT];
    anim_rig_ref_fk(local, ref_pose);
    mat4 ref_bind[ANIM_RIG_JOINT_COUNT];
    anim_rig_ref_fk(g_rig.bind, ref_bind);

    const nt_skin_binding_t *bindings[] = {&g_a, &g_b};
    for (size_t k = 0; k < sizeof(bindings) / sizeof(bindings[0]); ++k) {
        const nt_skin_binding_t *binding = bindings[k];
        nt_anim_mat34_t palette[ANIM_RIG_PALETTE_A_COUNT];
        nt_skin_palette_build(binding, model, ANIM_RIG_JOINT_COUNT, palette, ANIM_RIG_PALETTE_A_COUNT);

        for (uint16_t p = 0; p < binding->palette_count; ++p) {
            const uint16_t j = binding->remap[p];
            mat4 inv;
            glm_mat4_inv(ref_bind[j], inv);
            mat4 expected;
            glm_mat4_mul(ref_pose[j], inv, expected);
            anim_rig_assert_mat34_equals_mat4(&palette[p], expected, 1e-5F);
        }
    }
}

/* ---- Contracts ---- */

#if NT_ASSERT_MODE == NT_ASSERT_FULL
void test_palette_build_traps_on_small_capacity(void) {
    nt_anim_mat34_t model[ANIM_RIG_JOINT_COUNT];
    nt_anim_fk(&g_rig.skel, g_rig.bind, model, 0, ANIM_RIG_JOINT_COUNT);
    nt_anim_mat34_t palette[ANIM_RIG_PALETTE_A_COUNT];

    NT_TEST_EXPECT_ASSERT(nt_skin_palette_build(&g_a, model, ANIM_RIG_JOINT_COUNT, palette, ANIM_RIG_PALETTE_A_COUNT - 1));
}

/* Writing the palette into the model pose would feed later entries their own
 * output; the range guard is per call, not per entry. */
void test_palette_build_traps_on_overlapping_output(void) {
    nt_anim_mat34_t model[ANIM_RIG_JOINT_COUNT];
    nt_anim_fk(&g_rig.skel, g_rig.bind, model, 0, ANIM_RIG_JOINT_COUNT);

    NT_TEST_EXPECT_ASSERT(nt_skin_palette_build(&g_a, model, ANIM_RIG_JOINT_COUNT, &model[1], ANIM_RIG_PALETTE_A_COUNT));
}
#endif

#if NT_ANIM_CHECKS && (NT_ASSERT_MODE == NT_ASSERT_FULL)
void test_palette_build_traps_on_out_of_range_remap(void) {
    nt_anim_mat34_t model[ANIM_RIG_JOINT_COUNT];
    nt_anim_fk(&g_rig.skel, g_rig.bind, model, 0, ANIM_RIG_JOINT_COUNT);
    nt_anim_mat34_t palette[ANIM_RIG_PALETTE_A_COUNT];

    uint16_t remap[ANIM_RIG_PALETTE_A_COUNT];
    memcpy(remap, g_a.remap, sizeof(remap));
    remap[2] = ANIM_RIG_JOINT_COUNT;

    nt_skin_binding_t broken = g_a;
    broken.remap = remap;
    NT_TEST_EXPECT_ASSERT(nt_skin_palette_build(&broken, model, ANIM_RIG_JOINT_COUNT, palette, ANIM_RIG_PALETTE_A_COUNT));
}
#endif

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_palette_at_bind_pose_is_identity);
    RUN_TEST(test_shared_joint_matches_in_both_bindings);
    RUN_TEST(test_empty_palette_writes_nothing);
    RUN_TEST(test_palette_matches_cglm_reference);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_palette_build_traps_on_small_capacity);
    RUN_TEST(test_palette_build_traps_on_overlapping_output);
#endif
#if NT_ANIM_CHECKS && (NT_ASSERT_MODE == NT_ASSERT_FULL)
    RUN_TEST(test_palette_build_traps_on_out_of_range_remap);
#endif
    return UNITY_END();
}
