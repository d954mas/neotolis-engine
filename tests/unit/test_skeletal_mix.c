/* System and engine headers before Unity: <stdnoreturn.h> and the Windows SDK
 * clash over __declspec(noreturn) in the other order. */
#include <math.h>
#include <stddef.h>
#include <string.h>

#include "skeletal/nt_skeletal.h"
#include "test_helpers/skeletal_rig.h"

#include "unity.h"

#include "test_helpers/nt_assert_trap.h"

/* Unity is built with UNITY_EXCLUDE_FLOAT, so float comparisons go through
 * fabsf like everywhere else in the suite. */
#define ASSERT_FLOAT_NEAR(expected, actual, tol) TEST_ASSERT_TRUE_MESSAGE(fabsf((expected) - (actual)) <= (tol), "float not within tolerance")

#define ASSERT_POSE_BITS(expected, actual, count) TEST_ASSERT_EQUAL_MEMORY(expected, actual, (size_t)(count) * sizeof(nt_skeletal_trs_t))

enum { J = 4 };

static void quat_axis_angle(float *q, float ax, float ay, float az, float degrees) {
    const float len = sqrtf((ax * ax) + (ay * ay) + (az * az));
    const float half = (degrees * 3.14159265358979F / 180.0F) * 0.5F;
    const float s = sinf(half) / len;
    q[0] = ax * s;
    q[1] = ay * s;
    q[2] = az * s;
    q[3] = cosf(half);
}

/* Distinct translation, rotation and non-unit scale on every joint, so a
 * swapped joint or component shows up. */
static void make_pose(nt_skeletal_trs_t *p, float seed) {
    for (int j = 0; j < J; ++j) {
        const float f = seed + (float)j;
        p[j].t[0] = f;
        p[j].t[1] = -2.0F * f;
        p[j].t[2] = 0.5F + f;
        quat_axis_angle(p[j].q, 1.0F + f, 2.0F, -0.5F * f, 20.0F + (17.0F * f));
        p[j].s[0] = 1.0F + (0.1F * f);
        p[j].s[1] = 0.5F;
        p[j].s[2] = 2.0F;
    }
}

static void negate_q(nt_skeletal_trs_t *p) {
    for (int j = 0; j < J; ++j) {
        for (int c = 0; c < 4; ++c) {
            p[j].q[c] = -p[j].q[c];
        }
    }
}

/* actual == sum / |sum|: the closed-form result a test derives by hand. */
static void assert_normalized(const float sum[4], const float actual[4]) {
    const float len = sqrtf((sum[0] * sum[0]) + (sum[1] * sum[1]) + (sum[2] * sum[2]) + (sum[3] * sum[3]));
    for (int c = 0; c < 4; ++c) {
        ASSERT_FLOAT_NEAR(sum[c] / len, actual[c], 1e-6F);
    }
}

/* actual poses the same joint as expected: T/S equal, rotation equal up to
 * the sign the seed picks. */
static void assert_joint_near(const nt_skeletal_trs_t *expected, const nt_skeletal_trs_t *actual) {
    for (int c = 0; c < 3; ++c) {
        ASSERT_FLOAT_NEAR(expected->t[c], actual->t[c], 1e-5F);
        ASSERT_FLOAT_NEAR(expected->s[c], actual->s[c], 1e-6F);
    }
    const float d = (expected->q[0] * actual->q[0]) + (expected->q[1] * actual->q[1]) + (expected->q[2] * actual->q[2]) + (expected->q[3] * actual->q[3]);
    const float sign = (d < 0.0F) ? -1.0F : 1.0F;
    for (int c = 0; c < 4; ++c) {
        ASSERT_FLOAT_NEAR(sign * expected->q[c], actual->q[c], 1e-6F);
    }
}

static void fill_nan(nt_skeletal_trs_t *p) {
    for (int j = 0; j < J; ++j) {
        for (int c = 0; c < 3; ++c) {
            p[j].t[c] = NAN;
            p[j].s[c] = NAN;
        }
        for (int c = 0; c < 4; ++c) {
            p[j].q[c] = NAN;
        }
    }
}

static nt_skeletal_trs_t g_defaults[J];

void setUp(void) { make_pose(g_defaults, 100.0F); }

void tearDown(void) {}

// #region mix
void test_negating_any_input_rotation_including_the_first_gives_the_same_mix(void) {
    nt_skeletal_trs_t poses[3][J];
    make_pose(poses[0], 0.0F);
    make_pose(poses[1], 3.0F);
    make_pose(poses[2], 7.0F);
    const float weights[J] = {1.0F, 0.0F, 2.5F, 0.3F};
    const nt_skeletal_mix_input_t inputs[3] = {{poses[0], NULL, 1.0F}, {poses[1], weights, 0.5F}, {poses[2], NULL, 2.0F}};

    nt_skeletal_trs_t reference[J];
    nt_skeletal_mix(inputs, 3, g_defaults, J, reference);

    for (int combo = 1; combo < 8; ++combo) {
        for (int i = 0; i < 3; ++i) {
            if ((combo & (1 << i)) != 0) {
                negate_q(poses[i]);
            }
        }
        nt_skeletal_trs_t out[J];
        nt_skeletal_mix(inputs, 3, g_defaults, J, out);
        ASSERT_POSE_BITS(reference, out, J);
        for (int i = 0; i < 3; ++i) {
            if ((combo & (1 << i)) != 0) {
                negate_q(poses[i]);
            }
        }
    }
}

/* Identity and a 180 deg turn about x have dot 0 exactly. Without the
 * canonical branch the turn's sign is whatever the data carries, and the two
 * spellings of it would average to two different rotations. */
