/* Builder -> wire payload -> activator -> sampler. The other skeletal suites
 * either encode without decoding or feed the sampler hand-built runtime tables,
 * so an encoder/activator skew or a wrong runtime layout is invisible to them. */

/* System headers before Unity: <stdnoreturn.h> and the Windows SDK clash over
 * __declspec(noreturn) in the other order. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

/* clang-format off */
#include "skeletal_assets/nt_skeletal_assets.h"
#include "resource/nt_resource.h"
#include "hash/nt_hash.h"
#include "nt_builder.h"
#include "nt_pack_format.h"
#include "nt_skeletal_format.h"
#include "unity.h"
#include "test_helpers/nt_assert_trap.h"
/* clang-format on */

#define TMP_DIR "build/tests/tmp"

/* Unity is built with UNITY_EXCLUDE_FLOAT: exact expectations compare bits,
 * interpolated ones compare through fabsf. */
#define ASSERT_FLOAT_NEAR(expected, actual, tol) TEST_ASSERT_TRUE_MESSAGE(fabsf((expected) - (actual)) <= (tol), "float not within tolerance")
#define ASSERT_BITS_EQUAL(expected, actual, count) TEST_ASSERT_EQUAL_MEMORY(expected, actual, (size_t)(count) * sizeof(float))

// #region little-endian helpers
static uint32_t rd_u32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static void wr_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)(v >> 8);
}

static void wr_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
    p[2] = (uint8_t)((v >> 16) & 0xFFU);
    p[3] = (uint8_t)((v >> 24) & 0xFFU);
}

static void wr_f32(uint8_t *p, float v) {
    uint32_t bits = 0;
    memcpy(&bits, &v, sizeof(bits));
    wr_u32(p, bits);
}

/* Section descriptor s of a NANM payload. */
static uint8_t *section_desc(uint8_t *payload, uint32_t s) { return payload + sizeof(NtAnmHeader) + ((size_t)s * sizeof(NtAnmSection)); }
static uint32_t section_offset(const uint8_t *payload, uint32_t s) { return rd_u32(payload + sizeof(NtAnmHeader) + ((size_t)s * sizeof(NtAnmSection)) + 4U); }
// #endregion

// #region fixtures
/* Two roots and one three-deep chain, so both subtree rules are exercised. */
#define SKEL_JOINTS 4

static const uint16_t k_parent[SKEL_JOINTS] = {NT_SKELETAL_NO_PARENT, 0, 1, NT_SKELETAL_NO_PARENT};
static const uint16_t k_subtree_end[SKEL_JOINTS] = {3, 3, 3, 4};
static const uint32_t k_joint_id[SKEL_JOINTS] = {0x11111111U, 0x22222222U, 0x33333333U, 0x44444444U};

static const nt_skeletal_trs_t k_rest[SKEL_JOINTS] = {
    {{1.0F, 2.0F, 3.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}},
    {{0.0F, 0.5F, -0.25F}, {0.0F, 0.0F, -0.70710678F, 0.70710678F}, {2.0F, 1.0F, 0.5F}},
    {{-1.5F, 0.0F, 0.125F}, {0.70710678F, 0.0F, 0.0F, 0.70710678F}, {1.0F, 1.0F, 1.0F}},
    {{4.0F, -4.0F, 0.0F}, {0.0F, 1.0F, 0.0F, 0.0F}, {0.25F, 0.25F, 0.25F}},
};

#define FIXTURE_RIG_ID 0xABCDEF0123456789ULL

static nt_skeletal_skeleton_t fixture_skeleton(void) {
    return (nt_skeletal_skeleton_t){
        .rig_compat_id = (nt_hash64_t){.value = FIXTURE_RIG_ID},
        .parent = k_parent,
        .subtree_end = k_subtree_end,
        .joint_id = k_joint_id,
        .rest = k_rest,
        .joint_count = SKEL_JOINTS,
    };
}

#define SKIN_PALETTE 3

static const uint16_t k_remap[SKIN_PALETTE] = {0, 2, 1};

static const nt_skeletal_mat34_t k_inverse_bind[SKIN_PALETTE] = {
    {{{1.0F, 0.0F, 0.0F, -1.0F}, {0.0F, 1.0F, 0.0F, -2.0F}, {0.0F, 0.0F, 1.0F, -3.0F}}},
    {{{0.0F, -1.0F, 0.0F, 0.5F}, {1.0F, 0.0F, 0.0F, 0.25F}, {0.0F, 0.0F, 1.0F, 0.125F}}},
    {{{2.0F, 0.0F, 0.0F, 7.0F}, {0.0F, 2.0F, 0.0F, 8.0F}, {0.0F, 0.0F, 2.0F, 9.0F}}},
};

static nt_skin_binding_t fixture_binding(void) {
    return (nt_skin_binding_t){
        .rig_compat_id = (nt_hash64_t){.value = FIXTURE_RIG_ID},
        .remap = k_remap,
        .inverse_bind = k_inverse_bind,
        .palette_count = SKIN_PALETTE,
    };
}

/*
 * Asymmetric clip: a different mode per joint and component, plus an object
 * curve whose translation is sampled and whose rotation steps.
 *
 *   joint 0: t absent,   q sampled,  s constant
 *   joint 1: t sampled,  q constant, s absent
 *   joint 2: t step,     q absent,   s sampled
 *   object : t sampled,  q step,     s absent
 *
 * duration 1 over 5 samples, so every grid time is binary exact.
 */
#define CLIP_JOINTS 3
#define CLIP_SAMPLES 5
#define CLIP_CHANNELS (3 * (CLIP_JOINTS + 1))

