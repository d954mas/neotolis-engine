/* System and engine headers before Unity: <stdnoreturn.h> and the Windows SDK
 * clash over __declspec(noreturn) in the other order. */
#include <math.h>
#include <stddef.h>
#include <string.h>

#include "skeletal/nt_skeletal.h"

#include "unity.h"

#include "test_helpers/nt_assert_trap.h"

/* Unity is built with UNITY_EXCLUDE_FLOAT, so float comparisons go through
 * fabsf like everywhere else in the suite. */
#define ASSERT_FLOAT_NEAR(expected, actual, tol) TEST_ASSERT_TRUE_MESSAGE(fabsf((expected) - (actual)) <= (tol), "float not within tolerance")
#define ASSERT_DOUBLE_NEAR(expected, actual, tol) TEST_ASSERT_TRUE_MESSAGE(fabs((expected) - (actual)) <= (tol), "double not within tolerance")

#define ASSERT_BITS_EQUAL(expected, actual, count) TEST_ASSERT_EQUAL_MEMORY(expected, actual, (count) * sizeof(float))

/*
 * One asymmetric clip covers every storage mode and gives each joint a
 * different mix, so a swapped joint index or component offset shows up:
 *
 *   joint 0: t sampled,  q sampled (row 0), s constant
 *   joint 1: t constant, q step,           s sampled
 *   joint 2: t step,     q sampled (row 1), s absent
 *   joint 3: t constant, q constant,       s step (first key at 0.3)
 *   joint 4: nothing at all
 *
 * Two rotation rows and two constant translations keep the row and table
 * indexing honest: a sampler that ignored the row index would still pass with
 * one row of each kind. The grid is binary exact (duration 1, 5 samples, step
 * 0.25) so grid times land on the stored samples bit for bit.
 */
enum { CLIP_JOINTS = 5, CLIP_SAMPLES = 5, CLIP_BLOCK_FLOATS = 14, CLIP_STEP_KEYS = 7 };
enum { BLOCK_Q0_OFFSET = 3, BLOCK_Q1_OFFSET = 7, BLOCK_S_OFFSET = 11 };

static float g_blocks[CLIP_SAMPLES * CLIP_BLOCK_FLOATS];
static uint16_t g_t_joint[1] = {0};
static uint16_t g_q_joint[2] = {0, 2};
static uint16_t g_s_joint[1] = {1};
static uint16_t g_ct_joint[2] = {1, 3};
static float g_ct[6] = {-2.5F, 7.25F, 0.125F, 11.5F, -0.75F, 4.0F};
static uint16_t g_cq_joint[1] = {3};
static float g_cq[4];
static uint16_t g_cs_joint[1] = {0};
static float g_cs[3] = {2.0F, 0.5F, 3.0F};
static nt_skeletal_step_t g_steps[3];
static float g_step_times[CLIP_STEP_KEYS];
static float g_step_values[CLIP_STEP_KEYS * 4];
static nt_skeletal_clip_t g_clip;
static nt_skeletal_trs_t g_defaults[CLIP_JOINTS];

static void quat_axis_angle(float *q, float ax, float ay, float az, float degrees) {
    const float len = sqrtf((ax * ax) + (ay * ay) + (az * az));
    const float half = (degrees * 3.14159265358979F / 180.0F) * 0.5F;
    const float s = sinf(half) / len;
    q[0] = ax * s;
    q[1] = ay * s;
    q[2] = az * s;
    q[3] = cosf(half);
}

/* Every default rotation is a distinct non-identity unit quaternion, so an
 * "absent channel keeps the default" check cannot pass on an identity the clip
 * would have produced anyway. */
static void build_defaults(nt_skeletal_trs_t *d, uint16_t count) {
    for (uint16_t j = 0; j < count; ++j) {
        d[j].t[0] = 100.0F + (float)j;
        d[j].t[1] = 200.0F + (float)j;
        d[j].t[2] = 300.0F + (float)j;
        quat_axis_angle(d[j].q, 1.0F, 0.0F, 0.0F, 15.0F + (10.0F * (float)j));
        d[j].s[0] = 1.0F + (float)j;
        d[j].s[1] = 2.0F + (float)j;
        d[j].s[2] = 3.0F + (float)j;
    }
}

static void set_step_key(uint32_t key, float time, float a, float b, float c, float d) {
    g_step_times[key] = time;
    g_step_values[(key * 4U) + 0U] = a;
    g_step_values[(key * 4U) + 1U] = b;
    g_step_values[(key * 4U) + 2U] = c;
    g_step_values[(key * 4U) + 3U] = d;
}