void test_an_exactly_orthogonal_pair_takes_the_canonical_sign(void) {
    nt_skeletal_trs_t id[J];
    nt_skeletal_trs_t turn[J];
    make_pose(id, 0.0F);
    make_pose(turn, 0.0F);
    const float inv_sqrt2 = 0.70710678F;
    for (int k = 0; k < 2; ++k) {
        const float sign = (k == 0) ? -1.0F : 1.0F;
        for (int j = 0; j < J; ++j) {
            memcpy(id[j].q, (const float[4]){0.0F, 0.0F, 0.0F, 1.0F}, sizeof(id[j].q));
            memcpy(turn[j].q, (const float[4]){sign, 0.0F, 0.0F, 0.0F}, sizeof(turn[j].q));
        }
        const nt_skeletal_mix_input_t inputs[2] = {{id, NULL, 1.0F}, {turn, NULL, 1.0F}};
        nt_skeletal_trs_t out[J];
        nt_skeletal_mix(inputs, 2, g_defaults, J, out);
        ASSERT_FLOAT_NEAR(inv_sqrt2, out[0].q[0], 1e-6F);
        ASSERT_FLOAT_NEAR(inv_sqrt2, out[0].q[3], 1e-6F);
    }
}

/* An exact tie with w != 0 takes the sign that makes w positive, not the one
 * of its largest component (y here), which would give another rotation. */
void test_an_exact_tie_with_nonzero_w_makes_w_positive(void) {
    nt_skeletal_trs_t a[J];
    nt_skeletal_trs_t b[J];
    make_pose(a, 0.0F);
    make_pose(b, 0.0F);
    memcpy(a[0].q, (const float[4]){1.0F, 0.0F, 0.0F, 0.0F}, sizeof(a[0].q));
    memcpy(b[0].q, (const float[4]){0.0F, 0.8F, 0.0F, -0.6F}, sizeof(b[0].q));
    const nt_skeletal_mix_input_t inputs[2] = {{a, NULL, 1.0F}, {b, NULL, 1.0F}};
    nt_skeletal_trs_t out[J];
    nt_skeletal_mix(inputs, 2, g_defaults, J, out);
    const float sum[4] = {1.0F, -0.8F, 0.0F, 0.6F};
    assert_normalized(sum, out[0].q);
}

/* Rotations weigh by influence like T/S do: 0 and 90 deg about y at gains 1
 * and 3 give normalize(q0 + 3 q90), and a joint weight scales the gain. */
void test_rotations_weigh_by_gain_times_joint_weight(void) {
    nt_skeletal_trs_t a[J];
    nt_skeletal_trs_t b[J];
    make_pose(a, 0.0F);
    make_pose(b, 0.0F);
    for (int j = 0; j < J; ++j) {
        quat_axis_angle(a[j].q, 0.0F, 1.0F, 0.0F, 0.0F);
        quat_axis_angle(b[j].q, 0.0F, 1.0F, 0.0F, 90.0F);
    }
    const float weights[J] = {1.0F, 0.5F, 1.0F, 1.0F};
    const nt_skeletal_mix_input_t inputs[2] = {{a, NULL, 1.0F}, {b, weights, 3.0F}};
    nt_skeletal_trs_t out[J];
    nt_skeletal_mix(inputs, 2, g_defaults, J, out);
    for (int j = 0; j < 2; ++j) {
        const float w = 3.0F * weights[j];
        const float sum[4] = {0.0F, w * b[j].q[1], 0.0F, a[j].q[3] + (w * b[j].q[3])};
        assert_normalized(sum, out[j].q);
    }
}

/* Three turns about y, 0, 170 and -100 deg, at equal gains. Against the
 * running sum the third flips (dot < 0); against the first input it would
 * not (dot = cos 50 deg). Reversed, the seed and the flip land elsewhere and
 * the average is a different rotation: order is part of the result. */
void test_alignment_follows_the_running_sum_in_supplied_order(void) {
    nt_skeletal_trs_t p[3][J];
    const float degrees[3] = {0.0F, 170.0F, -100.0F};
    for (int i = 0; i < 3; ++i) {
        make_pose(p[i], 0.0F);
        quat_axis_angle(p[i][0].q, 0.0F, 1.0F, 0.0F, degrees[i]);
    }
    const float *q0 = p[0][0].q;
    const float *q1 = p[1][0].q;
    const float *q2 = p[2][0].q;
    nt_skeletal_trs_t out[J];

    const nt_skeletal_mix_input_t forward[3] = {{p[0], NULL, 1.0F}, {p[1], NULL, 1.0F}, {p[2], NULL, 1.0F}};
    nt_skeletal_mix(forward, 3, g_defaults, J, out);
    const float fwd[4] = {0.0F, q0[1] + q1[1] - q2[1], 0.0F, q0[3] + q1[3] - q2[3]};
    assert_normalized(fwd, out[0].q);

    /* Seed q2 (w > 0), q1 flips against it, q0 then agrees with the sum. */
    const nt_skeletal_mix_input_t reverse[3] = {{p[2], NULL, 1.0F}, {p[1], NULL, 1.0F}, {p[0], NULL, 1.0F}};
    nt_skeletal_mix(reverse, 3, g_defaults, J, out);
    const float rev[4] = {0.0F, q2[1] - q1[1] + q0[1], 0.0F, q2[3] - q1[3] + q0[3]};
    assert_normalized(rev, out[0].q);
    const float fwd_len = sqrtf((fwd[1] * fwd[1]) + (fwd[3] * fwd[3]));
    const float rev_len = sqrtf((rev[1] * rev[1]) + (rev[3] * rev[3]));
    TEST_ASSERT_TRUE(fabsf(((fwd[1] * rev[1]) + (fwd[3] * rev[3])) / (fwd_len * rev_len)) < 0.99F);
}