static const float k_q_samples[CLIP_SAMPLES * 4] = {
    0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.38268343F, 0.92387953F, 0.0F, 0.0F, 0.70710678F, 0.70710678F, 0.0F, 0.0F, 0.92387953F, 0.38268343F, 0.0F, 0.0F, 1.0F, 0.0F,
};
static const float k_t_samples[CLIP_SAMPLES * 3] = {0.0F, 0.0F, 0.0F, 1.0F, 0.5F, 0.0F, 2.0F, 1.0F, 0.0F, 3.0F, 1.5F, 0.0F, 4.0F, 2.0F, 0.0F};
static const float k_s_samples[CLIP_SAMPLES * 3] = {1.0F, 1.0F, 1.0F, 1.1F, 1.0F, 0.9F, 1.2F, 1.0F, 0.8F, 1.3F, 1.0F, 0.7F, 1.4F, 1.0F, 0.6F};
static const float k_object_t[CLIP_SAMPLES * 3] = {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.25F, 0.0F, 0.0F, 0.5F, 0.0F, 0.0F, 0.75F, 0.0F, 0.0F, 1.0F};

static const float k_const_s0[4] = {2.0F, 3.0F, 4.0F, 0.0F};
static const float k_const_q1[4] = {0.0F, 0.0F, 0.0F, 1.0F};

/* joint 2 translation: three keys inside the duration. */
static const float k_step_times[3] = {0.0F, 0.4F, 0.8F};
static const float k_step_values[3 * 4] = {0.0F, 0.0F, 0.0F, 0.0F, 5.0F, 0.0F, 0.0F, 0.0F, 5.0F, 5.0F, 0.0F, 0.0F};

/* object rotation: identity, then a quarter turn about z at half time. */
static const float k_object_step_times[2] = {0.0F, 0.5F};
static const float k_object_step_values[2 * 4] = {0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.70710678F, 0.70710678F};

static void fixture_clip(nt_builder_clip_t *clip, nt_builder_anim_channel_t channels[CLIP_CHANNELS]) {
    memset(channels, 0, sizeof(nt_builder_anim_channel_t) * (size_t)CLIP_CHANNELS);

    channels[1].mode = NT_ANM_CHANNEL_SAMPLED;
    channels[1].samples = k_q_samples;
    channels[2].mode = NT_ANM_CHANNEL_CONSTANT;
    memcpy(channels[2].constant, k_const_s0, sizeof(k_const_s0));

    channels[3].mode = NT_ANM_CHANNEL_SAMPLED;
    channels[3].samples = k_t_samples;
    channels[4].mode = NT_ANM_CHANNEL_CONSTANT;
    memcpy(channels[4].constant, k_const_q1, sizeof(k_const_q1));

    channels[6].mode = NT_ANM_CHANNEL_STEP;
    channels[6].step_times = k_step_times;
    channels[6].step_values = k_step_values;
    channels[6].step_count = 3;
    channels[8].mode = NT_ANM_CHANNEL_SAMPLED;
    channels[8].samples = k_s_samples;

    channels[9].mode = NT_ANM_CHANNEL_SAMPLED;
    channels[9].samples = k_object_t;
    channels[10].mode = NT_ANM_CHANNEL_STEP;
    channels[10].step_times = k_object_step_times;
    channels[10].step_values = k_object_step_values;
    channels[10].step_count = 2;

    memset(clip, 0, sizeof(*clip));
    clip->rig_compat_id = FIXTURE_RIG_ID;
    clip->kind = NT_ANM_KIND_ABSOLUTE;
    clip->joint_count = CLIP_JOINTS;
    clip->sample_count = CLIP_SAMPLES;
    clip->duration = 1.0F;
    clip->r_joints = 3.5F;
    clip->r_root = 4.0F;
    clip->s_max = 2.5F;
    clip->bake_fps_min = 30.0F;
    clip->bake_reach = 1.25F;
    clip->channels = channels;
}

static uint8_t *encode_fixture_clip(uint32_t *out_size) {
    nt_builder_clip_t clip;
    nt_builder_anim_channel_t channels[CLIP_CHANNELS];
    fixture_clip(&clip, channels);
    uint8_t *payload = NULL;
    nt_builder_encode_clip(&clip, &payload, out_size);
    TEST_ASSERT_NOT_NULL(payload);
    return payload;
}

static void build_defaults(nt_skeletal_trs_t *out, uint16_t count) {
    for (uint16_t j = 0; j < count; ++j) {
        out[j].t[0] = 100.0F + (float)j;
        out[j].t[1] = 200.0F + (float)j;
        out[j].t[2] = 300.0F + (float)j;
        out[j].q[0] = 0.0F;
        out[j].q[1] = 0.0F;
        out[j].q[2] = 0.0F;
        out[j].q[3] = 1.0F;
        out[j].s[0] = 1.0F + (float)j;
        out[j].s[1] = 2.0F + (float)j;
        out[j].s[2] = 3.0F + (float)j;
    }
}
// #endregion

// #region unity fixture
void setUp(void) {
    nt_resource_init(NULL);
    const nt_skeletal_assets_desc_t desc = {.max_skeletons = 2, .max_skin_bindings = 2, .max_clips = 2};
    TEST_ASSERT_EQUAL(NT_OK, nt_skeletal_assets_init(&desc));
}

/* Resources first: its shutdown deactivates assets through this module. */
void tearDown(void) {
    nt_resource_shutdown();
    nt_skeletal_assets_shutdown();
}

/* Re-open the pools with a different capacity inside a test. */
static void reinit_assets(uint16_t max_clips) {
    nt_skeletal_assets_shutdown();
    const nt_skeletal_assets_desc_t desc = {.max_skeletons = 1, .max_skin_bindings = 1, .max_clips = max_clips};
    TEST_ASSERT_EQUAL(NT_OK, nt_skeletal_assets_init(&desc));
}

/* The views take a resource handle, so a directly activated handle is published
 * through a virtual pack. A virtual pack stores caller-owned handles and never
 * deactivates them, so the test keeps ownership. */
static nt_resource_t publish_handle(const char *name, uint8_t asset_type, uint32_t runtime_handle) {
    const nt_hash32_t pack_id = nt_hash32_str(name);
    const nt_hash64_t resource_id = nt_hash64_str(name);
    const nt_resource_t handle = nt_resource_request(resource_id, asset_type);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_create_pack(pack_id, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(pack_id, resource_id, asset_type, runtime_handle));
    nt_resource_step();
    TEST_ASSERT_TRUE(nt_resource_is_ready(handle));
    return handle;
}
// #endregion