static void build_clip(void) {
    for (uint32_t k = 0; k < CLIP_SAMPLES; ++k) {
        float *blk = g_blocks + ((size_t)k * CLIP_BLOCK_FLOATS);
        blk[0] = (float)k;
        blk[1] = ((float)k * 2.0F) + 0.5F;
        blk[2] = (float)k * -0.25F;
        quat_axis_angle(blk + BLOCK_Q0_OFFSET, 0.0F, 1.0F, 0.0F, (float)k * 20.0F);
        /* Row 1 turns about z in 22.5 deg steps: the nlerp midpoint of two such
         * rotations is the rotation at their mean angle, a value the test can
         * write down without reproducing the kernel. */
        quat_axis_angle(blk + BLOCK_Q1_OFFSET, 0.0F, 0.0F, 1.0F, (float)k * 22.5F);
        blk[BLOCK_S_OFFSET + 0] = 1.0F + ((float)k * 0.5F);
        blk[BLOCK_S_OFFSET + 1] = 2.0F - ((float)k * 0.25F);
        blk[BLOCK_S_OFFSET + 2] = 0.5F + ((float)k * 0.125F);
    }

    quat_axis_angle(g_cq, 0.0F, 0.0F, 1.0F, 30.0F);

    quat_axis_angle(g_step_values + 0, 1.0F, 0.0F, 0.0F, 0.0F);
    g_step_times[0] = 0.0F;
    quat_axis_angle(g_step_values + 4, 1.0F, 0.0F, 0.0F, 45.0F);
    g_step_times[1] = 0.25F;
    quat_axis_angle(g_step_values + 8, 1.0F, 0.0F, 0.0F, 90.0F);
    g_step_times[2] = 0.6F;
    set_step_key(3, 0.0F, 1.0F, 2.0F, 3.0F, 0.0F);
    set_step_key(4, 0.5F, -4.0F, -5.0F, -6.0F, 0.0F);
    set_step_key(5, 0.3F, 1.5F, 2.5F, 3.5F, 0.0F);
    set_step_key(6, 0.8F, 0.25F, 0.5F, 0.75F, 0.0F);

    g_steps[0].first = 0;
    g_steps[0].count = 3;
    g_steps[0].joint = 1;
    g_steps[0].channel = 1;
    /* joint 2 translation, then joint 3 scale. */
    g_steps[1].first = 3;
    g_steps[1].count = 2;
    g_steps[1].joint = 2;
    g_steps[1].channel = 0;
    g_steps[2].first = 5;
    g_steps[2].count = 2;
    g_steps[2].joint = 3;
    g_steps[2].channel = 2;

    const nt_skeletal_clip_t clip = {
        .rig_compat_id = {.value = 0x0123456789ABCDEFULL},
        .duration = 1.0,
        .inv_step = (double)(CLIP_SAMPLES - 1),
        .blocks = g_blocks,
        .t_joint = g_t_joint,
        .q_joint = g_q_joint,
        .s_joint = g_s_joint,
        .ct_joint = g_ct_joint,
        .ct = g_ct,
        .cq_joint = g_cq_joint,
        .cq = g_cq,
        .cs_joint = g_cs_joint,
        .cs = g_cs,
        .steps = g_steps,
        .step_times = g_step_times,
        .step_values = g_step_values,
        .sample_count = CLIP_SAMPLES,
        .block_floats = CLIP_BLOCK_FLOATS,
        .n_steps = 3,
        .joint_count = CLIP_JOINTS,
        .n_t = 1,
        .n_q = 2,
        .n_s = 1,
        .n_ct = 2,
        .n_cq = 1,
        .n_cs = 1,
    };
    g_clip = clip;
}

void setUp(void) {
    build_clip();
    build_defaults(g_defaults, CLIP_JOINTS);
}

void tearDown(void) {}

static const float *block_of(uint32_t k) { return g_blocks + ((size_t)k * CLIP_BLOCK_FLOATS); }

/* ---- Channel modes ---- */

void test_absent_channels_take_the_defaults(void) {
    nt_skeletal_trs_t out[CLIP_JOINTS];
    nt_skeletal_sample(&g_clip, 0.4, g_defaults, out);

    ASSERT_BITS_EQUAL(g_defaults[4].t, out[4].t, 3);
    ASSERT_BITS_EQUAL(g_defaults[4].q, out[4].q, 4);
    ASSERT_BITS_EQUAL(g_defaults[2].s, out[2].s, 3);
}

void test_constant_channels_copy_their_value(void) {
    nt_skeletal_trs_t out[CLIP_JOINTS];
    nt_skeletal_sample(&g_clip, 0.4, g_defaults, out);

    ASSERT_BITS_EQUAL(g_ct, out[1].t, 3);
    ASSERT_BITS_EQUAL(g_ct + 3, out[3].t, 3);
    ASSERT_BITS_EQUAL(g_cq, out[3].q, 4);
    ASSERT_BITS_EQUAL(g_cs, out[0].s, 3);
}

void test_sampled_translation_and_scale_lerp_inside_an_interval(void) {
    nt_skeletal_trs_t out[CLIP_JOINTS];
    /* Halfway between sample 0 and sample 1 of the 0.25 s grid. */
    nt_skeletal_sample(&g_clip, 0.125, g_defaults, out);

    const float *a = block_of(0);
    const float *b = block_of(1);
    for (int c = 0; c < 3; ++c) {
        ASSERT_FLOAT_NEAR((a[c] + b[c]) * 0.5F, out[0].t[c], 1e-6F);
        ASSERT_FLOAT_NEAR((a[BLOCK_S_OFFSET + c] + b[BLOCK_S_OFFSET + c]) * 0.5F, out[1].s[c], 1e-6F);
    }
}

