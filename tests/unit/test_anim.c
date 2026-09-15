/* System and engine headers before Unity: <stdnoreturn.h> and the Windows SDK
 * clash over __declspec(noreturn) in the other order. */
#include <math.h>
#include <stddef.h>
#include <string.h>

#include "anim/nt_anim.h"
#include "math/nt_math.h"
#include "test_helpers/anim_rig.h"

#include "unity.h"

#include "test_helpers/nt_assert_trap.h"

/* Unity is built with UNITY_EXCLUDE_FLOAT, so float comparisons go through
 * fabsf like everywhere else in the suite. */
#define ASSERT_FLOAT_NEAR(expected, actual, tol) TEST_ASSERT_TRUE_MESSAGE(fabsf((expected) - (actual)) <= (tol), "float not within tolerance")

enum { JOINT_ROOT_A = 0, JOINT_SPINE = 1, JOINT_HELPER = 2, JOINT_ARM = 3, JOINT_HAND = 4, JOINT_LEG = 5, JOINT_ROOT_B = 6, JOINT_TAIL1 = 7, JOINT_TAIL2 = 8 };

static anim_rig_t g_rig;

void setUp(void) { anim_rig_asymmetric(&g_rig); }

void tearDown(void) {}

/* ---- cglm reference path ---- */

/* Reads t/q/s straight out of the AoS pose as vec3/versor: the ABI-alignment
 * exercise that Debug UBSan checks. */
static void ref_mat4_from_trs(nt_anim_trs_t *trs, mat4 out) {
    mat4 t;
    glm_translate_make(t, trs->t);
    mat4 r;
    glm_quat_mat4(trs->q, r);
    mat4 s;
    glm_scale_make(s, trs->s);
    mat4 tr;
    glm_mat4_mul(t, r, tr);
    glm_mat4_mul(tr, s, out);
}

static void ref_fk(nt_anim_trs_t *local, mat4 *out) {
    for (uint16_t j = 0; j < ANIM_RIG_JOINT_COUNT; ++j) {
        mat4 l;
        ref_mat4_from_trs(&local[j], l);
        const uint16_t p = g_rig.skel.parent[j];
        if (p == NT_ANIM_NO_PARENT) {
            glm_mat4_copy(l, out[j]);
        } else {
            glm_mat4_mul(out[p], l, out[j]);
        }
    }
}

static void assert_mat34_equals_mat4(const nt_anim_mat34_t *m34, mat4 ref, float tol) {
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 4; ++c) {
            ASSERT_FLOAT_NEAR(ref[c][r], m34->r[r][c], tol);
        }
    }
}

static void set_axis_angle(nt_anim_trs_t *o, float ax, float ay, float az, float degrees) {
    const float half = (degrees * 0.5F) * 0.017453292519943295F;
    const float sn = sinf(half);
    o->q[0] = ax * sn;
    o->q[1] = ay * sn;
    o->q[2] = az * sn;
    o->q[3] = cosf(half);
}

static nt_anim_trs_t make_trs(float tx, float ty, float tz, float ax, float ay, float az, float degrees, float sx, float sy, float sz) {
    nt_anim_trs_t o;
    o.t[0] = tx;
    o.t[1] = ty;
    o.t[2] = tz;
    o.s[0] = sx;
    o.s[1] = sy;
    o.s[2] = sz;
    set_axis_angle(&o, ax, ay, az, degrees);
    return o;
}

/* ---- Pose ABI ---- */

void test_abi_sizes_and_offsets(void) {
    TEST_ASSERT_EQUAL_size_t(40, sizeof(nt_anim_trs_t));
    TEST_ASSERT_EQUAL_size_t(0, offsetof(nt_anim_trs_t, t));
    TEST_ASSERT_EQUAL_size_t(12, offsetof(nt_anim_trs_t, q));
    TEST_ASSERT_EQUAL_size_t(28, offsetof(nt_anim_trs_t, s));
    TEST_ASSERT_EQUAL_size_t(4, _Alignof(nt_anim_trs_t));
    TEST_ASSERT_EQUAL_size_t(48, sizeof(nt_anim_mat34_t));
    TEST_ASSERT_EQUAL_size_t(4, _Alignof(nt_anim_mat34_t));
}

/* ---- Kernels against cglm ---- */