// #region round trips
void test_skeleton_round_trip(void) {
    nt_skeletal_skeleton_t source = fixture_skeleton();
    uint8_t *payload = NULL;
    uint32_t size = 0;
    nt_builder_encode_skeleton(&source, &payload, &size);

    const uint32_t handle = nt_skeletal_assets_activate_skeleton(payload, size);
    TEST_ASSERT_EQUAL_UINT32(1, handle);

    /* Nothing reads the payload after activation, so the view must survive it. */
    memset(payload, 0xCD, size);

    const nt_skeletal_skeleton_t *view = nt_skeletal_assets_skeleton(publish_handle("rigs/hero.nskl", NT_ASSET_SKELETON, handle));
    TEST_ASSERT_EQUAL_UINT16(SKEL_JOINTS, view->joint_count);
    TEST_ASSERT_EQUAL_HEX64(FIXTURE_RIG_ID, view->rig_compat_id.value);
    for (uint16_t j = 0; j < SKEL_JOINTS; ++j) {
        TEST_ASSERT_EQUAL_UINT16(k_parent[j], view->parent[j]);
        TEST_ASSERT_EQUAL_UINT16(k_subtree_end[j], view->subtree_end[j]);
        TEST_ASSERT_EQUAL_HEX32(k_joint_id[j], view->joint_id[j]);
        ASSERT_BITS_EQUAL(k_rest[j].t, view->rest[j].t, 3);
        ASSERT_BITS_EQUAL(k_rest[j].q, view->rest[j].q, 4);
        ASSERT_BITS_EQUAL(k_rest[j].s, view->rest[j].s, 3);
    }

    nt_skeletal_assets_deactivate_skeleton(handle);
    free(payload);
}

void test_skin_binding_round_trip(void) {
    nt_skin_binding_t source = fixture_binding();
    uint8_t *payload = NULL;
    uint32_t size = 0;
    nt_builder_encode_skin_binding(&source, 1.75F, 4.5F, &payload, &size);

    const uint32_t handle = nt_skeletal_assets_activate_skin_binding(payload, size);
    TEST_ASSERT_EQUAL_UINT32(1, handle);
    memset(payload, 0xCD, size);

    const nt_skin_binding_t *view = nt_skeletal_assets_skin_binding(publish_handle("rigs/hero.nskn", NT_ASSET_SKIN_BINDING, handle));
    TEST_ASSERT_EQUAL_UINT16(SKIN_PALETTE, view->palette_count);
    TEST_ASSERT_EQUAL_HEX64(FIXTURE_RIG_ID, view->rig_compat_id.value);
    const float k_reach = 1.75F;
    const float k_any_pose_radius = 4.5F;
    ASSERT_BITS_EQUAL(&k_reach, &view->reach, 1);
    ASSERT_BITS_EQUAL(&k_any_pose_radius, &view->any_pose_radius, 1);
    for (uint16_t p = 0; p < SKIN_PALETTE; ++p) {
        TEST_ASSERT_EQUAL_UINT16(k_remap[p], view->remap[p]);
        ASSERT_BITS_EQUAL(&k_inverse_bind[p].r[0][0], &view->inverse_bind[p].r[0][0], 12);
    }

    nt_skeletal_assets_deactivate_skin_binding(handle);
    free(payload);
}

/* Table shapes first: a wrong row order or a mixed-up joint list would still
 * sample plausibly at some times. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_clip_round_trip_tables(void) {
    uint32_t size = 0;
    uint8_t *payload = encode_fixture_clip(&size);
    const uint32_t handle = nt_skeletal_assets_activate_clip(payload, size);
    TEST_ASSERT_EQUAL_UINT32(1, handle);
    memset(payload, 0xCD, size);

    const nt_skeletal_clip_t *clip = nt_skeletal_assets_clip(publish_handle("clips/a.nanm", NT_ASSET_CLIP, handle));
    TEST_ASSERT_EQUAL_HEX64(FIXTURE_RIG_ID, clip->rig_compat_id.value);
    TEST_ASSERT_EQUAL_HEX64(0, clip->additive_ref_id.value);
    TEST_ASSERT_EQUAL_UINT8(NT_ANM_KIND_ABSOLUTE, clip->kind);
    TEST_ASSERT_EQUAL_UINT16(CLIP_JOINTS, clip->joint_count);
    TEST_ASSERT_EQUAL_UINT32(CLIP_SAMPLES, clip->sample_count);
    TEST_ASSERT_TRUE(clip->duration == 1.0);
    TEST_ASSERT_TRUE(clip->inv_step == 4.0);
    TEST_ASSERT_EQUAL_UINT32(10, clip->block_floats);

    TEST_ASSERT_EQUAL_UINT16(1, clip->n_t);
    TEST_ASSERT_EQUAL_UINT16(1, clip->n_q);
    TEST_ASSERT_EQUAL_UINT16(1, clip->n_s);
    TEST_ASSERT_EQUAL_UINT16(1, clip->t_joint[0]);
    TEST_ASSERT_EQUAL_UINT16(0, clip->q_joint[0]);
    TEST_ASSERT_EQUAL_UINT16(2, clip->s_joint[0]);

    TEST_ASSERT_EQUAL_UINT16(0, clip->n_ct);
    TEST_ASSERT_EQUAL_UINT16(1, clip->n_cq);
    TEST_ASSERT_EQUAL_UINT16(1, clip->n_cs);
    TEST_ASSERT_EQUAL_UINT16(1, clip->cq_joint[0]);
    TEST_ASSERT_EQUAL_UINT16(0, clip->cs_joint[0]);
    ASSERT_BITS_EQUAL(k_const_q1, clip->cq, 4);
    ASSERT_BITS_EQUAL(k_const_s0, clip->cs, 3);

    /* Frame blocks are the transpose of the wire planes: t row, q row, s row. */
    for (uint32_t i = 0; i < CLIP_SAMPLES; ++i) {
        const float *block = clip->blocks + ((size_t)i * clip->block_floats);
        ASSERT_BITS_EQUAL(&k_t_samples[(size_t)i * 3U], block, 3);
        ASSERT_BITS_EQUAL(&k_q_samples[(size_t)i * 4U], block + 3, 4);
        ASSERT_BITS_EQUAL(&k_s_samples[(size_t)i * 3U], block + 7, 3);
    }

    TEST_ASSERT_EQUAL_UINT32(1, clip->n_steps);
    TEST_ASSERT_EQUAL_UINT16(2, clip->steps[0].joint);
    TEST_ASSERT_EQUAL_UINT8(0, clip->steps[0].channel);
    TEST_ASSERT_EQUAL_UINT32(0, clip->steps[0].first);
    TEST_ASSERT_EQUAL_UINT32(3, clip->steps[0].count);
    for (uint32_t k = 0; k < 3; ++k) {
        ASSERT_BITS_EQUAL(&k_step_times[k], &clip->step_times[k], 1);
        ASSERT_BITS_EQUAL(&k_step_values[(size_t)k * 4U], &clip->step_values[(size_t)k * 4U], 4);
    }

    /* The object curve keeps the clip's grid but its own modes and ranges. */
    TEST_ASSERT_EQUAL_UINT8(NT_ANM_CHANNEL_SAMPLED, clip->object.mode[0]);
    TEST_ASSERT_EQUAL_UINT8(NT_ANM_CHANNEL_STEP, clip->object.mode[1]);
    TEST_ASSERT_EQUAL_UINT8(NT_ANM_CHANNEL_ABSENT, clip->object.mode[2]);
    TEST_ASSERT_EQUAL_UINT32(CLIP_SAMPLES, clip->object.sample_count);
    TEST_ASSERT_TRUE(clip->object.inv_step == 4.0);
    TEST_ASSERT_EQUAL_UINT32(3, clip->object.step_first[1]);
    TEST_ASSERT_EQUAL_UINT32(2, clip->object.step_count[1]);
    TEST_ASSERT_EQUAL_UINT32(0, clip->object.step_count[0]);
    for (uint32_t i = 0; i < CLIP_SAMPLES; ++i) {
        ASSERT_BITS_EQUAL(&k_object_t[(size_t)i * 3U], clip->object.sampled[i].t, 3);
    }
    for (uint32_t k = 0; k < 2; ++k) {
        ASSERT_BITS_EQUAL(&k_object_step_times[k], &clip->step_times[3 + k], 1);
        ASSERT_BITS_EQUAL(&k_object_step_values[(size_t)k * 4U], &clip->step_values[((size_t)3U + k) * 4U], 4);
    }

    nt_skeletal_assets_deactivate_clip(handle);
    free(payload);
}
// #endregion