/* Aligning to rest would average +170 and -170 deg to 0 deg. */
void test_plus_and_minus_170_degrees_average_to_180(void) {
    nt_skeletal_trs_t a[J];
    nt_skeletal_trs_t b[J];
    make_pose(a, 0.0F);
    make_pose(b, 0.0F);
    quat_axis_angle(a[2].q, 0.0F, 1.0F, 0.0F, 170.0F);
    quat_axis_angle(b[2].q, 0.0F, 1.0F, 0.0F, -170.0F);
    const nt_skeletal_mix_input_t inputs[2] = {{a, NULL, 1.0F}, {b, NULL, 1.0F}};
    nt_skeletal_trs_t out[J];
    nt_skeletal_mix(inputs, 2, g_defaults, J, out);
    ASSERT_FLOAT_NEAR(1.0F, fabsf(out[2].q[1]), 1e-6F);
    ASSERT_FLOAT_NEAR(0.0F, out[2].q[3], 1e-6F);
}

void test_zero_total_influence_copies_the_defaults(void) {
    nt_skeletal_trs_t a[J];
    make_pose(a, 1.0F);
    const float zeros[J] = {0.0F, 0.0F, 0.0F, 0.0F};
    const nt_skeletal_mix_input_t inputs[2] = {{a, NULL, 0.0F}, {a, zeros, 1.0F}};
    nt_skeletal_trs_t out[J];

    nt_skeletal_mix(inputs, 2, g_defaults, J, out);
    ASSERT_POSE_BITS(g_defaults, out, J);

    memset(out, 0, sizeof(out));
    nt_skeletal_mix(NULL, 0, g_defaults, J, out);
    ASSERT_POSE_BITS(g_defaults, out, J);
}

/* A gain-0 track keeps its clock but the game need not sample it: whatever
 * its buffer holds, stale or garbage, never reaches the result. */
void test_a_zero_influence_input_reads_nothing_from_its_pose(void) {
    nt_skeletal_trs_t a[J];
    nt_skeletal_trs_t garbage[J];
    make_pose(a, 1.0F);
    fill_nan(garbage);
    const float zeros[J] = {0.0F, 0.0F, 0.0F, 0.0F};
    const nt_skeletal_mix_input_t alone = {a, NULL, 1.0F};
    const nt_skeletal_mix_input_t by_gain[2] = {{garbage, NULL, 0.0F}, {a, NULL, 1.0F}};
    const nt_skeletal_mix_input_t by_weight[2] = {{garbage, zeros, 1.0F}, {a, NULL, 1.0F}};
    nt_skeletal_trs_t expected[J];
    nt_skeletal_trs_t out[J];
    nt_skeletal_mix(&alone, 1, g_defaults, J, expected);
    nt_skeletal_mix(by_gain, 2, g_defaults, J, out);
    ASSERT_POSE_BITS(expected, out, J);
    nt_skeletal_mix(by_weight, 2, g_defaults, J, out);
    ASSERT_POSE_BITS(expected, out, J);
}

void test_zero_weights_on_one_input_leave_the_others_normalized(void) {
    nt_skeletal_trs_t a[J];
    nt_skeletal_trs_t b[J];
    make_pose(a, 0.0F);
    make_pose(b, 5.0F);
    const float weights[J] = {1.0F, 0.0F, 1.0F, 1.0F};
    const nt_skeletal_mix_input_t inputs[2] = {{a, weights, 1.0F}, {b, NULL, 0.5F}};
    nt_skeletal_trs_t out[J];
    nt_skeletal_mix(inputs, 2, g_defaults, J, out);

    for (int c = 0; c < 3; ++c) {
        ASSERT_FLOAT_NEAR(b[1].t[c], out[1].t[c], 1e-5F);
        ASSERT_FLOAT_NEAR(b[1].s[c], out[1].s[c], 1e-6F);
        ASSERT_FLOAT_NEAR((a[0].t[c] + (0.5F * b[0].t[c])) / 1.5F, out[0].t[c], 1e-5F);
    }
    for (int c = 0; c < 4; ++c) {
        ASSERT_FLOAT_NEAR(b[1].q[c], out[1].q[c], 1e-6F);
    }
}

/* No threshold: fading toward rest is an explicit rest input or an override.
 * A sole input poses fully at any gain of the range, and a pair weighs T/S by
 * the gain ratio alone. */
void test_a_tiny_or_huge_gain_contributes_fully_and_by_ratio(void) {
    nt_skeletal_trs_t a[J];
    nt_skeletal_trs_t b[J];
    make_pose(a, 2.0F);
    make_pose(b, 5.0F);
    const float gains[3] = {0.0001F, 0x1p-60F, 0x1p58F};
    for (int g = 0; g < 3; ++g) {
        const nt_skeletal_mix_input_t alone = {a, NULL, gains[g]};
        const nt_skeletal_mix_input_t pair[2] = {{a, NULL, gains[g]}, {b, NULL, 3.0F * gains[g]}};
        nt_skeletal_trs_t out[J];
        nt_skeletal_mix(&alone, 1, g_defaults, J, out);
        for (int j = 0; j < J; ++j) {
            assert_joint_near(&a[j], &out[j]);
        }
        nt_skeletal_mix(pair, 2, g_defaults, J, out);
        for (int j = 0; j < J; ++j) {
            for (int c = 0; c < 3; ++c) {
                ASSERT_FLOAT_NEAR((a[j].t[c] + (3.0F * b[j].t[c])) / 4.0F, out[j].t[c], 1e-5F);
                ASSERT_FLOAT_NEAR((a[j].s[c] + (3.0F * b[j].s[c])) / 4.0F, out[j].s[c], 1e-6F);
            }
        }
    }
}