void test_mat34_from_trs_matches_cglm(void) {
    nt_anim_trs_t local[ANIM_RIG_JOINT_COUNT];
    nt_anim_pose_rest(&g_rig.skel, local);

    for (uint16_t j = 0; j < ANIM_RIG_JOINT_COUNT; ++j) {
        mat4 ref;
        ref_mat4_from_trs(&local[j], ref);
        nt_anim_mat34_t m;
        nt_anim_mat34_from_trs(&local[j], &m);
        assert_mat34_equals_mat4(&m, ref, 1e-5F);

        mat4 ref_bind;
        ref_mat4_from_trs(&g_rig.bind[j], ref_bind);
        nt_anim_mat34_t m_bind;
        nt_anim_mat34_from_trs(&g_rig.bind[j], &m_bind);
        assert_mat34_equals_mat4(&m_bind, ref_bind, 1e-5F);
    }
}

void test_mat34_mul_matches_cglm(void) {
    nt_anim_trs_t a = make_trs(0.3F, -1.2F, 2.0F, 0.0F, 1.0F, 0.0F, 37.0F, 1.0F, 2.5F, 0.4F);
    nt_anim_trs_t b = make_trs(-0.7F, 0.9F, 0.15F, 0.0F, 0.0F, 1.0F, -112.0F, 1.7F, 1.0F, 0.6F);

    mat4 ra;
    ref_mat4_from_trs(&a, ra);
    mat4 rb;
    ref_mat4_from_trs(&b, rb);
    mat4 rab;
    glm_mat4_mul(ra, rb, rab);

    nt_anim_mat34_t ma;
    nt_anim_mat34_from_trs(&a, &ma);
    nt_anim_mat34_t mb;
    nt_anim_mat34_from_trs(&b, &mb);
    nt_anim_mat34_t mab;
    nt_anim_mat34_mul(&ma, &mb, &mab);

    assert_mat34_equals_mat4(&mab, rab, 1e-5F);
}

void test_mat34_from_mat4_takes_top_three_rows(void) {
    nt_anim_trs_t a = make_trs(4.0F, -2.0F, 0.5F, 1.0F, 0.0F, 0.0F, 63.0F, 0.75F, 1.0F, 1.25F);
    mat4 ref;
    ref_mat4_from_trs(&a, ref);

    nt_anim_mat34_t m;
    nt_anim_mat34_from_mat4((const float *)ref, &m);
    assert_mat34_equals_mat4(&m, ref, 0.0F);
}

/* ---- FK ---- */

void test_fk_full_rig_matches_cglm_chain(void) {
    nt_anim_trs_t local[ANIM_RIG_JOINT_COUNT];
    memcpy(local, g_rig.bind, sizeof(local));

    nt_anim_mat34_t model[ANIM_RIG_JOINT_COUNT];
    nt_anim_fk(&g_rig.skel, local, model, 0, ANIM_RIG_JOINT_COUNT);

    mat4 ref[ANIM_RIG_JOINT_COUNT];
    ref_fk(local, ref);

    for (uint16_t j = 0; j < ANIM_RIG_JOINT_COUNT; ++j) {
        assert_mat34_equals_mat4(&model[j], ref[j], 1e-5F);
    }
}

void test_fk_transforms_points_as_column_vectors(void) {
    nt_anim_trs_t local[ANIM_RIG_JOINT_COUNT];
    memcpy(local, g_rig.bind, sizeof(local));

    nt_anim_mat34_t model[ANIM_RIG_JOINT_COUNT];
    nt_anim_fk(&g_rig.skel, local, model, 0, ANIM_RIG_JOINT_COUNT);

    /* Sequential cglm application from hand up to its root. */
    vec3 v = {0.0F, 0.3F, 0.0F};
    uint16_t j = JOINT_HAND;
    while (j != NT_ANIM_NO_PARENT) {
        mat4 l;
        ref_mat4_from_trs(&local[j], l);
        vec3 next;
        glm_mat4_mulv3(l, v, 1.0F, next);
        glm_vec3_copy(next, v);
        j = g_rig.skel.parent[j];
    }

    const nt_anim_mat34_t *g = &model[JOINT_HAND];
    for (int i = 0; i < 3; ++i) {
        const float x = (g->r[i][0] * 0.0F) + (g->r[i][1] * 0.3F) + (g->r[i][2] * 0.0F) + g->r[i][3];
        ASSERT_FLOAT_NEAR(v[i], x, 1e-5F);
    }
}