/* The second rotation row must come from its own block slot and land on its own
 * joint: 22.5 deg and 45 deg about z meet at 33.75 deg about z. */
void test_the_second_sampled_rotation_row_interpolates_on_its_own_joint(void) {
    nt_skeletal_trs_t out[CLIP_JOINTS];
    nt_skeletal_sample(&g_clip, 0.375, g_defaults, out);

    ASSERT_FLOAT_NEAR(0.0F, out[2].q[0], 1e-6F);
    ASSERT_FLOAT_NEAR(0.0F, out[2].q[1], 1e-6F);
    ASSERT_FLOAT_NEAR(0.29028483F, out[2].q[2], 1e-6F);
    ASSERT_FLOAT_NEAR(0.95694034F, out[2].q[3], 1e-6F);
}

void test_grid_times_reproduce_the_stored_samples_exactly(void) {
    nt_skeletal_trs_t out[CLIP_JOINTS];
    for (uint32_t k = 0; k < CLIP_SAMPLES; ++k) {
        const double time = (double)k * 0.25;
        nt_skeletal_sample(&g_clip, time, g_defaults, out);
        ASSERT_BITS_EQUAL(block_of(k), out[0].t, 3);
        ASSERT_BITS_EQUAL(block_of(k) + BLOCK_Q0_OFFSET, out[0].q, 4);
        ASSERT_BITS_EQUAL(block_of(k) + BLOCK_Q1_OFFSET, out[2].q, 4);
        ASSERT_BITS_EQUAL(block_of(k) + BLOCK_S_OFFSET, out[1].s, 3);
    }
}

void test_the_end_of_the_clip_is_the_last_sample(void) {
    nt_skeletal_trs_t out[CLIP_JOINTS];
    nt_skeletal_sample(&g_clip, g_clip.duration, g_defaults, out);
    ASSERT_BITS_EQUAL(block_of(CLIP_SAMPLES - 1), out[0].t, 3);
    ASSERT_BITS_EQUAL(block_of(CLIP_SAMPLES - 1) + BLOCK_Q0_OFFSET, out[0].q, 4);
    ASSERT_BITS_EQUAL(block_of(CLIP_SAMPLES - 1) + BLOCK_Q1_OFFSET, out[2].q, 4);
}

/* ---- A grid whose step is not a binary fraction ---- */

enum { GRID31_SAMPLES = 31 };
static float g_grid31[GRID31_SAMPLES * 3];
static uint16_t g_grid31_joint[1] = {0};

static void build_grid31(nt_skeletal_clip_t *clip, double duration) {
    for (uint32_t k = 0; k < GRID31_SAMPLES; ++k) {
        g_grid31[(k * 3U) + 0U] = (float)k * 0.1F;
        g_grid31[(k * 3U) + 1U] = 5.0F - ((float)k * 0.2F);
        g_grid31[(k * 3U) + 2U] = (float)k * (float)k * 0.01F;
    }
    const nt_skeletal_clip_t c = {
        .duration = duration,
        .inv_step = (double)(GRID31_SAMPLES - 1) / duration,
        .blocks = g_grid31,
        .t_joint = g_grid31_joint,
        .sample_count = GRID31_SAMPLES,
        .block_floats = 3,
        .joint_count = 1,
        .n_t = 1,
    };
    *clip = c;
}

void test_a_non_binary_grid_reproduces_its_samples_within_tolerance(void) {
    nt_skeletal_clip_t clip;
    build_grid31(&clip, 1.0);
    nt_skeletal_trs_t defaults;
    build_defaults(&defaults, 1);

    nt_skeletal_trs_t out;
    for (uint32_t k = 0; k < GRID31_SAMPLES; ++k) {
        nt_skeletal_sample(&clip, (double)k / (double)(GRID31_SAMPLES - 1), &defaults, &out);
        for (int c = 0; c < 3; ++c) {
            ASSERT_FLOAT_NEAR(g_grid31[(k * 3U) + (uint32_t)c], out.t[c], 1e-6F);
        }
    }
}

/* 30 intervals over a duration that is neither a binary fraction nor a whole
 * number: duration * inv_step lands a ulp off the last sample, and only the
 * clamp keeps the end of the clip an exact copy of the stored block. */
void test_the_end_of_a_non_binary_clip_is_still_the_last_block(void) {
    nt_skeletal_clip_t clip;
    build_grid31(&clip, (double)0.7F);
    nt_skeletal_trs_t defaults;
    build_defaults(&defaults, 1);

    nt_skeletal_trs_t out;
    nt_skeletal_sample(&clip, clip.duration, &defaults, &out);
    ASSERT_BITS_EQUAL(g_grid31 + ((size_t)(GRID31_SAMPLES - 1U) * 3U), out.t, 3);
}

/* ---- Rotation interpolation ---- */

static float g_pair[2 * 4];
static uint16_t g_pair_joint[1] = {0};