/* A partial layer fading out next to a full-strength one: on the joints it
 * owns alone its influence is as small as 2^-60 * 2, and it must still pose
 * them exactly. */
void test_a_fading_partial_input_still_poses_the_joints_it_owns(void) {
    nt_skeletal_trs_t legs[J];
    nt_skeletal_trs_t arms[J];
    make_pose(legs, 0.0F);
    make_pose(arms, 7.0F);
    const float leg_w[J] = {1.0F, 0.0F, 1.0F, 0.0F};
    const float arm_w[J] = {0.0F, 2.0F, 0.0F, 2.0F};
    /* 2^-60 leaves |sum|^2 below 1e-30, where the 1/W rescale keeps the
     * rotation's precision. */
    const float fades[2] = {0x1p-60F, 1e-10F};
    for (int f = 0; f < 2; ++f) {
        const nt_skeletal_mix_input_t inputs[2] = {{legs, leg_w, 1.0F}, {arms, arm_w, fades[f]}};
        nt_skeletal_trs_t out[J];
        nt_skeletal_mix(inputs, 2, g_defaults, J, out);
        for (int j = 1; j < J; j += 2) {
            assert_joint_near(&arms[j], &out[j]);
        }
    }
}

/* Local-space mixing with different weights on parent and child moves
 * rotations only: bone offsets and non-unit scales survive, so FK keeps every
 * child on its parent. The cglm FK is the independent reference. */
void test_asymmetric_parent_child_weights_keep_the_chain_attached(void) {
    skeletal_rig_t rig;
    skeletal_rig_asymmetric(&rig);
    float weights[SKELETAL_RIG_JOINT_COUNT];
    for (int j = 0; j < SKELETAL_RIG_JOINT_COUNT; ++j) {
        weights[j] = ((j & 1) == 0) ? 0.2F : 3.0F;
    }
    const nt_skeletal_mix_input_t inputs[2] = {{rig.rest, NULL, 1.0F}, {rig.bind, weights, 1.0F}};
    nt_skeletal_trs_t out[SKELETAL_RIG_JOINT_COUNT];
    nt_skeletal_mix(inputs, 2, rig.rest, SKELETAL_RIG_JOINT_COUNT, out);

    for (int j = 0; j < SKELETAL_RIG_JOINT_COUNT; ++j) {
        for (int c = 0; c < 3; ++c) {
            ASSERT_FLOAT_NEAR(rig.rest[j].t[c], out[j].t[c], 1e-5F);
            ASSERT_FLOAT_NEAR(rig.rest[j].s[c], out[j].s[c], 1e-6F);
        }
    }

    /* Joints 3 (weight 3) and 4 (weight 0.2) are posed differently in rest
     * and bind; the sum seeds on rest with w made positive and aligns bind. */
    const int posed[2] = {3, 4};
    for (int k = 0; k < 2; ++k) {
        const int j = posed[k];
        const float *r = rig.rest[j].q;
        const float *b = rig.bind[j].q;
        const float seed = (r[3] < 0.0F) ? -1.0F : 1.0F;
        const float d = seed * ((r[0] * b[0]) + (r[1] * b[1]) + (r[2] * b[2]) + (r[3] * b[3]));
        const float wb = (d < 0.0F) ? -weights[j] : weights[j];
        float sum[4];
        for (int c = 0; c < 4; ++c) {
            sum[c] = (seed * r[c]) + (wb * b[c]);
        }
        assert_normalized(sum, out[j].q);
    }

    nt_skeletal_mat34_t model[SKELETAL_RIG_JOINT_COUNT];
    mat4 ref[SKELETAL_RIG_JOINT_COUNT];
    nt_skeletal_fk(&rig.skel, out, model, 0, SKELETAL_RIG_JOINT_COUNT);
    skeletal_rig_ref_fk(out, ref);
    for (int j = 0; j < SKELETAL_RIG_JOINT_COUNT; ++j) {
        skeletal_rig_assert_mat34_equals_mat4(&model[j], ref[j], 1e-4F);
    }
}

/* R3: run at gain 1 plus an attack weighted legs 0.25 / arms 3. A partial
 * input's coefficient is bw / (1 + bw), whatever split the full-body inputs
 * take as long as their gains sum to 1. */