void test_fk_subtree_matches_full_pass(void) {
    nt_anim_trs_t local[ANIM_RIG_JOINT_COUNT];
    nt_anim_pose_rest(&g_rig.skel, local);

    nt_anim_mat34_t model[ANIM_RIG_JOINT_COUNT];
    nt_anim_fk(&g_rig.skel, local, model, 0, ANIM_RIG_JOINT_COUNT);

    nt_anim_mat34_t before[ANIM_RIG_JOINT_COUNT];
    memcpy(before, model, sizeof(before));

    set_axis_angle(&local[JOINT_ARM], 0.0F, 0.0F, 1.0F, 33.0F);
    nt_anim_fk(&g_rig.skel, local, model, JOINT_ARM, 2);

    nt_anim_mat34_t full[ANIM_RIG_JOINT_COUNT];
    nt_anim_fk(&g_rig.skel, local, full, 0, ANIM_RIG_JOINT_COUNT);

    for (int i = 0; i < 3; ++i) {
        for (int c = 0; c < 4; ++c) {
            ASSERT_FLOAT_NEAR(full[JOINT_ARM].r[i][c], model[JOINT_ARM].r[i][c], 1e-6F);
            ASSERT_FLOAT_NEAR(full[JOINT_HAND].r[i][c], model[JOINT_HAND].r[i][c], 1e-6F);
        }
    }

    const uint16_t untouched[] = {JOINT_ROOT_A, JOINT_SPINE, JOINT_HELPER, JOINT_LEG, JOINT_ROOT_B, JOINT_TAIL1, JOINT_TAIL2};
    for (size_t k = 0; k < sizeof(untouched) / sizeof(untouched[0]); ++k) {
        TEST_ASSERT_EQUAL_MEMORY(&before[untouched[k]], &model[untouched[k]], sizeof(nt_anim_mat34_t));
    }
}

void test_fk_root_spanning_range_equals_per_root_ranges(void) {
    nt_anim_trs_t local[ANIM_RIG_JOINT_COUNT];
    memcpy(local, g_rig.bind, sizeof(local));

    nt_anim_mat34_t whole[ANIM_RIG_JOINT_COUNT];
    nt_anim_fk(&g_rig.skel, local, whole, 0, ANIM_RIG_JOINT_COUNT);

    nt_anim_mat34_t split[ANIM_RIG_JOINT_COUNT];
    nt_anim_fk(&g_rig.skel, local, split, 0, 6);
    nt_anim_fk(&g_rig.skel, local, split, 6, 3);

    TEST_ASSERT_EQUAL_MEMORY(whole, split, sizeof(whole));
}

void test_fk_whole_subtree_range_is_valid(void) {
    nt_anim_trs_t local[ANIM_RIG_JOINT_COUNT];
    memcpy(local, g_rig.bind, sizeof(local));

    nt_anim_mat34_t model[ANIM_RIG_JOINT_COUNT];
    nt_anim_fk(&g_rig.skel, local, model, 0, ANIM_RIG_JOINT_COUNT);

    nt_anim_mat34_t again[ANIM_RIG_JOINT_COUNT];
    memcpy(again, model, sizeof(again));
    /* spine's subtree is [1, 6): a range that ends exactly at subtree_end. */
    nt_anim_fk(&g_rig.skel, local, again, JOINT_SPINE, 5);

    TEST_ASSERT_EQUAL_MEMORY(model, again, sizeof(model));
}

void test_fk_traps_on_invalid_ranges(void) {
    nt_anim_trs_t local[ANIM_RIG_JOINT_COUNT];
    memcpy(local, g_rig.bind, sizeof(local));
    nt_anim_mat34_t model[ANIM_RIG_JOINT_COUNT];

    NT_TEST_EXPECT_ASSERT(nt_anim_fk(&g_rig.skel, local, model, 0, 0));
    NT_TEST_EXPECT_ASSERT(nt_anim_fk(&g_rig.skel, local, model, 7, 3));
    /* [3, 6) leaves arm's subtree, which ends at 5. */
    NT_TEST_EXPECT_ASSERT(nt_anim_fk(&g_rig.skel, local, model, JOINT_ARM, 3));
}

/* ---- Rest pose ---- */

void test_pose_rest_then_fk_matches_fk_over_rest(void) {
    nt_anim_trs_t local[ANIM_RIG_JOINT_COUNT];
    nt_anim_pose_rest(&g_rig.skel, local);

    nt_anim_mat34_t copied[ANIM_RIG_JOINT_COUNT];
    nt_anim_fk(&g_rig.skel, local, copied, 0, ANIM_RIG_JOINT_COUNT);

    nt_anim_mat34_t direct[ANIM_RIG_JOINT_COUNT];
    nt_anim_fk(&g_rig.skel, g_rig.skel.rest, direct, 0, ANIM_RIG_JOINT_COUNT);

    TEST_ASSERT_EQUAL_MEMORY(direct, copied, sizeof(direct));
}