// #region sampling
static void ref_lerp3(const float *a, const float *b, float u, float *out) {
    for (int c = 0; c < 3; ++c) {
        out[c] = (a[c] * (1.0F - u)) + (b[c] * u);
    }
}

static void ref_nlerp(const float *a, const float *b, float u, float *out) {
    const float d = (a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]) + (a[3] * b[3]);
    const float sign = (d < 0.0F) ? -1.0F : 1.0F;
    float len2 = 0.0F;
    for (int c = 0; c < 4; ++c) {
        out[c] = (a[c] * (1.0F - u)) + (b[c] * sign * u);
        len2 += out[c] * out[c];
    }
    const float inv = 1.0F / sqrtf(len2);
    for (int c = 0; c < 4; ++c) {
        out[c] *= inv;
    }
}

/* Every grid time reproduces the source samples bit for bit, absent channels
 * take the defaults and the step track holds its last key. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_clip_samples_grid_times_exactly(void) {
    uint32_t size = 0;
    uint8_t *payload = encode_fixture_clip(&size);
    const uint32_t handle = nt_skeletal_assets_activate_clip(payload, size);
    TEST_ASSERT_EQUAL_UINT32(1, handle);
    const nt_skeletal_clip_t *clip = nt_skeletal_assets_clip(publish_handle("clips/b.nanm", NT_ASSET_CLIP, handle));

    nt_skeletal_trs_t defaults[CLIP_JOINTS];
    nt_skeletal_trs_t pose[CLIP_JOINTS];
    build_defaults(defaults, CLIP_JOINTS);

    for (uint32_t i = 0; i < CLIP_SAMPLES; ++i) {
        const double time = (double)i * 0.25;
        nt_skeletal_sample(clip, time, defaults, pose);

        ASSERT_BITS_EQUAL(defaults[0].t, pose[0].t, 3); /* absent */
        ASSERT_BITS_EQUAL(&k_q_samples[(size_t)i * 4U], pose[0].q, 4);
        ASSERT_BITS_EQUAL(k_const_s0, pose[0].s, 3);

        ASSERT_BITS_EQUAL(&k_t_samples[(size_t)i * 3U], pose[1].t, 3);
        ASSERT_BITS_EQUAL(k_const_q1, pose[1].q, 4);
        ASSERT_BITS_EQUAL(defaults[1].s, pose[1].s, 3);

        /* keys at 0, 0.4 and 0.8 over a grid of 0.25. */
        uint32_t key = 0U;
        if (i >= 4U) {
            key = 2U;
        } else if (i >= 2U) {
            key = 1U;
        }
        ASSERT_BITS_EQUAL(&k_step_values[(size_t)key * 4U], pose[2].t, 3);
        ASSERT_BITS_EQUAL(defaults[2].q, pose[2].q, 4);
        ASSERT_BITS_EQUAL(&k_s_samples[(size_t)i * 3U], pose[2].s, 3);
    }

    nt_skeletal_assets_deactivate_clip(handle);
    free(payload);
}