static void build_quat_pair(nt_skeletal_clip_t *clip, const float *a, const float *b) {
    memcpy(g_pair, a, 4 * sizeof(float));
    memcpy(g_pair + 4, b, 4 * sizeof(float));
    const nt_skeletal_clip_t c = {
        .duration = 1.0,
        .inv_step = 1.0,
        .blocks = g_pair,
        .q_joint = g_pair_joint,
        .sample_count = 2,
        .block_floats = 4,
        .joint_count = 1,
        .n_q = 1,
    };
    *clip = c;
}

void test_a_negated_endpoint_gives_the_same_rotation(void) {
    nt_skeletal_trs_t defaults;
    build_defaults(&defaults, 1);

    float a[4];
    float b[4];
    quat_axis_angle(a, 0.3F, 1.0F, -0.4F, 25.0F);
    quat_axis_angle(b, 0.3F, 1.0F, -0.4F, 100.0F);
    float negated[4];
    for (int c = 0; c < 4; ++c) {
        negated[c] = -b[c];
    }

    nt_skeletal_clip_t clip;
    nt_skeletal_trs_t plain;
    build_quat_pair(&clip, a, b);
    nt_skeletal_sample(&clip, 0.37, &defaults, &plain);

    nt_skeletal_trs_t flipped;
    build_quat_pair(&clip, a, negated);
    nt_skeletal_sample(&clip, 0.37, &defaults, &flipped);

    ASSERT_BITS_EQUAL(plain.q, flipped.q, 4);
}

void test_a_wide_pair_takes_the_short_way(void) {
    nt_skeletal_trs_t defaults;
    build_defaults(&defaults, 1);

    float a[4];
    float b[4];
    quat_axis_angle(a, 0.0F, 0.0F, 1.0F, 170.0F);
    quat_axis_angle(b, 0.0F, 0.0F, 1.0F, -170.0F);

    nt_skeletal_clip_t clip;
    build_quat_pair(&clip, a, b);
    nt_skeletal_trs_t out;
    nt_skeletal_sample(&clip, 0.5, &defaults, &out);

    /* Halfway from +170 deg to -170 deg the short way is 180 deg about Z, not
     * the identity the long way would produce. */
    ASSERT_FLOAT_NEAR(0.0F, out.q[0], 1e-5F);
    ASSERT_FLOAT_NEAR(0.0F, out.q[1], 1e-5F);
    ASSERT_FLOAT_NEAR(1.0F, fabsf(out.q[2]), 1e-5F);
    ASSERT_FLOAT_NEAR(0.0F, out.q[3], 1e-5F);
}

/* ---- STEP tracks ---- */

/* Key timestamps are float, so the sample times come from the table itself: a
 * decimal literal like 0.6 is a different double than (double)0.6F. */
void test_step_channels_hold_the_last_key_at_or_before_the_time(void) {
    nt_skeletal_trs_t out[CLIP_JOINTS];

    nt_skeletal_sample(&g_clip, (double)g_step_times[1], g_defaults, out);
    ASSERT_BITS_EQUAL(g_step_values + 4, out[1].q, 4);

    nt_skeletal_sample(&g_clip, nextafter((double)g_step_times[1], 0.0), g_defaults, out);
    ASSERT_BITS_EQUAL(g_step_values + 0, out[1].q, 4);

    nt_skeletal_sample(&g_clip, (double)g_step_times[2], g_defaults, out);
    ASSERT_BITS_EQUAL(g_step_values + 8, out[1].q, 4);

    nt_skeletal_sample(&g_clip, 0.0, g_defaults, out);
    ASSERT_BITS_EQUAL(g_step_values + 12, out[2].t, 3);
    nt_skeletal_sample(&g_clip, (double)g_step_times[4], g_defaults, out);
    ASSERT_BITS_EQUAL(g_step_values + 16, out[2].t, 3);
    nt_skeletal_sample(&g_clip, nextafter((double)g_step_times[4], 0.0), g_defaults, out);
    ASSERT_BITS_EQUAL(g_step_values + 12, out[2].t, 3);
}

void test_a_time_before_the_first_step_key_holds_that_key(void) {
    nt_skeletal_trs_t out[CLIP_JOINTS];
    nt_skeletal_sample(&g_clip, 0.0, g_defaults, out);
    ASSERT_BITS_EQUAL(g_step_values + 20, out[3].s, 3);

    nt_skeletal_sample(&g_clip, (double)g_step_times[6], g_defaults, out);
    ASSERT_BITS_EQUAL(g_step_values + 24, out[3].s, 3);
}

/* ---- One-sample clips ---- */

static uint16_t g_one_ct_joint[1] = {0};
static float g_one_ct[3] = {9.0F, -8.0F, 7.5F};
static float g_one_step_times[2] = {0.0F, 0.5F};
static float g_one_step_values[8] = {1.0F, 1.0F, 1.0F, 0.0F, 4.0F, 5.0F, 6.0F, 0.0F};
static nt_skeletal_step_t g_one_step[1];