void test_pose_rest_traps_on_self_copy(void) { NT_TEST_EXPECT_ASSERT(nt_anim_pose_rest(&g_rig.skel, (nt_anim_trs_t *)g_rig.skel.rest)); }

/* ---- Sockets ---- */

void test_socket_matches_cglm_composition(void) {
    nt_anim_trs_t local[ANIM_RIG_JOINT_COUNT];
    memcpy(local, g_rig.bind, sizeof(local));

    nt_anim_mat34_t model[ANIM_RIG_JOINT_COUNT];
    nt_anim_fk(&g_rig.skel, local, model, 0, ANIM_RIG_JOINT_COUNT);

    mat4 ref[ANIM_RIG_JOINT_COUNT];
    ref_fk(local, ref);

    nt_anim_trs_t world_trs = make_trs(1.0F, 2.0F, -3.0F, 0.0F, 1.0F, 0.0F, 25.0F, 1.5F, 1.5F, 1.5F);
    mat4 world;
    ref_mat4_from_trs(&world_trs, world);

    nt_anim_trs_t socket_local = make_trs(0.05F, 0.0F, 0.1F, 0.0F, 1.0F, 0.0F, 10.0F, 1.0F, 1.0F, 1.0F);
    mat4 socket4;
    ref_mat4_from_trs(&socket_local, socket4);

    mat4 eg;
    glm_mat4_mul(world, ref[JOINT_HAND], eg);
    mat4 expected;
    glm_mat4_mul(eg, socket4, expected);

    nt_anim_mat34_t out;
    nt_anim_socket((const float *)world, &model[JOINT_HAND], &socket_local, &out);

    assert_mat34_equals_mat4(&out, expected, 1e-5F);
}

/* ---- Per-element checks ---- */

#if NT_ANIM_CHECKS
void test_fk_traps_on_non_finite_translation(void) {
    nt_anim_trs_t local[ANIM_RIG_JOINT_COUNT];
    memcpy(local, g_rig.bind, sizeof(local));
    nt_anim_mat34_t model[ANIM_RIG_JOINT_COUNT];

    local[JOINT_HELPER].t[0] = NAN;
    NT_TEST_EXPECT_ASSERT(nt_anim_fk(&g_rig.skel, local, model, 0, ANIM_RIG_JOINT_COUNT));
}

void test_fk_traps_on_non_unit_quaternion(void) {
    nt_anim_trs_t local[ANIM_RIG_JOINT_COUNT];
    memcpy(local, g_rig.bind, sizeof(local));
    nt_anim_mat34_t model[ANIM_RIG_JOINT_COUNT];

    local[JOINT_HELPER].q[0] = 0.0F;
    local[JOINT_HELPER].q[1] = 0.0F;
    local[JOINT_HELPER].q[2] = 0.0F;
    local[JOINT_HELPER].q[3] = 2.0F;
    NT_TEST_EXPECT_ASSERT(nt_anim_fk(&g_rig.skel, local, model, 0, ANIM_RIG_JOINT_COUNT));
}
#endif

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_abi_sizes_and_offsets);
    RUN_TEST(test_mat34_from_trs_matches_cglm);
    RUN_TEST(test_mat34_mul_matches_cglm);
    RUN_TEST(test_mat34_from_mat4_takes_top_three_rows);
    RUN_TEST(test_fk_full_rig_matches_cglm_chain);
    RUN_TEST(test_fk_transforms_points_as_column_vectors);
    RUN_TEST(test_fk_subtree_matches_full_pass);
    RUN_TEST(test_fk_root_spanning_range_equals_per_root_ranges);
    RUN_TEST(test_fk_whole_subtree_range_is_valid);
    RUN_TEST(test_fk_traps_on_invalid_ranges);
    RUN_TEST(test_pose_rest_then_fk_matches_fk_over_rest);
    RUN_TEST(test_pose_rest_traps_on_self_copy);
    RUN_TEST(test_socket_matches_cglm_composition);
#if NT_ANIM_CHECKS
    RUN_TEST(test_fk_traps_on_non_finite_translation);
    RUN_TEST(test_fk_traps_on_non_unit_quaternion);
#endif
    return UNITY_END();
}