/* Between grid points the decoded blocks interpolate; the step track is exact
 * at its own timestamp and still holds the previous key just before it. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_clip_interpolates_between_samples(void) {
    uint32_t size = 0;
    uint8_t *payload = encode_fixture_clip(&size);
    const uint32_t handle = nt_skeletal_assets_activate_clip(payload, size);
    const nt_skeletal_clip_t *clip = nt_skeletal_assets_clip(publish_handle("clips/c.nanm", NT_ASSET_CLIP, handle));

    nt_skeletal_trs_t defaults[CLIP_JOINTS];
    nt_skeletal_trs_t pose[CLIP_JOINTS];
    build_defaults(defaults, CLIP_JOINTS);

    /* 0.375 = block 1 + half an interval. */
    nt_skeletal_sample(clip, 0.375, defaults, pose);

    float expect_t[3];
    float expect_q[4];
    float expect_s[3];
    ref_lerp3(&k_t_samples[3], &k_t_samples[6], 0.5F, expect_t);
    ref_nlerp(&k_q_samples[4], &k_q_samples[8], 0.5F, expect_q);
    ref_lerp3(&k_s_samples[3], &k_s_samples[6], 0.5F, expect_s);
    for (int c = 0; c < 3; ++c) {
        ASSERT_FLOAT_NEAR(expect_t[c], pose[1].t[c], 1e-6F);
        ASSERT_FLOAT_NEAR(expect_s[c], pose[2].s[c], 1e-6F);
    }
    for (int c = 0; c < 4; ++c) {
        ASSERT_FLOAT_NEAR(expect_q[c], pose[0].q[c], 1e-6F);
    }

    /* STEP holds the previous key up to but excluding its own timestamp. */
    nt_skeletal_sample(clip, (double)k_step_times[1] - 1e-6, defaults, pose);
    ASSERT_BITS_EQUAL(&k_step_values[0], pose[2].t, 3);
    nt_skeletal_sample(clip, (double)k_step_times[1], defaults, pose);
    ASSERT_BITS_EQUAL(&k_step_values[4], pose[2].t, 3);

    nt_skeletal_assets_deactivate_clip(handle);
    free(payload);
}

void test_object_curve_samples(void) {
    uint32_t size = 0;
    uint8_t *payload = encode_fixture_clip(&size);
    const uint32_t handle = nt_skeletal_assets_activate_clip(payload, size);
    const nt_skeletal_clip_t *clip = nt_skeletal_assets_clip(publish_handle("clips/d.nanm", NT_ASSET_CLIP, handle));

    nt_skeletal_trs_t defaults[1];
    nt_skeletal_trs_t out;
    build_defaults(defaults, 1);

    for (uint32_t i = 0; i < CLIP_SAMPLES; ++i) {
        nt_skeletal_sample_object(&clip->object, (double)i * 0.25, defaults, &out);
        ASSERT_BITS_EQUAL(&k_object_t[(size_t)i * 3U], out.t, 3);
        const uint32_t key = (i < 2) ? 0U : 1U; /* the object key sits at 0.5 */
        ASSERT_BITS_EQUAL(&k_object_step_values[(size_t)key * 4U], out.q, 4);
        ASSERT_BITS_EQUAL(defaults[0].s, out.s, 3); /* absent */
    }

    /* Mid-grid the sampled translation interpolates. */
    nt_skeletal_sample_object(&clip->object, 0.375, defaults, &out);
    float expect_t[3];
    ref_lerp3(&k_object_t[3], &k_object_t[6], 0.5F, expect_t);
    for (int c = 0; c < 3; ++c) {
        ASSERT_FLOAT_NEAR(expect_t[c], out.t[c], 1e-6F);
    }

    nt_skeletal_assets_deactivate_clip(handle);
    free(payload);
}
// #endregion

// #region rejections
/* Every rejection runs against a pool of one clip: after N refusals a valid
 * payload must still take slot 1, which proves nothing was published or held. */
#define EXPECT_CLIP_REJECTED(buffer, bytes, why) TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_skeletal_assets_activate_clip(buffer, bytes), why)

void test_skeleton_rejections(void) {
    nt_skeletal_skeleton_t source = fixture_skeleton();
    uint8_t *valid = NULL;
    uint32_t size = 0;
    nt_builder_encode_skeleton(&source, &valid, &size);
    uint8_t *buf = (uint8_t *)malloc(size);
    TEST_ASSERT_NOT_NULL(buf);

    memcpy(buf, valid, size);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_skeletal_assets_activate_skeleton(buf, size - 1U), "truncated payload");

    memcpy(buf, valid, size);
    wr_u32(buf, 0xDEADBEEFU);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_skeletal_assets_activate_skeleton(buf, size), "bad magic");

    memcpy(buf, valid, size);
    wr_u16(buf + 4, 0x0200U);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_skeletal_assets_activate_skeleton(buf, size), "major version 2");

    /* parent[2] = 3 points forward, so the joints are not in preorder. */
    memcpy(buf, valid, size);
    wr_u16(buf + 16 + 4, 3U);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_skeletal_assets_activate_skeleton(buf, size), "non-preorder parent");

    /* subtree_end[2] = 4 leaves the range of parent 1, which ends at 3. */
    memcpy(buf, valid, size);
    wr_u16(buf + 16 + (size_t)(2 * SKEL_JOINTS) + 4, 4U);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_skeletal_assets_activate_skeleton(buf, size), "child outside its parent subtree");

    /* The last root must close at joint_count. */
    memcpy(buf, valid, size);
    wr_u16(buf + 16 + (size_t)(2 * SKEL_JOINTS) + 6, 3U);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_skeletal_assets_activate_skeleton(buf, size), "last root does not end at joint_count");

    /* Root 0 must close where root 3 starts. */
    memcpy(buf, valid, size);
    wr_u16(buf + 16 + (size_t)(2 * SKEL_JOINTS), 2U);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_skeletal_assets_activate_skeleton(buf, size), "root does not end at the next root");

    const uint32_t rest0 = 16U + (8U * SKEL_JOINTS);
    memcpy(buf, valid, size);
    wr_f32(buf + rest0 + 12U, 0.5F); /* q.x = 0.5 with w = 1 is not unit */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_skeletal_assets_activate_skeleton(buf, size), "non-unit rest rotation");

    memcpy(buf, valid, size);
    wr_u32(buf + rest0, 0x7F800000U); /* +inf translation */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_skeletal_assets_activate_skeleton(buf, size), "non-finite rest translation");

    memcpy(buf, valid, size);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, nt_skeletal_assets_activate_skeleton(buf, size), "a valid skeleton still takes slot 1");
    nt_skeletal_assets_deactivate_skeleton(1);

    free(buf);
    free(valid);
}