void test_partial_weights_give_fixed_coefficients_through_a_crossfade(void) {
    nt_skeletal_trs_t walk[J];
    nt_skeletal_trs_t run[J];
    nt_skeletal_trs_t attack[J];
    make_pose(walk, 0.0F);
    make_pose(run, 4.0F);
    make_pose(attack, 8.0F);
    for (int j = 0; j < J; ++j) {
        memset(walk[j].t, 0, sizeof(walk[j].t));
        memset(run[j].t, 0, sizeof(run[j].t));
        walk[j].t[1] = 1.0F;
        run[j].t[1] = 3.0F;
        attack[j].t[0] = 1.0F;
        attack[j].t[1] = 0.0F;
        attack[j].t[2] = 0.0F;
    }
    const float weights[J] = {0.25F, 3.0F, 0.25F, 3.0F}; /* leg, arm, leg, arm */

    for (int step = 0; step <= 4; ++step) {
        const float c = 0.25F * (float)step;
        const nt_skeletal_mix_input_t inputs[3] = {{walk, NULL, 1.0F - c}, {run, NULL, c}, {attack, weights, 1.0F}};
        nt_skeletal_trs_t out[J];
        nt_skeletal_mix(inputs, 3, g_defaults, J, out);
        ASSERT_FLOAT_NEAR(0.2F, out[0].t[0], 1e-6F);
        ASSERT_FLOAT_NEAR(0.75F, out[1].t[0], 1e-6F);
        ASSERT_FLOAT_NEAR(0.2F, out[2].t[0], 1e-6F);
        ASSERT_FLOAT_NEAR(0.75F, out[3].t[0], 1e-6F);
        for (int j = 0; j < J; ++j) {
            ASSERT_FLOAT_NEAR(((1.0F - c) + (3.0F * c)) / (1.0F + weights[j]), out[j].t[1], 1e-6F);
        }
    }
}
// #endregion

// #region override
/* Strength is alpha * mask whatever the gains inside the base mix do: the
 * thing mix cannot express without per-joint compensation. */
void test_override_strength_ignores_the_gains_inside_the_base(void) {
    nt_skeletal_trs_t walk[J];
    nt_skeletal_trs_t run[J];
    nt_skeletal_trs_t aim[J];
    make_pose(walk, 0.0F);
    make_pose(run, 4.0F);
    make_pose(aim, 8.0F);
    const float mask[J] = {0.0F, 0.5F, 1.0F, 0.25F};

    for (int step = 0; step <= 4; ++step) {
        const float c = 0.25F * (float)step;
        const nt_skeletal_mix_input_t inputs[2] = {{walk, NULL, 1.0F - c}, {run, NULL, c}};
        nt_skeletal_trs_t base[J];
        nt_skeletal_trs_t out[J];
        nt_skeletal_mix(inputs, 2, g_defaults, J, base);
        nt_skeletal_override(base, aim, mask, 0.8F, J, out);
        ASSERT_POSE_BITS(&base[0], &out[0], 1);
        for (int j = 1; j < J; ++j) {
            const float a = 0.8F * mask[j];
            for (int k = 0; k < 3; ++k) {
                ASSERT_FLOAT_NEAR(base[j].t[k] + (a * (aim[j].t[k] - base[j].t[k])), out[j].t[k], 1e-5F);
            }
        }
    }
}

/* Between the endpoints Q takes the short way (top given as -q) and S lerps. */
void test_override_blends_rotation_the_short_way_and_lerps_scale(void) {
    nt_skeletal_trs_t base[J];
    nt_skeletal_trs_t top[J];
    nt_skeletal_trs_t out[J];
    make_pose(base, 0.0F);
    make_pose(top, 6.0F);
    quat_axis_angle(base[1].q, 0.0F, 1.0F, 0.0F, 0.0F);
    quat_axis_angle(top[1].q, 0.0F, 1.0F, 0.0F, 90.0F);
    for (int c = 0; c < 4; ++c) {
        top[1].q[c] = -top[1].q[c];
    }
    top[1].s[0] = 3.0F;
    const float mask[J] = {0.0F, 0.25F, 0.0F, 0.0F};
    nt_skeletal_override(base, top, mask, 1.0F, J, out);

    const float sum[4] = {0.0F, 0.25F * -top[1].q[1], 0.0F, (0.75F * base[1].q[3]) + (0.25F * -top[1].q[3])};
    assert_normalized(sum, out[1].q);
    for (int c = 0; c < 3; ++c) {
        ASSERT_FLOAT_NEAR((0.75F * base[1].s[c]) + (0.25F * top[1].s[c]), out[1].s[c], 1e-6F);
    }
}

/* A joint the mask leaves out reads nothing from top, so a partial layer
 * need not pose the joints it does not own. */
void test_override_reads_nothing_from_top_where_the_mask_is_zero(void) {
    nt_skeletal_trs_t base[J];
    nt_skeletal_trs_t top[J];
    nt_skeletal_trs_t out[J];
    make_pose(base, 0.0F);
    make_pose(top, 6.0F);
    nt_skeletal_trs_t garbage[J];
    fill_nan(garbage);
    top[0] = garbage[0];
    top[2] = garbage[2];
    top[3] = garbage[3];
    const float mask[J] = {0.0F, 1.0F, 0.0F, 0.0F};
    nt_skeletal_override(base, top, mask, 0.6F, J, out);
    ASSERT_POSE_BITS(&base[0], &out[0], 1);
    ASSERT_POSE_BITS(&base[2], &out[2], 2);
}

void test_override_endpoints_copy_exactly(void) {
    nt_skeletal_trs_t base[J];
    nt_skeletal_trs_t top[J];
    nt_skeletal_trs_t out[J];
    make_pose(base, 0.0F);
    make_pose(top, 6.0F);
    nt_skeletal_override(base, top, NULL, 0.0F, J, out);
    ASSERT_POSE_BITS(base, out, J);
    nt_skeletal_override(base, top, NULL, 1.0F, J, out);
    ASSERT_POSE_BITS(top, out, J);
}

void test_override_in_place_matches_a_separate_output(void) {
    nt_skeletal_trs_t base[J];
    nt_skeletal_trs_t top[J];
    nt_skeletal_trs_t out[J];
    make_pose(base, 0.0F);
    make_pose(top, 6.0F);
    const float mask[J] = {0.1F, 0.5F, 0.0F, 1.0F};
    nt_skeletal_override(base, top, mask, 0.7F, J, out);
    nt_skeletal_override(base, top, mask, 0.7F, J, base);
    ASSERT_POSE_BITS(out, base, J);
}
// #endregion