void test_a_single_sample_clip_of_duration_zero_applies_its_constants(void) {
    nt_skeletal_trs_t defaults;
    build_defaults(&defaults, 1);

    const nt_skeletal_clip_t clip = {
        .duration = 0.0,
        .ct_joint = g_one_ct_joint,
        .ct = g_one_ct,
        .sample_count = 1,
        .joint_count = 1,
        .n_ct = 1,
    };

    nt_skeletal_trs_t out;
    nt_skeletal_sample(&clip, 0.0, &defaults, &out);
    ASSERT_BITS_EQUAL(g_one_ct, out.t, 3);
    ASSERT_BITS_EQUAL(defaults.q, out.q, 4);
    ASSERT_BITS_EQUAL(defaults.s, out.s, 3);
}

void test_a_single_sample_clip_can_still_have_a_duration_and_step_keys(void) {
    nt_skeletal_trs_t defaults;
    build_defaults(&defaults, 1);

    g_one_step[0].first = 0;
    g_one_step[0].count = 2;
    g_one_step[0].joint = 0;
    g_one_step[0].channel = 2;
    const nt_skeletal_clip_t clip = {
        .duration = 2.0,
        .steps = g_one_step,
        .step_times = g_one_step_times,
        .step_values = g_one_step_values,
        .sample_count = 1,
        .n_steps = 1,
        .joint_count = 1,
    };

    nt_skeletal_trs_t out;
    nt_skeletal_sample(&clip, 2.0, &defaults, &out);
    ASSERT_BITS_EQUAL(g_one_step_values + 4, out.s, 3);
    nt_skeletal_sample(&clip, 0.25, &defaults, &out);
    ASSERT_BITS_EQUAL(g_one_step_values + 0, out.s, 3);
}

/* ---- Object curve ---- */

static nt_skeletal_trs_t g_object_samples[2];
static float g_object_step_times[2] = {0.0F, 0.75F};
static float g_object_step_values[8] = {0.0F, 0.0F, 0.0F, 0.0F, 3.0F, -3.0F, 1.5F, 0.0F};

void test_a_null_object_curve_copies_the_defaults(void) {
    nt_skeletal_trs_t defaults;
    build_defaults(&defaults, 1);
    nt_skeletal_trs_t out;
    nt_skeletal_sample_object(NULL, 0.0, &defaults, &out);
    TEST_ASSERT_EQUAL_MEMORY(&defaults, &out, sizeof(out));
}

void test_an_absent_object_curve_copies_the_defaults(void) {
    nt_skeletal_trs_t defaults;
    build_defaults(&defaults, 1);
    const nt_skeletal_object_curve_t curve = {.duration = 1.0, .sample_count = 1};

    nt_skeletal_trs_t out;
    nt_skeletal_sample_object(&curve, 0.5, &defaults, &out);
    TEST_ASSERT_EQUAL_MEMORY(&defaults, &out, sizeof(out));
}

/* A curve with no channel is the same as no curve, so the caller owes it no
 * time range: a clip whose object curve is empty is sampled at the track time
 * of whatever else it is blended with. */
void test_an_absent_object_curve_accepts_a_time_past_its_duration(void) {
    nt_skeletal_trs_t defaults;
    build_defaults(&defaults, 1);
    const nt_skeletal_object_curve_t curve = {.duration = 1.0, .sample_count = 1};

    nt_skeletal_trs_t out;
    nt_skeletal_sample_object(&curve, 5.0, &defaults, &out);
    TEST_ASSERT_EQUAL_MEMORY(&defaults, &out, sizeof(out));
}

void test_a_constant_object_channel_copies_its_value(void) {
    nt_skeletal_trs_t defaults;
    build_defaults(&defaults, 1);

    nt_skeletal_object_curve_t curve = {.duration = 1.0, .sample_count = 1};
    curve.mode[0] = NT_SKELETAL_CHANNEL_CONSTANT;
    curve.constant.t[0] = -1.0F;
    curve.constant.t[1] = 12.0F;
    curve.constant.t[2] = 0.5F;

    nt_skeletal_trs_t out;
    nt_skeletal_sample_object(&curve, 0.5, &defaults, &out);
    ASSERT_BITS_EQUAL(curve.constant.t, out.t, 3);
    ASSERT_BITS_EQUAL(defaults.q, out.q, 4);
    ASSERT_BITS_EQUAL(defaults.s, out.s, 3);
}