void test_skin_binding_rejections(void) {
    nt_skin_binding_t source = fixture_binding();
    uint8_t *valid = NULL;
    uint32_t size = 0;
    nt_builder_encode_skin_binding(&source, 1.75F, 4.5F, &valid, &size);
    uint8_t *buf = (uint8_t *)malloc(size);
    TEST_ASSERT_NOT_NULL(buf);

    memcpy(buf, valid, size);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_skeletal_assets_activate_skin_binding(buf, size - 1U), "truncated payload");

    memcpy(buf, valid, size);
    buf[16] = 1U;
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_skeletal_assets_activate_skin_binding(buf, size), "mesh_space 1");

    memcpy(buf, valid, size);
    wr_f32(buf + 20, -0.5F);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_skeletal_assets_activate_skin_binding(buf, size), "negative reach");

    memcpy(buf, valid, size);
    wr_u32(buf + 28 + (size_t)(2 * SKIN_PALETTE), 0x7FC00000U); /* NaN in the first matrix */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_skeletal_assets_activate_skin_binding(buf, size), "non-finite inverse bind");

    memcpy(buf, valid, size);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, nt_skeletal_assets_activate_skin_binding(buf, size), "a valid binding still takes slot 1");
    nt_skeletal_assets_deactivate_skin_binding(1);

    free(buf);
    free(valid);
}

/* Header and section-table rules. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_clip_header_and_section_rejections(void) {
    reinit_assets(1);

    uint32_t size = 0;
    uint8_t *valid = encode_fixture_clip(&size);
    uint8_t *buf = (uint8_t *)malloc(size);
    TEST_ASSERT_NOT_NULL(buf);

    memcpy(buf, valid, size);
    EXPECT_CLIP_REJECTED(buf, size - 1U, "truncated payload");

    memcpy(buf, valid, size);
    wr_u32(buf, 0xDEADBEEFU);
    EXPECT_CLIP_REJECTED(buf, size, "bad magic");

    memcpy(buf, valid, size);
    wr_u16(buf + 4, 0x0200U);
    EXPECT_CLIP_REJECTED(buf, size, "major version 2");

    memcpy(buf, valid, size);
    buf[11] = 1U;
    EXPECT_CLIP_REJECTED(buf, size, "codec 1");

    memcpy(buf, valid, size);
    buf[10] = NT_ANM_KIND_ADDITIVE;
    EXPECT_CLIP_REJECTED(buf, size, "additive clip without a reference id");

    memcpy(buf, valid, size);
    wr_f32(buf + 36, -1.0F);
    EXPECT_CLIP_REJECTED(buf, size, "negative bounds radius");

    /* 0x08000000 * 0x20 is exactly 2^32: the bound must be computed in 64 bits,
     * because the truncated product is 0 and would pass. */
    memcpy(buf, valid, size);
    wr_u32(section_desc(buf, 6) + 8, 0x08000000U);
    wr_u32(section_desc(buf, 6) + 12, 0x00000020U);
    EXPECT_CLIP_REJECTED(buf, size, "section count * stride wraps in 32 bits");

    memcpy(buf, valid, size);
    wr_u32(section_desc(buf, 4) + 4, section_offset(valid, 4) + 2U);
    EXPECT_CLIP_REJECTED(buf, size, "misaligned section offset");

    memcpy(buf, valid, size);
    wr_u32(section_desc(buf, 3), NT_SKELETAL_FOURCC('Z', 'Z', 'Z', 'Z'));
    EXPECT_CLIP_REJECTED(buf, size, "missing known section tag");

    memcpy(buf, valid, size);
    wr_u32(section_desc(buf, 2), NT_ANM_TAG_PLNT);
    EXPECT_CLIP_REJECTED(buf, size, "duplicate section tag");

    memcpy(buf, valid, size);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, nt_skeletal_assets_activate_clip(buf, size), "a valid clip still takes slot 1");
    nt_skeletal_assets_deactivate_clip(1);

    free(buf);
    free(valid);
}

/* Channel, constant, plane and step rules. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_clip_table_rejections(void) {
    reinit_assets(1);

    uint32_t size = 0;
    uint8_t *valid = encode_fixture_clip(&size);
    uint8_t *buf = (uint8_t *)malloc(size);
    TEST_ASSERT_NOT_NULL(buf);

    const uint32_t chan = section_offset(valid, 0);
    const uint32_t cnst = section_offset(valid, 4);
    const uint32_t stpk = section_offset(valid, 6);

    /* Channel 9 is the second sampled translation, so its row index is 1. */
    memcpy(buf, valid, size);
    wr_u16(buf + chan + (size_t)(4 * 9) + 2, 0U);
    EXPECT_CLIP_REJECTED(buf, size, "sampled row index out of order");

    memcpy(buf, valid, size);
    wr_u16(buf + chan + (size_t)(4 * 2) + 2, 2U);
    EXPECT_CLIP_REJECTED(buf, size, "constant index past the table");

    memcpy(buf, valid, size);
    buf[chan + (4 * 0)] = 7U;
    EXPECT_CLIP_REJECTED(buf, size, "unknown channel mode");

    memcpy(buf, valid, size);
    wr_u16(buf + chan + (size_t)(4 * 0) + 2, 3U);
    EXPECT_CLIP_REJECTED(buf, size, "absent channel with an index");

    /* The constant rotation of joint 1 is CNST entry 1. */
    memcpy(buf, valid, size);
    wr_f32(buf + cnst + 16 + 0, 0.5F);
    EXPECT_CLIP_REJECTED(buf, size, "non-unit constant rotation");

    memcpy(buf, valid, size);
    wr_f32(buf + cnst + 12, 1.0F); /* entry 0 is a scale: the fourth float must stay 0 */
    EXPECT_CLIP_REJECTED(buf, size, "constant scale with a fourth component");

    memcpy(buf, valid, size);
    wr_f32(buf + stpk, 0.1F);
    EXPECT_CLIP_REJECTED(buf, size, "first step key not at 0");

    memcpy(buf, valid, size);
    wr_f32(buf + stpk + 20, 0.0F);
    EXPECT_CLIP_REJECTED(buf, size, "step times not strictly increasing");

    memcpy(buf, valid, size);
    wr_f32(buf + stpk + 40, 1.5F);
    EXPECT_CLIP_REJECTED(buf, size, "last step key past the duration");

    memcpy(buf, valid, size);
    wr_u32(buf + stpk + 4, 0x7F800000U);
    EXPECT_CLIP_REJECTED(buf, size, "non-finite step value");

    /* A plane row that is not a unit quaternion. */
    memcpy(buf, valid, size);
    wr_f32(buf + section_offset(valid, 2), 0.5F);
    EXPECT_CLIP_REJECTED(buf, size, "non-unit sampled rotation");

    memcpy(buf, valid, size);
    wr_u32(buf + section_offset(valid, 1), 0x7FC00000U);
    EXPECT_CLIP_REJECTED(buf, size, "non-finite sampled translation");

    /* sample_count 1 keeps the planes empty; the strides come along so the
     * sample-count rule is what rejects, not the stride check. */
    memcpy(buf, valid, size);
    wr_u32(buf + 32, 1U);
    wr_u32(section_desc(buf, 1) + 12, 12U);
    wr_u32(section_desc(buf, 2) + 12, 16U);
    wr_u32(section_desc(buf, 3) + 12, 12U);
    EXPECT_CLIP_REJECTED(buf, size, "plane rows with a single sample");

    memcpy(buf, valid, size);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, nt_skeletal_assets_activate_clip(buf, size), "a valid clip still takes slot 1");
    nt_skeletal_assets_deactivate_clip(1);

    free(buf);
    free(valid);
}