// #region interruption recipe
/* The reference recipe of the spec, as game code over the kernels: tracks
 * are clocks, clip poses are functions of time, and every buffer is a fixed
 * array sized up front. */
enum { CLIP_WALK, CLIP_RUN, CLIP_JUMP, CLIP_ROLL, CLIP_WAVE, CLIP_COUNT };

static void clip_pose(int clip, double time, nt_skeletal_trs_t *out) {
    for (int j = 0; j < J; ++j) {
        const float f = (float)((clip * 7) + j);
        const float phase = (float)time * (1.0F + (0.3F * (float)clip));
        out[j].t[0] = f + sinf(phase);
        out[j].t[1] = cosf(phase + f);
        out[j].t[2] = 0.25F * f;
        quat_axis_angle(out[j].q, 1.0F, f, 0.5F, 60.0F * sinf(phase + (0.5F * f)));
        out[j].s[0] = 1.0F;
        out[j].s[1] = 1.0F + (0.1F * (float)clip);
        out[j].s[2] = 1.0F;
    }
}

typedef struct {
    nt_skeletal_track_t tracks[CLIP_COUNT];
    nt_skeletal_trs_t sampled[CLIP_COUNT][J];
    nt_skeletal_trs_t capture[J];
    nt_skeletal_trs_t snapshot[J];
    nt_skeletal_trs_t signal[J];
    nt_skeletal_trs_t final[J];
} hero_t;

static hero_t g_hero;
static const float g_wave_weights[J] = {0.0F, 2.0F, 0.0F, 2.0F};

/* The displayed pose: the locomotion signal plus an independent partial wave
 * that the interruption never touches. */
static void hero_compose(hero_t *h) {
    const nt_skeletal_mix_input_t inputs[2] = {{h->signal, NULL, 1.0F}, {h->sampled[CLIP_WAVE], g_wave_weights, 1.0F}};
    nt_skeletal_mix(inputs, 2, g_defaults, J, h->final);
}

static void hero_sample(hero_t *h) {
    for (int c = 0; c < CLIP_COUNT; ++c) {
        if ((h->tracks[c].flags & NT_SKELETAL_TRACK_OCCUPIED) != 0U) {
            clip_pose(c, h->tracks[c].time, h->sampled[c]);
        }
    }
}

void test_interruption_recipe_is_continuous_at_the_handoff_and_reuses_one_snapshot(void) {
    hero_t *h = &g_hero;
    memset(h, 0, sizeof(*h));
    for (int c = 0; c < CLIP_COUNT; ++c) {
        h->tracks[c] = (nt_skeletal_track_t){0.0, 2.0 + (0.5 * c), 1.0F, NT_SKELETAL_TRACK_LOOPING};
    }
    h->tracks[CLIP_WALK].flags |= NT_SKELETAL_TRACK_OCCUPIED;
    h->tracks[CLIP_RUN].flags |= NT_SKELETAL_TRACK_OCCUPIED;
    h->tracks[CLIP_WAVE].flags |= NT_SKELETAL_TRACK_OCCUPIED;
    const double dt = 1.0 / 60.0;

    /* A walk -> run crossfade halfway through when the jump arrives. */
    const nt_skeletal_mix_input_t loco[2] = {{h->sampled[CLIP_WALK], NULL, 0.5F}, {h->sampled[CLIP_RUN], NULL, 0.5F}};
    nt_skeletal_tracks_advance(h->tracks, CLIP_COUNT, dt);
    hero_sample(h);
    nt_skeletal_mix(loco, 2, g_defaults, J, h->signal);
    hero_compose(h);

    // #region first interruption: walk/run crossfade -> jump
    nt_skeletal_tracks_advance(h->tracks, CLIP_COUNT, dt);
    h->tracks[CLIP_JUMP].flags |= NT_SKELETAL_TRACK_OCCUPIED;
    hero_sample(h);
    /* What this update would have shown without the interruption. */
    nt_skeletal_mix(loco, 2, g_defaults, J, h->capture);
    nt_skeletal_trs_t expected_final[J];
    memcpy(h->signal, h->capture, sizeof(h->capture));
    hero_compose(h);
    memcpy(expected_final, h->final, sizeof(expected_final));

    memcpy(h->snapshot, h->capture, sizeof(h->capture));
    h->tracks[CLIP_WALK].flags = 0U;
    h->tracks[CLIP_RUN].flags = 0U;
    nt_skeletal_override(h->snapshot, h->sampled[CLIP_JUMP], NULL, 0.0F, J, h->signal);
    hero_compose(h);
    ASSERT_POSE_BITS(h->capture, h->signal, J);
    ASSERT_POSE_BITS(expected_final, h->final, J);
    // #endregion

    // #region ramp toward the jump, then a second interruption -> roll
    const float a = 0.5F;
    for (int frame = 0; frame < 2; ++frame) {
        nt_skeletal_tracks_advance(h->tracks, CLIP_COUNT, dt);
        hero_sample(h);
    }

    nt_skeletal_tracks_advance(h->tracks, CLIP_COUNT, dt);
    h->tracks[CLIP_ROLL].flags |= NT_SKELETAL_TRACK_OCCUPIED;
    hero_sample(h);
    const double wave_time = h->tracks[CLIP_WAVE].time;
    nt_skeletal_override(h->snapshot, h->sampled[CLIP_JUMP], NULL, a, J, h->capture);
    memcpy(h->signal, h->capture, sizeof(h->capture));
    hero_compose(h);
    memcpy(expected_final, h->final, sizeof(expected_final));

    memcpy(h->snapshot, h->capture, sizeof(h->capture));
    h->tracks[CLIP_JUMP].flags = 0U;
    nt_skeletal_override(h->snapshot, h->sampled[CLIP_ROLL], NULL, 0.0F, J, h->signal);
    hero_compose(h);
    ASSERT_POSE_BITS(h->capture, h->signal, J);
    ASSERT_POSE_BITS(expected_final, h->final, J);
    // #endregion

    /* The wave track kept its own clock and coefficient through both
     * interruptions. Memory stays constant by construction: hero_t is the
     * whole state, and the second interruption overwrote the one snapshot. */
    ASSERT_FLOAT_NEAR(5.0F * (float)dt, (float)wave_time, 1e-6F);
    for (int k = 0; k < 3; ++k) {
        ASSERT_FLOAT_NEAR((h->signal[1].t[k] + (2.0F * h->sampled[CLIP_WAVE][1].t[k])) / 3.0F, h->final[1].t[k], 1e-5F);
    }
}
// #endregion