void test_a_sampled_object_curve_interpolates_and_hits_its_grid(void) {
    nt_skeletal_trs_t defaults;
    build_defaults(&defaults, 1);

    g_object_samples[0].t[0] = 0.0F;
    g_object_samples[0].t[1] = 4.0F;
    g_object_samples[0].t[2] = -2.0F;
    quat_axis_angle(g_object_samples[0].q, 0.0F, 1.0F, 0.0F, 0.0F);
    g_object_samples[1].t[0] = 10.0F;
    g_object_samples[1].t[1] = 0.0F;
    g_object_samples[1].t[2] = 2.0F;
    quat_axis_angle(g_object_samples[1].q, 0.0F, 1.0F, 0.0F, 60.0F);

    nt_skeletal_object_curve_t curve = {.sampled = g_object_samples, .duration = 2.0, .inv_step = 0.5, .sample_count = 2};
    curve.mode[0] = NT_SKELETAL_CHANNEL_SAMPLED;
    curve.mode[1] = NT_SKELETAL_CHANNEL_SAMPLED;

    nt_skeletal_trs_t out;
    nt_skeletal_sample_object(&curve, 1.0, &defaults, &out);
    ASSERT_FLOAT_NEAR(5.0F, out.t[0], 1e-6F);
    ASSERT_FLOAT_NEAR(2.0F, out.t[1], 1e-6F);
    ASSERT_FLOAT_NEAR(0.0F, out.t[2], 1e-6F);
    ASSERT_BITS_EQUAL(defaults.s, out.s, 3);

    nt_skeletal_sample_object(&curve, 0.0, &defaults, &out);
    ASSERT_BITS_EQUAL(g_object_samples[0].t, out.t, 3);
    ASSERT_BITS_EQUAL(g_object_samples[0].q, out.q, 4);

    nt_skeletal_sample_object(&curve, 2.0, &defaults, &out);
    ASSERT_BITS_EQUAL(g_object_samples[1].t, out.t, 3);
    ASSERT_BITS_EQUAL(g_object_samples[1].q, out.q, 4);
}

void test_a_step_object_channel_holds_the_last_key(void) {
    nt_skeletal_trs_t defaults;
    build_defaults(&defaults, 1);

    nt_skeletal_object_curve_t curve = {.step_times = g_object_step_times, .step_values = g_object_step_values, .duration = 1.0, .sample_count = 1};
    curve.mode[0] = NT_SKELETAL_CHANNEL_STEP;
    curve.step_first[0] = 0;
    curve.step_count[0] = 2;

    nt_skeletal_trs_t out;
    nt_skeletal_sample_object(&curve, 0.74, &defaults, &out);
    ASSERT_BITS_EQUAL(g_object_step_values + 0, out.t, 3);
    nt_skeletal_sample_object(&curve, (double)g_object_step_times[1], &defaults, &out);
    ASSERT_BITS_EQUAL(g_object_step_values + 4, out.t, 3);
}

/* ---- Track clock ---- */

static nt_skeletal_track_t make_track(double time, double duration, float speed, uint32_t flags) {
    nt_skeletal_track_t t = {0};
    t.time = time;
    t.duration = duration;
    t.speed = speed;
    t.flags = flags;
    return t;
}

void test_a_looping_track_wraps_forward(void) {
    nt_skeletal_track_t tracks[2];
    tracks[0] = make_track(1.5, 2.0, 1.0F, NT_SKELETAL_TRACK_OCCUPIED | NT_SKELETAL_TRACK_LOOPING);
    /* 2.5 durations in one step: the floor/modulo takes them all at once. */
    tracks[1] = make_track(0.0, 2.0, 5.0F, NT_SKELETAL_TRACK_OCCUPIED | NT_SKELETAL_TRACK_LOOPING);

    nt_skeletal_tracks_advance(tracks, 2, 1.0);
    ASSERT_DOUBLE_NEAR(0.5, tracks[0].time, 1e-12);
    ASSERT_DOUBLE_NEAR(1.0, tracks[1].time, 1e-12);
}

void test_a_looping_track_wraps_backwards_and_lands_on_zero_at_a_full_cycle(void) {
    nt_skeletal_track_t tracks[2];
    tracks[0] = make_track(0.5, 2.0, -1.0F, NT_SKELETAL_TRACK_OCCUPIED | NT_SKELETAL_TRACK_LOOPING);
    tracks[1] = make_track(0.0, 2.0, 2.0F, NT_SKELETAL_TRACK_OCCUPIED | NT_SKELETAL_TRACK_LOOPING);

    nt_skeletal_tracks_advance(tracks, 2, 1.0);
    ASSERT_DOUBLE_NEAR(1.5, tracks[0].time, 1e-12);
    ASSERT_DOUBLE_NEAR(0.0, tracks[1].time, 1e-12);
}

void test_a_non_looping_track_clamps_at_both_ends(void) {
    nt_skeletal_track_t tracks[2];
    tracks[0] = make_track(1.9, 2.0, 1.0F, NT_SKELETAL_TRACK_OCCUPIED);
    tracks[1] = make_track(0.4, 2.0, -3.0F, NT_SKELETAL_TRACK_OCCUPIED);

    nt_skeletal_tracks_advance(tracks, 2, 1.0);
    ASSERT_DOUBLE_NEAR(2.0, tracks[0].time, 1e-12);
    ASSERT_DOUBLE_NEAR(0.0, tracks[1].time, 1e-12);
}

void test_speed_zero_holds_and_duration_zero_stays_at_zero(void) {
    nt_skeletal_track_t tracks[2];
    tracks[0] = make_track(1.234, 2.0, 0.0F, NT_SKELETAL_TRACK_OCCUPIED | NT_SKELETAL_TRACK_LOOPING);
    tracks[1] = make_track(5.0, 0.0, 1.0F, NT_SKELETAL_TRACK_OCCUPIED);

    nt_skeletal_tracks_advance(tracks, 2, 10.0);
    ASSERT_DOUBLE_NEAR(1.234, tracks[0].time, 1e-12);
    ASSERT_DOUBLE_NEAR(0.0, tracks[1].time, 1e-12);
}