/* The minor-version rule: an unknown section tag is skipped, not rejected. */
void test_clip_skips_unknown_section(void) {
    uint32_t base_size = 0;
    uint8_t *base = encode_fixture_clip(&base_size);

    const uint32_t table_end = (uint32_t)sizeof(NtAnmHeader) + ((uint32_t)NT_ANM_SECTION_COUNT * (uint32_t)sizeof(NtAnmSection));
    const uint32_t size = base_size + (uint32_t)sizeof(NtAnmSection);
    uint8_t *buf = (uint8_t *)malloc(size);
    TEST_ASSERT_NOT_NULL(buf);

    memcpy(buf, base, table_end);
    wr_u16(buf + 6, (uint16_t)(NT_ANM_SECTION_COUNT + 1));
    for (uint32_t s = 0; s < NT_ANM_SECTION_COUNT; ++s) {
        wr_u32(section_desc(buf, s) + 4, section_offset(base, s) + (uint32_t)sizeof(NtAnmSection));
    }
    uint8_t *unknown = section_desc(buf, NT_ANM_SECTION_COUNT);
    wr_u32(unknown, NT_SKELETAL_FOURCC('Z', 'Z', 'Z', 'Z'));
    wr_u32(unknown + 4, table_end + (uint32_t)sizeof(NtAnmSection));
    wr_u32(unknown + 8, 0U);
    wr_u32(unknown + 12, 0U);
    memcpy(buf + table_end + sizeof(NtAnmSection), base + table_end, base_size - table_end);

    const uint32_t handle = nt_skeletal_assets_activate_clip(buf, size);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, handle, "an unknown section tag must be skipped");
    const nt_skeletal_clip_t *clip = nt_skeletal_assets_clip(publish_handle("clips/e.nanm", NT_ASSET_CLIP, handle));
    TEST_ASSERT_EQUAL_UINT16(1, clip->n_t);
    TEST_ASSERT_EQUAL_UINT32(CLIP_SAMPLES, clip->sample_count);
    nt_skeletal_assets_deactivate_clip(handle);

    free(buf);
    free(base);
}

/* Capacity is a game decision, so exhausting a pool is a programming error. */
void test_clip_pool_overflow_asserts(void) {
    reinit_assets(1);

    uint32_t size = 0;
    uint8_t *payload = encode_fixture_clip(&size);
    TEST_ASSERT_EQUAL_UINT32(1, nt_skeletal_assets_activate_clip(payload, size));
    NT_TEST_EXPECT_ASSERT(nt_skeletal_assets_activate_clip(payload, size));

    nt_skeletal_assets_deactivate_clip(1);
    free(payload);
}
// #endregion

// #region two packs through the resource system
static uint8_t *read_file_bytes(const char *path, uint32_t *out_size) {
    FILE *f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL(f);
    (void)fseek(f, 0, SEEK_END);
    const long len = ftell(f);
    (void)fseek(f, 0, SEEK_SET);
    TEST_ASSERT_TRUE(len > 0);
    uint8_t *buf = (uint8_t *)malloc((size_t)len);
    TEST_ASSERT_NOT_NULL(buf);
    const size_t got = fread(buf, 1, (size_t)len, f);
    (void)fclose(f);
    TEST_ASSERT_EQUAL_size_t((size_t)len, got);
    *out_size = (uint32_t)len;
    return buf;
}

/* Pack A carries the rig and one clip, pack B a second clip of the same rig. */
static void build_pack_a(const char *path) {
    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL(ctx);
    nt_skeletal_skeleton_t skel = fixture_skeleton();
    nt_builder_clip_t clip;
    nt_builder_anim_channel_t channels[CLIP_CHANNELS];
    fixture_clip(&clip, channels);
    nt_builder_add_skeleton(ctx, &skel, "rigs/hero.nskl");
    nt_builder_add_clip(ctx, &clip, "clips/run.nanm");
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    nt_builder_free_pack(ctx);
}

static void build_pack_b(const char *path) {
    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL(ctx);
    nt_builder_clip_t clip;
    nt_builder_anim_channel_t channels[CLIP_CHANNELS];
    fixture_clip(&clip, channels);
    clip.r_root = 9.0F; /* tells the two clip views apart */
    nt_builder_add_clip(ctx, &clip, "clips/walk.nanm");
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    nt_builder_free_pack(ctx);
}