// #region preconditions
#if NT_ASSERT_MODE == NT_ASSERT_FULL
#define ASSERT_TRAPPED_ON(fragment) TEST_ASSERT_TRUE_MESSAGE(strstr(nt_test_assert_last_expr, (fragment)) != NULL, "a different NT_ASSERT fired: " fragment)

void test_mix_traps_on_a_gain_outside_its_range(void) {
    nt_skeletal_trs_t a[J];
    nt_skeletal_trs_t out[J];
    make_pose(a, 0.0F);
    const float bad[5] = {-0.5F, NAN, INFINITY, 0x1p-61F, 0x1p61F};
    for (int i = 0; i < 5; ++i) {
        const nt_skeletal_mix_input_t input = {a, NULL, bad[i]};
        NT_TEST_EXPECT_ASSERT(nt_skeletal_mix(&input, 1, g_defaults, J, out));
        ASSERT_TRAPPED_ON("inputs[i].gain >= 0x1p-60F && inputs[i].gain <= 0x1p60F");
    }
}

void test_mix_traps_on_null_inputs_or_pose(void) {
    nt_skeletal_trs_t out[J];
    NT_TEST_EXPECT_ASSERT(nt_skeletal_mix(NULL, 1, g_defaults, J, out));
    ASSERT_TRAPPED_ON("inputs != NULL || input_count == 0U");
    const nt_skeletal_mix_input_t input = {NULL, NULL, 1.0F};
    NT_TEST_EXPECT_ASSERT(nt_skeletal_mix(&input, 1, g_defaults, J, out));
    ASSERT_TRAPPED_ON("inputs[i].pose != NULL");
}

#if NT_SKELETAL_CHECKS
/* Values the range compares let through: an infinite weight and a pose that
 * is not a unit rotation on a contributing input. */
void test_mix_checks_trap_on_an_infinite_weight_or_a_non_unit_rotation(void) {
    nt_skeletal_trs_t a[J];
    nt_skeletal_trs_t out[J];
    make_pose(a, 0.0F);
    const float weights[J] = {1.0F, INFINITY, 1.0F, 1.0F};
    const nt_skeletal_mix_input_t weighted = {a, weights, 1.0F};
    NT_TEST_EXPECT_ASSERT(nt_skeletal_mix(&weighted, 1, g_defaults, J, out));
    ASSERT_TRAPPED_ON("nt_skeletal_finite((double)in->weights[j])");
    a[2].q[0] = 2.0F;
    const nt_skeletal_mix_input_t plain = {a, NULL, 1.0F};
    NT_TEST_EXPECT_ASSERT(nt_skeletal_mix(&plain, 1, g_defaults, J, out));
    ASSERT_TRAPPED_ON("- 1.0F) < 1e-3F");
    make_pose(a, 0.0F);
    a[1].s[2] = INFINITY;
    NT_TEST_EXPECT_ASSERT(nt_skeletal_mix(&plain, 1, g_defaults, J, out));
    ASSERT_TRAPPED_ON("nt_skeletal_finite((double)v->s[c])");
}

void test_override_checks_trap_on_a_blended_joint_that_is_not_a_unit_rotation(void) {
    nt_skeletal_trs_t base[J];
    nt_skeletal_trs_t top[J];
    nt_skeletal_trs_t out[J];
    make_pose(base, 0.0F);
    make_pose(top, 1.0F);
    base[1].q[3] = 3.0F;
    NT_TEST_EXPECT_ASSERT(nt_skeletal_override(base, top, NULL, 0.5F, J, out));
    ASSERT_TRAPPED_ON("- 1.0F) < 1e-3F");
    make_pose(base, 0.0F);
    top[2].t[0] = NAN;
    NT_TEST_EXPECT_ASSERT(nt_skeletal_override(base, top, NULL, 0.5F, J, out));
    ASSERT_TRAPPED_ON("nt_skeletal_finite((double)v->t[c])");
}
#endif

void test_mix_traps_on_a_negative_or_nan_weight(void) {
    nt_skeletal_trs_t a[J];
    nt_skeletal_trs_t out[J];
    make_pose(a, 0.0F);
    float weights[J] = {1.0F, 1.0F, -0.1F, 1.0F};
    const nt_skeletal_mix_input_t input = {a, weights, 1.0F};
    NT_TEST_EXPECT_ASSERT(nt_skeletal_mix(&input, 1, g_defaults, J, out));
    ASSERT_TRAPPED_ON("in->weights[j] >= 0.0F");
    weights[2] = NAN;
    NT_TEST_EXPECT_ASSERT(nt_skeletal_mix(&input, 1, g_defaults, J, out));
    ASSERT_TRAPPED_ON("in->weights[j] >= 0.0F");
}