/* Reversing the clock must undo the same number of steps exactly, which is the
 * property a scrubbing or ping-pong game relies on. The numbers are binary
 * exact, so the wrap arithmetic has to return the original time bit for bit. */
void test_reversing_the_speed_returns_a_track_to_its_start(void) {
    const uint32_t k_steps = 7;
    nt_skeletal_track_t tracks[2];
    tracks[0] = make_track(0.5, 2.0, 1.0F, NT_SKELETAL_TRACK_OCCUPIED | NT_SKELETAL_TRACK_LOOPING);
    /* Non-looping: the seven forward steps of 0.125 s stay inside [0, 2], so no
     * clamp swallows part of the excursion. */
    tracks[1] = make_track(0.5, 2.0, 0.5F, NT_SKELETAL_TRACK_OCCUPIED);

    for (uint32_t i = 0; i < k_steps; ++i) {
        nt_skeletal_tracks_advance(tracks, 2, 0.25);
    }
    ASSERT_DOUBLE_NEAR(0.25, tracks[0].time, 1e-12); /* 0.5 + 1.75 wraps once */
    ASSERT_DOUBLE_NEAR(1.375, tracks[1].time, 1e-12);

    tracks[0].speed = -1.0F;
    tracks[1].speed = -0.5F;
    for (uint32_t i = 0; i < k_steps; ++i) {
        nt_skeletal_tracks_advance(tracks, 2, 0.25);
    }
    TEST_ASSERT_TRUE_MESSAGE(tracks[0].time == 0.5, "the looping track returns to its start");
    TEST_ASSERT_TRUE_MESSAGE(tracks[1].time == 0.5, "the non-looping track returns to its start");
}

/* Five cycles of reverse in one step: the floor/modulo takes them all at once
 * instead of subtracting duration repeatedly. */
void test_a_looping_track_wraps_several_cycles_backwards(void) {
    nt_skeletal_track_t track = make_track(0.5, 2.0, -5.0F, NT_SKELETAL_TRACK_OCCUPIED | NT_SKELETAL_TRACK_LOOPING);

    nt_skeletal_tracks_advance(&track, 1, 1.0);
    ASSERT_DOUBLE_NEAR(1.5, track.time, 1e-12);
}

/* Non-binary duration and step: after three reverse frames the quotient and the
 * product round so that the residue comes back as the duration itself, which is
 * the start of the next cycle, not a time past its end. */
void test_a_looping_track_restarts_when_the_residue_lands_on_the_duration(void) {
    nt_skeletal_track_t track = make_track(0.0, 0.35, -7.0F, NT_SKELETAL_TRACK_OCCUPIED | NT_SKELETAL_TRACK_LOOPING);

    for (uint32_t i = 0; i < 3U; ++i) {
        nt_skeletal_tracks_advance(&track, 1, 1.0 / 60.0);
        TEST_ASSERT_TRUE_MESSAGE(track.time >= 0.0 && track.time < track.duration, "a looping track stays inside [0, duration)");
    }
    TEST_ASSERT_TRUE_MESSAGE(track.time == 0.0, "the residue of a full reverse cycle restarts the cycle at 0");
}

void test_an_unoccupied_track_is_untouched(void) {
    nt_skeletal_track_t tracks[2];
    tracks[0] = make_track(7.5, 2.0, 1.0F, 0);
    tracks[1] = make_track(0.25, 2.0, 1.0F, NT_SKELETAL_TRACK_OCCUPIED);

    nt_skeletal_tracks_advance(tracks, 2, 0.5);
    ASSERT_DOUBLE_NEAR(7.5, tracks[0].time, 1e-12);
    ASSERT_DOUBLE_NEAR(0.75, tracks[1].time, 1e-12);
}

/* ---- Contracts ---- */

#if NT_ASSERT_MODE == NT_ASSERT_FULL
/* Which assert fired is part of the claim: a trap test that only sees "some
 * assert" passes just as happily on an unrelated precondition. */
#define ASSERT_TRAPPED_ON(fragment) TEST_ASSERT_TRUE_MESSAGE(strstr(nt_test_assert_last_expr, (fragment)) != NULL, "a different NT_ASSERT fired: " fragment)

void test_sample_traps_outside_the_clip(void) {
    nt_skeletal_trs_t out[CLIP_JOINTS];
    NT_TEST_EXPECT_ASSERT(nt_skeletal_sample(&g_clip, -0.1, g_defaults, out));
    ASSERT_TRAPPED_ON("time >= 0.0 && time <= clip->duration");
    NT_TEST_EXPECT_ASSERT(nt_skeletal_sample(&g_clip, 1.5, g_defaults, out));
    ASSERT_TRAPPED_ON("time >= 0.0 && time <= clip->duration");
}