/* Blanks the asset payloads of a pack, leaving the header and entry table the
 * resource registry still borrows. A copy-out activator must not notice. */
static void wipe_asset_payloads(uint8_t *pack) {
    const NtPackHeader *header = (const NtPackHeader *)pack;
    const NtAssetEntry *entries = (const NtAssetEntry *)(pack + sizeof(NtPackHeader));
    for (uint16_t i = 0; i < header->asset_count; ++i) {
        memset(pack + entries[i].offset, 0xCD, entries[i].size);
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_two_packs_share_one_skeleton(void) {
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);
    const char *path_a = TMP_DIR "/skeletal_assets_a.ntpack";
    const char *path_b = TMP_DIR "/skeletal_assets_b.ntpack";
    build_pack_a(path_a);
    build_pack_b(path_b);

    uint32_t size_a = 0;
    uint32_t size_b = 0;
    uint8_t *pack_a = read_file_bytes(path_a, &size_a);
    uint8_t *pack_b = read_file_bytes(path_b, &size_b);

    /* Exactly how an application wires the three pairs up. */
    nt_resource_register_type(NT_ASSET_SKELETON, &(nt_resource_type_desc_t){.activate = nt_skeletal_assets_activate_skeleton, .deactivate = nt_skeletal_assets_deactivate_skeleton});
    nt_resource_register_type(NT_ASSET_SKIN_BINDING, &(nt_resource_type_desc_t){.activate = nt_skeletal_assets_activate_skin_binding, .deactivate = nt_skeletal_assets_deactivate_skin_binding});
    nt_resource_register_type(NT_ASSET_CLIP, &(nt_resource_type_desc_t){.activate = nt_skeletal_assets_activate_clip, .deactivate = nt_skeletal_assets_deactivate_clip});

    const nt_hash32_t pid_a = nt_hash32_str("skeletal_pack_a");
    const nt_hash32_t pid_b = nt_hash32_str("skeletal_pack_b");
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid_a, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid_a, pack_a, size_a));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_mount(pid_b, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_parse_pack(pid_b, pack_b, size_b));

    const nt_resource_t rig = nt_resource_request(nt_builder_normalize_and_hash("rigs/hero.nskl"), NT_ASSET_SKELETON);
    const nt_resource_t run = nt_resource_request(nt_builder_normalize_and_hash("clips/run.nanm"), NT_ASSET_CLIP);
    const nt_resource_t walk = nt_resource_request(nt_builder_normalize_and_hash("clips/walk.nanm"), NT_ASSET_CLIP);
    nt_resource_step();

    TEST_ASSERT_TRUE_MESSAGE(nt_resource_is_ready(rig), "skeleton ready");
    TEST_ASSERT_TRUE_MESSAGE(nt_resource_is_ready(run), "clip from pack A ready");
    TEST_ASSERT_TRUE_MESSAGE(nt_resource_is_ready(walk), "clip from pack B ready");

    const nt_skeletal_skeleton_t *skeleton = nt_skeletal_assets_skeleton(rig);
    const nt_skeletal_clip_t *clip_run = nt_skeletal_assets_clip(run);
    const nt_skeletal_clip_t *clip_walk = nt_skeletal_assets_clip(walk);
    /* The pairing check the game makes once, per §15. */
    TEST_ASSERT_EQUAL_HEX64(skeleton->rig_compat_id.value, clip_run->rig_compat_id.value);
    TEST_ASSERT_EQUAL_HEX64(skeleton->rig_compat_id.value, clip_walk->rig_compat_id.value);
    TEST_ASSERT_TRUE(clip_run->r_root != clip_walk->r_root);

    nt_skeletal_trs_t defaults[CLIP_JOINTS];
    nt_skeletal_trs_t before[CLIP_JOINTS];
    nt_skeletal_trs_t after[CLIP_JOINTS];
    build_defaults(defaults, CLIP_JOINTS);
    nt_skeletal_sample(clip_run, 0.375, defaults, before);

    /* Copy-out: the blob may be dropped or overwritten after activation. */
    wipe_asset_payloads(pack_a);
    nt_skeletal_sample(clip_run, 0.375, defaults, after);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(before, after, sizeof(before), "the clip view must not read the pack blob");

    /* Unmounting one clip pack leaves the other playable. */
    nt_resource_unmount(pid_b);
    nt_resource_step();
    TEST_ASSERT_FALSE_MESSAGE(nt_resource_is_ready(walk), "the unmounted clip is gone");
    TEST_ASSERT_TRUE_MESSAGE(nt_resource_is_ready(run), "the other pack's clip survives");
    TEST_ASSERT_TRUE_MESSAGE(nt_resource_is_ready(rig), "the skeleton survives");

    const nt_skeletal_clip_t *clip_run_again = nt_skeletal_assets_clip(run);
    nt_skeletal_sample(clip_run_again, 0.375, defaults, after);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(before, after, sizeof(before), "the surviving clip samples identically");
    TEST_ASSERT_EQUAL_UINT16(SKEL_JOINTS, nt_skeletal_assets_skeleton(rig)->joint_count);

    nt_resource_unmount(pid_a);
    nt_resource_step();
    TEST_ASSERT_FALSE(nt_resource_is_ready(run));
    TEST_ASSERT_FALSE(nt_resource_is_ready(rig));

    free(pack_a);
    free(pack_b);
}
// #endregion

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_skeleton_round_trip);
    RUN_TEST(test_skin_binding_round_trip);
    RUN_TEST(test_clip_round_trip_tables);
    RUN_TEST(test_clip_samples_grid_times_exactly);
    RUN_TEST(test_clip_interpolates_between_samples);
    RUN_TEST(test_object_curve_samples);
    RUN_TEST(test_skeleton_rejections);
    RUN_TEST(test_skin_binding_rejections);
    RUN_TEST(test_clip_header_and_section_rejections);
    RUN_TEST(test_clip_table_rejections);
    RUN_TEST(test_clip_skips_unknown_section);
    RUN_TEST(test_clip_pool_overflow_asserts);
    RUN_TEST(test_two_packs_share_one_skeleton);
    return UNITY_END();
}