void test_mix_traps_when_the_output_overlaps_an_input(void) {
    nt_skeletal_trs_t a[J + 1];
    make_pose(a, 0.0F);
    const nt_skeletal_mix_input_t input = {a, NULL, 1.0F};
    NT_TEST_EXPECT_ASSERT(nt_skeletal_mix(&input, 1, g_defaults, J, a + 1));
    ASSERT_TRAPPED_ON("nt_skeletal_poses_disjoint(out, inputs[i].pose, joint_count)");
    NT_TEST_EXPECT_ASSERT(nt_skeletal_mix(&input, 1, g_defaults, J, g_defaults));
    ASSERT_TRAPPED_ON("nt_skeletal_poses_disjoint(out, defaults, joint_count)");
}

void test_override_traps_on_alpha_or_mask_out_of_range(void) {
    nt_skeletal_trs_t base[J];
    nt_skeletal_trs_t top[J];
    nt_skeletal_trs_t out[J];
    make_pose(base, 0.0F);
    make_pose(top, 1.0F);
    NT_TEST_EXPECT_ASSERT(nt_skeletal_override(base, top, NULL, 1.5F, J, out));
    ASSERT_TRAPPED_ON("alpha >= 0.0F && alpha <= 1.0F");
    NT_TEST_EXPECT_ASSERT(nt_skeletal_override(base, top, NULL, NAN, J, out));
    ASSERT_TRAPPED_ON("alpha >= 0.0F && alpha <= 1.0F");
    const float nan_mask[J] = {0.0F, NAN, 1.0F, 0.0F};
    NT_TEST_EXPECT_ASSERT(nt_skeletal_override(base, top, nan_mask, 1.0F, J, out));
    ASSERT_TRAPPED_ON("mask[j] >= 0.0F && mask[j] <= 1.0F");
    const float mask[J] = {0.0F, 1.0F, 1.01F, 0.0F};
    NT_TEST_EXPECT_ASSERT(nt_skeletal_override(base, top, mask, 1.0F, J, out));
    ASSERT_TRAPPED_ON("mask[j] >= 0.0F && mask[j] <= 1.0F");
}

void test_override_traps_when_the_output_overlaps_top_or_part_of_base(void) {
    nt_skeletal_trs_t base[J + 1];
    nt_skeletal_trs_t top[J];
    make_pose(base, 0.0F);
    make_pose(top, 1.0F);
    NT_TEST_EXPECT_ASSERT(nt_skeletal_override(base, top, NULL, 0.5F, J, top));
    ASSERT_TRAPPED_ON("nt_skeletal_poses_disjoint(out, top, joint_count)");
    NT_TEST_EXPECT_ASSERT(nt_skeletal_override(base, top, NULL, 0.5F, J, base + 1));
    ASSERT_TRAPPED_ON("out == base || nt_skeletal_poses_disjoint(out, base, joint_count)");
}
#endif
// #endregion

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_negating_any_input_rotation_including_the_first_gives_the_same_mix);
    RUN_TEST(test_an_exactly_orthogonal_pair_takes_the_canonical_sign);
    RUN_TEST(test_an_exact_tie_with_nonzero_w_makes_w_positive);
    RUN_TEST(test_rotations_weigh_by_gain_times_joint_weight);
    RUN_TEST(test_alignment_follows_the_running_sum_in_supplied_order);
    RUN_TEST(test_plus_and_minus_170_degrees_average_to_180);
    RUN_TEST(test_zero_total_influence_copies_the_defaults);
    RUN_TEST(test_a_zero_influence_input_reads_nothing_from_its_pose);
    RUN_TEST(test_zero_weights_on_one_input_leave_the_others_normalized);
    RUN_TEST(test_a_tiny_or_huge_gain_contributes_fully_and_by_ratio);
    RUN_TEST(test_a_fading_partial_input_still_poses_the_joints_it_owns);
    RUN_TEST(test_asymmetric_parent_child_weights_keep_the_chain_attached);
    RUN_TEST(test_partial_weights_give_fixed_coefficients_through_a_crossfade);
    RUN_TEST(test_override_strength_ignores_the_gains_inside_the_base);
    RUN_TEST(test_override_endpoints_copy_exactly);
    RUN_TEST(test_override_in_place_matches_a_separate_output);
    RUN_TEST(test_override_blends_rotation_the_short_way_and_lerps_scale);
    RUN_TEST(test_override_reads_nothing_from_top_where_the_mask_is_zero);
    RUN_TEST(test_interruption_recipe_is_continuous_at_the_handoff_and_reuses_one_snapshot);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_mix_traps_on_a_gain_outside_its_range);
    RUN_TEST(test_mix_traps_on_null_inputs_or_pose);
#if NT_SKELETAL_CHECKS
    RUN_TEST(test_mix_checks_trap_on_an_infinite_weight_or_a_non_unit_rotation);
    RUN_TEST(test_override_checks_trap_on_a_blended_joint_that_is_not_a_unit_rotation);
#endif
    RUN_TEST(test_mix_traps_on_a_negative_or_nan_weight);
    RUN_TEST(test_mix_traps_when_the_output_overlaps_an_input);
    RUN_TEST(test_override_traps_on_alpha_or_mask_out_of_range);
    RUN_TEST(test_override_traps_when_the_output_overlaps_top_or_part_of_base);
#endif
    return UNITY_END();
}