void test_sample_traps_when_the_output_overlaps_the_defaults(void) {
    nt_skeletal_trs_t out[CLIP_JOINTS];
    memcpy(out, g_defaults, sizeof(out));
    /* The overlap assert fires before the first read or write through the
     * restrict pointers, so passing one buffer as both is defined here. */
    NT_TEST_EXPECT_ASSERT(nt_skeletal_sample(&g_clip, 0.0, out, out));
    ASSERT_TRAPPED_ON("(uintptr_t)(defaults + clip->joint_count) <= (uintptr_t)out");
}

void test_tracks_advance_traps_on_negative_dt(void) {
    nt_skeletal_track_t track = make_track(0.0, 1.0, 1.0F, NT_SKELETAL_TRACK_OCCUPIED);
    NT_TEST_EXPECT_ASSERT(nt_skeletal_tracks_advance(&track, 1, -1.0));
    ASSERT_TRAPPED_ON("dt >= 0.0");
}

/* A non-finite dt reaches the same int64 conversion a non-finite speed does. */
void test_tracks_advance_traps_on_a_non_finite_dt(void) {
    nt_skeletal_track_t track = make_track(0.0, 1.0, 1.0F, NT_SKELETAL_TRACK_OCCUPIED | NT_SKELETAL_TRACK_LOOPING);
    NT_TEST_EXPECT_ASSERT(nt_skeletal_tracks_advance(&track, 1, (double)INFINITY));
    ASSERT_TRAPPED_ON("dt - dt");
}

/* A non-finite speed would make the cycle count's conversion to int64 undefined,
 * which traps on wasm instead of wrapping. */
void test_tracks_advance_traps_on_a_non_finite_speed(void) {
    nt_skeletal_track_t track = make_track(0.0, 1.0, 0.0F, NT_SKELETAL_TRACK_OCCUPIED | NT_SKELETAL_TRACK_LOOPING);
    track.speed = NAN;
    NT_TEST_EXPECT_ASSERT(nt_skeletal_tracks_advance(&track, 1, 1.0));
    ASSERT_TRAPPED_ON("track->speed - track->speed");

    track.speed = INFINITY;
    NT_TEST_EXPECT_ASSERT(nt_skeletal_tracks_advance(&track, 1, 1.0));
    ASSERT_TRAPPED_ON("track->speed - track->speed");
}
#endif

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_absent_channels_take_the_defaults);
    RUN_TEST(test_constant_channels_copy_their_value);
    RUN_TEST(test_sampled_translation_and_scale_lerp_inside_an_interval);
    RUN_TEST(test_the_second_sampled_rotation_row_interpolates_on_its_own_joint);
    RUN_TEST(test_grid_times_reproduce_the_stored_samples_exactly);
    RUN_TEST(test_the_end_of_the_clip_is_the_last_sample);
    RUN_TEST(test_a_non_binary_grid_reproduces_its_samples_within_tolerance);
    RUN_TEST(test_the_end_of_a_non_binary_clip_is_still_the_last_block);
    RUN_TEST(test_a_negated_endpoint_gives_the_same_rotation);
    RUN_TEST(test_a_wide_pair_takes_the_short_way);
    RUN_TEST(test_step_channels_hold_the_last_key_at_or_before_the_time);
    RUN_TEST(test_a_time_before_the_first_step_key_holds_that_key);
    RUN_TEST(test_a_single_sample_clip_of_duration_zero_applies_its_constants);
    RUN_TEST(test_a_single_sample_clip_can_still_have_a_duration_and_step_keys);
    RUN_TEST(test_a_null_object_curve_copies_the_defaults);
    RUN_TEST(test_an_absent_object_curve_copies_the_defaults);
    RUN_TEST(test_an_absent_object_curve_accepts_a_time_past_its_duration);
    RUN_TEST(test_a_constant_object_channel_copies_its_value);
    RUN_TEST(test_a_sampled_object_curve_interpolates_and_hits_its_grid);
    RUN_TEST(test_a_step_object_channel_holds_the_last_key);
    RUN_TEST(test_a_looping_track_wraps_forward);
    RUN_TEST(test_a_looping_track_wraps_backwards_and_lands_on_zero_at_a_full_cycle);
    RUN_TEST(test_a_non_looping_track_clamps_at_both_ends);
    RUN_TEST(test_speed_zero_holds_and_duration_zero_stays_at_zero);
    RUN_TEST(test_reversing_the_speed_returns_a_track_to_its_start);
    RUN_TEST(test_a_looping_track_wraps_several_cycles_backwards);
    RUN_TEST(test_a_looping_track_restarts_when_the_residue_lands_on_the_duration);
    RUN_TEST(test_an_unoccupied_track_is_untouched);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_sample_traps_outside_the_clip);
    RUN_TEST(test_sample_traps_when_the_output_overlaps_the_defaults);
    RUN_TEST(test_tracks_advance_traps_on_negative_dt);
    RUN_TEST(test_tracks_advance_traps_on_a_non_finite_dt);
    RUN_TEST(test_tracks_advance_traps_on_a_non_finite_speed);
#endif
    return UNITY_END();
}
