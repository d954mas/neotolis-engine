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

static nt_skeletal_skeleton_t fixture_skeleton(void) {
    return (nt_skeletal_skeleton_t){
        .parent = k_parent,
        .subtree_end = k_subtree_end,
        .joint_id = k_joint_id,
        .rest = k_rest,
        .joint_count = SKEL_JOINTS,
    };
}

/* The encoder computes the rig identity, so the fixture asks the one
 * implementation of the schema instead of inventing a number. */
static uint64_t fixture_rig_id(void) {
    nt_skeletal_skeleton_t skel = fixture_skeleton();
    uint8_t scratch[NT_SKELETAL_RIG_ID_BYTES(SKEL_JOINTS)];
    return nt_skeletal_rig_compat_id(&skel, scratch, (uint32_t)sizeof(scratch)).value;
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
        .rig_compat_id = (nt_hash64_t){.value = fixture_rig_id()},
        .remap = k_remap,
        .inverse_bind = k_inverse_bind,
        .palette_count = SKIN_PALETTE,
    };
}

/*
 * Asymmetric clip: a different mode per joint and component, two sampled rows of
 * the same kind and two constant translations, so a row index or a per-kind
 * table offset that is ignored cannot pass.
 *
 *   joint 0: t constant, q sampled (q row 0), s constant
 *   joint 1: t sampled (t row 0), q constant, s absent
 *   joint 2: t sampled (t row 1), q sampled (q row 1), s sampled (s row 0)
 *   joint 3: t constant, q absent, s step
 *   object : t sampled, q step, s absent
 *
 * duration 1 over 5 samples, so every grid time is binary exact. The rotation
 * rows turn about one axis in equal steps: the nlerp midpoint of two of them is
 * the rotation at their mean angle, which the test writes down as a literal
 * instead of reimplementing the kernel.
 */
#define CLIP_JOINTS 4
#define CLIP_SAMPLES 5
#define CLIP_CHANNELS (3 * (CLIP_JOINTS + 1))
#define CLIP_BLOCK_FLOATS 17

/* joint 0: 0, 22.5, 45, 67.5 and 90 degrees about z. */
static const float k_q_row0[CLIP_SAMPLES * 4] = {
    0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.19509032F, 0.98078528F, 0.0F, 0.0F, 0.38268343F, 0.92387953F, 0.0F, 0.0F, 0.55557023F, 0.83146961F, 0.0F, 0.0F, 0.70710678F, 0.70710678F,
};
/* joint 2: the same angles about x, counting down. */
static const float k_q_row1[CLIP_SAMPLES * 4] = {
    0.70710678F, 0.0F, 0.0F, 0.70710678F, 0.55557023F, 0.0F, 0.0F, 0.83146961F, 0.38268343F, 0.0F, 0.0F, 0.92387953F, 0.19509032F, 0.0F, 0.0F, 0.98078528F, 0.0F, 0.0F, 0.0F, 1.0F,
};
static const float k_t_row0[CLIP_SAMPLES * 3] = {0.0F, 0.0F, 0.0F, 1.0F, 0.5F, -0.25F, 2.0F, 1.0F, -0.5F, 3.0F, 1.5F, -0.75F, 4.0F, 2.0F, -1.0F};
static const float k_t_row1[CLIP_SAMPLES * 3] = {10.0F, -1.0F, 0.5F, 10.5F, -2.0F, 1.5F, 11.0F, -3.0F, 2.5F, 11.5F, -4.0F, 3.5F, 12.0F, -5.0F, 4.5F};
static const float k_s_row0[CLIP_SAMPLES * 3] = {1.0F, 1.0F, 1.0F, 1.1F, 1.0F, 0.9F, 1.2F, 1.0F, 0.8F, 1.3F, 1.0F, 0.7F, 1.4F, 1.0F, 0.6F};
static const float k_object_t[CLIP_SAMPLES * 3] = {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.25F, 0.0F, 0.0F, 0.5F, 0.0F, 0.0F, 0.75F, 0.0F, 0.0F, 1.0F};

/* nlerp midpoints of samples 1 and 2: 33.75 degrees about z, 56.25 about x. */
static const float k_q_row0_mid[4] = {0.0F, 0.0F, 0.29028483F, 0.95694034F};
static const float k_q_row1_mid[4] = {0.47139674F, 0.0F, 0.0F, 0.88192126F};

static const float k_const_t0[4] = {1.5F, -2.25F, 0.75F, 0.0F};
static const float k_const_s0[4] = {2.0F, 3.0F, 4.0F, 0.0F};
static const float k_const_q1[4] = {0.0F, 0.0F, 0.70710678F, 0.70710678F};
static const float k_const_t3[4] = {-5.5F, 6.25F, -7.125F, 0.0F};

/* joint 3 scale: three keys inside the duration. */
static const float k_step_times[3] = {0.0F, 0.4F, 0.8F};
static const float k_step_values[3 * 4] = {1.0F, 1.0F, 1.0F, 0.0F, 2.0F, 0.5F, 3.0F, 0.0F, 0.25F, 4.0F, 0.5F, 0.0F};

/* object rotation: an eighth turn about x, then a quarter turn about z. */
static const float k_object_step_times[2] = {0.0F, 0.5F};
static const float k_object_step_values[2 * 4] = {0.38268343F, 0.0F, 0.0F, 0.92387953F, 0.0F, 0.0F, 0.70710678F, 0.70710678F};

static void fixture_clip(nt_builder_clip_t *clip, nt_builder_anim_channel_t channels[CLIP_CHANNELS]) {
    memset(channels, 0, sizeof(nt_builder_anim_channel_t) * (size_t)CLIP_CHANNELS);

    channels[0].mode = NT_SKELETAL_CHANNEL_CONSTANT;
    memcpy(channels[0].constant, k_const_t0, sizeof(k_const_t0));
    channels[1].mode = NT_SKELETAL_CHANNEL_SAMPLED;
    channels[1].samples = k_q_row0;
    channels[2].mode = NT_SKELETAL_CHANNEL_CONSTANT;
    memcpy(channels[2].constant, k_const_s0, sizeof(k_const_s0));

    channels[3].mode = NT_SKELETAL_CHANNEL_SAMPLED;
    channels[3].samples = k_t_row0;
    channels[4].mode = NT_SKELETAL_CHANNEL_CONSTANT;
    memcpy(channels[4].constant, k_const_q1, sizeof(k_const_q1));

    channels[6].mode = NT_SKELETAL_CHANNEL_SAMPLED;
    channels[6].samples = k_t_row1;
    channels[7].mode = NT_SKELETAL_CHANNEL_SAMPLED;
    channels[7].samples = k_q_row1;
    channels[8].mode = NT_SKELETAL_CHANNEL_SAMPLED;
    channels[8].samples = k_s_row0;

    channels[9].mode = NT_SKELETAL_CHANNEL_CONSTANT;
    memcpy(channels[9].constant, k_const_t3, sizeof(k_const_t3));
    channels[11].mode = NT_SKELETAL_CHANNEL_STEP;
    channels[11].step_times = k_step_times;
    channels[11].step_values = k_step_values;
    channels[11].step_count = 3;

    channels[12].mode = NT_SKELETAL_CHANNEL_SAMPLED;
    channels[12].samples = k_object_t;
    channels[13].mode = NT_SKELETAL_CHANNEL_STEP;
    channels[13].step_times = k_object_step_times;
    channels[13].step_values = k_object_step_values;
    channels[13].step_count = 2;

    memset(clip, 0, sizeof(*clip));
    clip->rig_compat_id = fixture_rig_id();
    clip->joint_count = CLIP_JOINTS;
    clip->sample_count = CLIP_SAMPLES;
    clip->duration = 1.0F;
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

/* Offsets of the fixture clip's payload, in the order §16 lists them; the
 * rejection tests patch bytes through these. */
enum {
    ANM_OFF_BLOCKS = 120,
    ANM_OFF_CT = ANM_OFF_BLOCKS + (CLIP_SAMPLES * CLIP_BLOCK_FLOATS * 4),
    ANM_OFF_CQ = ANM_OFF_CT + 24,
    ANM_OFF_CS = ANM_OFF_CQ + 16,
    ANM_OFF_STEPS = ANM_OFF_CS + 12,
    ANM_OFF_KEYS = ANM_OFF_STEPS + 12,
    ANM_OFF_OBJECT = ANM_OFF_KEYS + (5 * 20),
    ANM_OFF_T_JOINT = ANM_OFF_OBJECT + (CLIP_SAMPLES * 40),
    ANM_OFF_Q_JOINT = ANM_OFF_T_JOINT + 4,
    ANM_OFF_S_JOINT = ANM_OFF_Q_JOINT + 4,
    ANM_OFF_CT_JOINT = ANM_OFF_S_JOINT + 2,
    ANM_OFF_CQ_JOINT = ANM_OFF_CT_JOINT + 4,
    ANM_OFF_CS_JOINT = ANM_OFF_CQ_JOINT + 2,
    ANM_SIZE = ANM_OFF_CS_JOINT + 2,
};

/* Header field offsets, from NtAnmHeader. */
enum {
    ANM_HDR_JOINT_COUNT = 6,
    ANM_HDR_SAMPLE_COUNT = 8,
    ANM_HDR_DURATION = 12,
    ANM_HDR_OBJECT_MODE = 52,
    ANM_HDR_OBJECT_STEP_FIRST = 96,
    ANM_HDR_OBJECT_STEP_COUNT = 108,
};

/* Distinct non-identity unit rotations, so "absent channel keeps the default"
 * cannot pass on an identity the clip would have produced anyway. */
static const float k_default_q[CLIP_JOINTS][4] = {
    {0.5F, 0.5F, 0.5F, 0.5F},
    {0.70710678F, 0.0F, 0.0F, 0.70710678F},
    {0.0F, 0.70710678F, 0.0F, 0.70710678F},
    {0.0F, 0.0F, 0.6F, 0.8F},
};

static void build_defaults(nt_skeletal_trs_t *out, uint16_t count) {
    for (uint16_t j = 0; j < count; ++j) {
        out[j].t[0] = 100.0F + (float)j;
        out[j].t[1] = 200.0F + (float)j;
        out[j].t[2] = 300.0F + (float)j;
        memcpy(out[j].q, k_default_q[j], sizeof(out[j].q));
        out[j].s[0] = 1.0F + (float)j;
        out[j].s[1] = 2.0F + (float)j;
        out[j].s[2] = 3.0F + (float)j;
    }
}

/* Expected value of a T/S channel halfway between two grid samples: the kernel
 * lerps as a*(1-u) + b*u, which at u = 0.5 is the plain average. */
static float mid(const float *row, uint32_t component) { return (row[component] + row[component + 3U]) * 0.5F; }
// #endregion

// #region unity fixture
void setUp(void) {
    nt_resource_init(NULL);
    nt_skeletal_assets_init(4);
}

/* Resources first: its shutdown deactivates assets through this module. */
void tearDown(void) {
    nt_resource_shutdown();
    nt_skeletal_assets_shutdown();
}

/* Re-open the pool with a different capacity inside a test. */
static void reinit_assets(uint16_t max_assets) {
    nt_skeletal_assets_shutdown();
    nt_skeletal_assets_init(max_assets);
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
    TEST_ASSERT_NOT_EQUAL_UINT32(0, handle);

    /* Nothing reads the payload after activation, so the view must survive it. */
    memset(payload, 0xCD, size);

    const nt_skeletal_skeleton_t *view = nt_skeletal_assets_skeleton(publish_handle("rigs/hero.nskl", NT_ASSET_SKELETON, handle));
    TEST_ASSERT_EQUAL_UINT16(SKEL_JOINTS, view->joint_count);
    TEST_ASSERT_EQUAL_HEX64(fixture_rig_id(), view->rig_compat_id.value);
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
    nt_builder_encode_skin_binding(&source, &payload, &size);

    const uint32_t handle = nt_skeletal_assets_activate_skin_binding(payload, size);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, handle);
    memset(payload, 0xCD, size);

    const nt_skin_binding_t *view = nt_skeletal_assets_skin_binding(publish_handle("rigs/hero.nskn", NT_ASSET_SKIN_BINDING, handle));
    TEST_ASSERT_EQUAL_UINT16(SKIN_PALETTE, view->palette_count);
    TEST_ASSERT_EQUAL_HEX64(fixture_rig_id(), view->rig_compat_id.value);
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
    TEST_ASSERT_EQUAL_UINT32(ANM_SIZE, size);
    const uint32_t handle = nt_skeletal_assets_activate_clip(payload, size);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, handle);
    memset(payload, 0xCD, size);

    const nt_skeletal_clip_t *clip = nt_skeletal_assets_clip(publish_handle("clips/a.nanm", NT_ASSET_CLIP, handle));
    TEST_ASSERT_EQUAL_HEX64(fixture_rig_id(), clip->rig_compat_id.value);
    TEST_ASSERT_EQUAL_HEX64(0, clip->additive_ref_id.value);
    TEST_ASSERT_EQUAL_UINT16(CLIP_JOINTS, clip->joint_count);
    TEST_ASSERT_EQUAL_UINT32(CLIP_SAMPLES, clip->sample_count);
    TEST_ASSERT_TRUE(clip->duration == 1.0);
    TEST_ASSERT_TRUE(clip->inv_step == 4.0);
    TEST_ASSERT_EQUAL_UINT32(CLIP_BLOCK_FLOATS, clip->block_floats);

    TEST_ASSERT_EQUAL_UINT16(2, clip->n_t);
    TEST_ASSERT_EQUAL_UINT16(2, clip->n_q);
    TEST_ASSERT_EQUAL_UINT16(1, clip->n_s);
    TEST_ASSERT_EQUAL_UINT16(1, clip->t_joint[0]);
    TEST_ASSERT_EQUAL_UINT16(2, clip->t_joint[1]);
    TEST_ASSERT_EQUAL_UINT16(0, clip->q_joint[0]);
    TEST_ASSERT_EQUAL_UINT16(2, clip->q_joint[1]);
    TEST_ASSERT_EQUAL_UINT16(2, clip->s_joint[0]);

    TEST_ASSERT_EQUAL_UINT16(2, clip->n_ct);
    TEST_ASSERT_EQUAL_UINT16(1, clip->n_cq);
    TEST_ASSERT_EQUAL_UINT16(1, clip->n_cs);
    TEST_ASSERT_EQUAL_UINT16(0, clip->ct_joint[0]);
    TEST_ASSERT_EQUAL_UINT16(3, clip->ct_joint[1]);
    TEST_ASSERT_EQUAL_UINT16(1, clip->cq_joint[0]);
    TEST_ASSERT_EQUAL_UINT16(0, clip->cs_joint[0]);
    ASSERT_BITS_EQUAL(k_const_t0, clip->ct, 3);
    ASSERT_BITS_EQUAL(k_const_t3, clip->ct + 3, 3);
    ASSERT_BITS_EQUAL(k_const_q1, clip->cq, 4);
    ASSERT_BITS_EQUAL(k_const_s0, clip->cs, 3);

    /* Frame blocks hold both t rows, both q rows, then the s row. The object
     * curve is not a joint row and stays out. */
    for (uint32_t i = 0; i < CLIP_SAMPLES; ++i) {
        const float *block = clip->blocks + ((size_t)i * clip->block_floats);
        ASSERT_BITS_EQUAL(&k_t_row0[(size_t)i * 3U], block, 3);
        ASSERT_BITS_EQUAL(&k_t_row1[(size_t)i * 3U], block + 3, 3);
        ASSERT_BITS_EQUAL(&k_q_row0[(size_t)i * 4U], block + 6, 4);
        ASSERT_BITS_EQUAL(&k_q_row1[(size_t)i * 4U], block + 10, 4);
        ASSERT_BITS_EQUAL(&k_s_row0[(size_t)i * 3U], block + 14, 3);
    }

    TEST_ASSERT_EQUAL_UINT32(1, clip->n_steps);
    TEST_ASSERT_EQUAL_UINT16(3, clip->steps[0].joint);
    TEST_ASSERT_EQUAL_UINT8(2, clip->steps[0].channel);
    TEST_ASSERT_EQUAL_UINT32(0, clip->steps[0].first);
    TEST_ASSERT_EQUAL_UINT32(3, clip->steps[0].count);
    for (uint32_t k = 0; k < 3; ++k) {
        ASSERT_BITS_EQUAL(&k_step_times[k], &clip->keys[k].time, 1);
        ASSERT_BITS_EQUAL(&k_step_values[(size_t)k * 4U], clip->keys[k].v, 4);
    }

    /* The object curve keeps the clip's grid but its own modes and ranges. */
    TEST_ASSERT_EQUAL_UINT8(NT_SKELETAL_CHANNEL_SAMPLED, clip->object.mode[0]);
    TEST_ASSERT_EQUAL_UINT8(NT_SKELETAL_CHANNEL_STEP, clip->object.mode[1]);
    TEST_ASSERT_EQUAL_UINT8(NT_SKELETAL_CHANNEL_ABSENT, clip->object.mode[2]);
    TEST_ASSERT_EQUAL_UINT32(CLIP_SAMPLES, clip->object.sample_count);
    TEST_ASSERT_TRUE(clip->object.inv_step == 4.0);
    TEST_ASSERT_EQUAL_UINT32(3, clip->object.step_first[1]);
    TEST_ASSERT_EQUAL_UINT32(2, clip->object.step_count[1]);
    TEST_ASSERT_EQUAL_UINT32(0, clip->object.step_count[0]);
    for (uint32_t i = 0; i < CLIP_SAMPLES; ++i) {
        ASSERT_BITS_EQUAL(&k_object_t[(size_t)i * 3U], clip->object.sampled[i].t, 3);
    }
    for (uint32_t k = 0; k < 2; ++k) {
        ASSERT_BITS_EQUAL(&k_object_step_times[k], &clip->object.keys[3 + k].time, 1);
        ASSERT_BITS_EQUAL(&k_object_step_values[(size_t)k * 4U], clip->object.keys[3 + k].v, 4);
    }

    nt_skeletal_assets_deactivate_clip(handle);
    free(payload);
}
// #endregion

// #region sampling
/* Every grid time reproduces the source samples bit for bit, absent channels
 * take the defaults and the step track holds its last key. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_clip_samples_grid_times_exactly(void) {
    uint32_t size = 0;
    uint8_t *payload = encode_fixture_clip(&size);
    const uint32_t handle = nt_skeletal_assets_activate_clip(payload, size);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, handle);
    const nt_skeletal_clip_t *clip = nt_skeletal_assets_clip(publish_handle("clips/b.nanm", NT_ASSET_CLIP, handle));

    nt_skeletal_trs_t defaults[CLIP_JOINTS];
    nt_skeletal_trs_t pose[CLIP_JOINTS];
    build_defaults(defaults, CLIP_JOINTS);

    for (uint32_t i = 0; i < CLIP_SAMPLES; ++i) {
        const double time = (double)i * 0.25;
        nt_skeletal_sample(clip, time, defaults, pose);

        ASSERT_BITS_EQUAL(k_const_t0, pose[0].t, 3);
        ASSERT_BITS_EQUAL(&k_q_row0[(size_t)i * 4U], pose[0].q, 4);
        ASSERT_BITS_EQUAL(k_const_s0, pose[0].s, 3);

        ASSERT_BITS_EQUAL(&k_t_row0[(size_t)i * 3U], pose[1].t, 3);
        ASSERT_BITS_EQUAL(k_const_q1, pose[1].q, 4);
        ASSERT_BITS_EQUAL(defaults[1].s, pose[1].s, 3); /* absent */

        ASSERT_BITS_EQUAL(&k_t_row1[(size_t)i * 3U], pose[2].t, 3);
        ASSERT_BITS_EQUAL(&k_q_row1[(size_t)i * 4U], pose[2].q, 4);
        ASSERT_BITS_EQUAL(&k_s_row0[(size_t)i * 3U], pose[2].s, 3);

        /* keys at 0, 0.4 and 0.8 over a grid of 0.25. */
        uint32_t key = 0U;
        if (i >= 4U) {
            key = 2U;
        } else if (i >= 2U) {
            key = 1U;
        }
        ASSERT_BITS_EQUAL(k_const_t3, pose[3].t, 3);
        ASSERT_BITS_EQUAL(defaults[3].q, pose[3].q, 4); /* absent */
        ASSERT_BITS_EQUAL(&k_step_values[(size_t)key * 4U], pose[3].s, 3);
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

    /* 0.375 = block 1 plus half an interval, so u is exactly 0.5. */
    nt_skeletal_sample(clip, 0.375, defaults, pose);

    for (uint32_t c = 0; c < 3; ++c) {
        ASSERT_FLOAT_NEAR(mid(&k_t_row0[3], c), pose[1].t[c], 1e-6F);
        ASSERT_FLOAT_NEAR(mid(&k_t_row1[3], c), pose[2].t[c], 1e-6F);
        ASSERT_FLOAT_NEAR(mid(&k_s_row0[3], c), pose[2].s[c], 1e-6F);
    }
    for (uint32_t c = 0; c < 4; ++c) {
        ASSERT_FLOAT_NEAR(k_q_row0_mid[c], pose[0].q[c], 1e-6F);
        ASSERT_FLOAT_NEAR(k_q_row1_mid[c], pose[2].q[c], 1e-6F);
    }

    /* STEP holds the previous key up to but excluding its own timestamp. */
    nt_skeletal_sample(clip, (double)k_step_times[1] - 1e-6, defaults, pose);
    ASSERT_BITS_EQUAL(&k_step_values[0], pose[3].s, 3);
    nt_skeletal_sample(clip, (double)k_step_times[1], defaults, pose);
    ASSERT_BITS_EQUAL(&k_step_values[4], pose[3].s, 3);

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
    for (uint32_t c = 0; c < 3; ++c) {
        ASSERT_FLOAT_NEAR(mid(&k_object_t[3], c), out.t[c], 1e-6F);
    }

    nt_skeletal_assets_deactivate_clip(handle);
    free(payload);
}
// #endregion

// #region rejections
/* Only structure is checked at activation: a payload whose size, magic,
 * version, write indices or key partition do not hold could make the sampler
 * read or write outside the allocation. Values inside a sound structure belong
 * to the builder and to NT_SKELETAL_CHECKS.
 *
 * Every rejection runs against a pool of one asset: after N refusals a valid
 * payload must still activate, which proves nothing was published or held. */
#define EXPECT_CLIP_REJECTED(buffer, bytes, why) TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_skeletal_assets_activate_clip(buffer, bytes), why)

/* Offset of parent[j] / subtree_end[j] inside an NSKL payload. */
#define SKL_PARENT_AT(j) (16U + (2U * (j)))
#define SKL_END_AT(j) (16U + (2U * SKEL_JOINTS) + (2U * (j)))

/* The hierarchy rule is one walk, so each patch must be the single mutation that
 * reaches the rule its message names. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_skeleton_rejections(void) {
    reinit_assets(1);

    nt_skeletal_skeleton_t source = fixture_skeleton();
    uint8_t *valid = NULL;
    uint32_t size = 0;
    nt_builder_encode_skeleton(&source, &valid, &size);
    uint8_t *buf = (uint8_t *)calloc(size + 1U, 1);
    TEST_ASSERT_NOT_NULL(buf);

    memcpy(buf, valid, size);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_skeletal_assets_activate_skeleton(buf, size - 1U), "one byte short");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_skeletal_assets_activate_skeleton(buf, size + 1U), "one byte long");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_skeletal_assets_activate_skeleton(buf, 8U), "shorter than the header");

    memcpy(buf, valid, size);
    wr_u32(buf, 0xDEADBEEFU);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_skeletal_assets_activate_skeleton(buf, size), "bad magic");

    memcpy(buf, valid, size);
    wr_u16(buf + 4, NT_SKELETAL_FORMAT_VERSION + 1U);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_skeletal_assets_activate_skeleton(buf, size), "a later version");

    memcpy(buf, valid, size);
    wr_u16(buf + 6, 0U);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_skeletal_assets_activate_skeleton(buf, size), "joint_count 0");

    /* Sibling over-claim: joint 1's range is still open at joint 2, so joint 2
     * may not reach past it to joint 0. Range nesting alone accepts this. */
    memcpy(buf, valid, size);
    wr_u16(buf + SKL_PARENT_AT(2), 0U);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_skeletal_assets_activate_skeleton(buf, size), "a joint claimed by an open sibling subtree");

    /* parent[2] = 3 points forward, so the joints are not in preorder. */
    memcpy(buf, valid, size);
    wr_u16(buf + SKL_PARENT_AT(2), 3U);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_skeletal_assets_activate_skeleton(buf, size), "non-preorder parent");

    /* subtree_end[2] = 4 leaves the range of parent 1, which ends at 3. */
    memcpy(buf, valid, size);
    wr_u16(buf + SKL_END_AT(2), 4U);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_skeletal_assets_activate_skeleton(buf, size), "child outside its parent subtree");

    /* Joint 1 closes at 2 while joint 2 still names it as its parent. */
    memcpy(buf, valid, size);
    wr_u16(buf + SKL_END_AT(1), 2U);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_skeletal_assets_activate_skeleton(buf, size), "a subtree closing before its last descendant");

    /* Root 0 reaching to 4 swallows root 3, which names no parent. */
    memcpy(buf, valid, size);
    wr_u16(buf + SKL_END_AT(0), 4U);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_skeletal_assets_activate_skeleton(buf, size), "a root overlapping the next root");

    /* A range must end above its own joint. */
    memcpy(buf, valid, size);
    wr_u16(buf + SKL_END_AT(3), 3U);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_skeletal_assets_activate_skeleton(buf, size), "subtree_end not above its own joint");

    memcpy(buf, valid, size);
    const uint32_t handle = nt_skeletal_assets_activate_skeleton(buf, size);
    TEST_ASSERT_NOT_EQUAL_UINT32_MESSAGE(0, handle, "a valid skeleton still activates");
    nt_skeletal_assets_deactivate_skeleton(handle);

    free(buf);
    free(valid);
}

void test_skin_binding_rejections(void) {
    reinit_assets(1);

    nt_skin_binding_t source = fixture_binding();
    uint8_t *valid = NULL;
    uint32_t size = 0;
    nt_builder_encode_skin_binding(&source, &valid, &size);
    uint8_t *buf = (uint8_t *)calloc(size + 1U, 1);
    TEST_ASSERT_NOT_NULL(buf);

    memcpy(buf, valid, size);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_skeletal_assets_activate_skin_binding(buf, size - 1U), "one byte short");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_skeletal_assets_activate_skin_binding(buf, size + 1U), "one byte long");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_skeletal_assets_activate_skin_binding(buf, 8U), "shorter than the header");

    memcpy(buf, valid, size);
    wr_u32(buf, 0xDEADBEEFU);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_skeletal_assets_activate_skin_binding(buf, size), "bad magic");

    memcpy(buf, valid, size);
    wr_u16(buf + 4, NT_SKELETAL_FORMAT_VERSION + 1U);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_skeletal_assets_activate_skin_binding(buf, size), "a later version");

    memcpy(buf, valid, size);
    wr_u16(buf + 6, 0U);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, nt_skeletal_assets_activate_skin_binding(buf, size), "palette_count 0");

    memcpy(buf, valid, size);
    const uint32_t handle = nt_skeletal_assets_activate_skin_binding(buf, size);
    TEST_ASSERT_NOT_EQUAL_UINT32_MESSAGE(0, handle, "a valid binding still activates");
    nt_skeletal_assets_deactivate_skin_binding(handle);

    free(buf);
    free(valid);
}

/* Header rules, including the exact size the counts imply. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_clip_header_rejections(void) {
    reinit_assets(1);

    uint32_t size = 0;
    uint8_t *valid = encode_fixture_clip(&size);
    uint8_t *buf = (uint8_t *)calloc(size + 1U, 1);
    TEST_ASSERT_NOT_NULL(buf);

    memcpy(buf, valid, size);
    EXPECT_CLIP_REJECTED(buf, size - 1U, "one byte short");
    EXPECT_CLIP_REJECTED(buf, size + 1U, "one byte long");
    EXPECT_CLIP_REJECTED(buf, 64U, "shorter than the header");

    memcpy(buf, valid, size);
    wr_u32(buf, 0xDEADBEEFU);
    EXPECT_CLIP_REJECTED(buf, size, "bad magic");

    memcpy(buf, valid, size);
    wr_u16(buf + 4, NT_SKELETAL_FORMAT_VERSION + 1U);
    EXPECT_CLIP_REJECTED(buf, size, "a later version");

    memcpy(buf, valid, size);
    wr_u16(buf + ANM_HDR_JOINT_COUNT, 0U);
    EXPECT_CLIP_REJECTED(buf, size, "joint_count 0");

    memcpy(buf, valid, size);
    wr_u32(buf + ANM_HDR_SAMPLE_COUNT, 0U);
    EXPECT_CLIP_REJECTED(buf, size, "sample_count 0");

    memcpy(buf, valid, size);
    wr_f32(buf + ANM_HDR_DURATION, -1.0F);
    EXPECT_CLIP_REJECTED(buf, size, "negative duration");

    memcpy(buf, valid, size);
    wr_u32(buf + ANM_HDR_DURATION, 0x7FC00000U);
    EXPECT_CLIP_REJECTED(buf, size, "NaN duration");

    memcpy(buf, valid, size);
    wr_f32(buf + ANM_HDR_DURATION, 0.0F);
    EXPECT_CLIP_REJECTED(buf, size, "a grid with no interval to step through");

    memcpy(buf, valid, size);
    buf[ANM_HDR_OBJECT_MODE + 2U] = 4U;
    EXPECT_CLIP_REJECTED(buf, size, "unknown object channel mode");

    /* A count that no longer matches the arrays changes the payload size. */
    memcpy(buf, valid, size);
    wr_u16(buf + 32, 3U);
    EXPECT_CLIP_REJECTED(buf, size, "one sampled t row too many");

    memcpy(buf, valid, size);
    const uint32_t handle = nt_skeletal_assets_activate_clip(buf, size);
    TEST_ASSERT_NOT_EQUAL_UINT32_MESSAGE(0, handle, "a valid clip still activates");
    nt_skeletal_assets_deactivate_clip(handle);

    free(buf);
    free(valid);
}

/* Every table entry the sampler turns into a write index into the caller's pose. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_clip_write_index_rejections(void) {
    reinit_assets(1);

    uint32_t size = 0;
    uint8_t *valid = encode_fixture_clip(&size);
    uint8_t *buf = (uint8_t *)malloc(size);
    TEST_ASSERT_NOT_NULL(buf);

    const uint32_t tables[6] = {ANM_OFF_T_JOINT, ANM_OFF_Q_JOINT, ANM_OFF_S_JOINT, ANM_OFF_CT_JOINT, ANM_OFF_CQ_JOINT, ANM_OFF_CS_JOINT};
    for (uint32_t t = 0; t < 6; ++t) {
        memcpy(buf, valid, size);
        wr_u16(buf + tables[t], CLIP_JOINTS);
        EXPECT_CLIP_REJECTED(buf, size, "a joint table entry at joint_count");
    }

    memcpy(buf, valid, size);
    wr_u16(buf + ANM_OFF_STEPS + 8, CLIP_JOINTS);
    EXPECT_CLIP_REJECTED(buf, size, "a step track on a joint past the end");

    memcpy(buf, valid, size);
    buf[ANM_OFF_STEPS + 10] = 3U;
    EXPECT_CLIP_REJECTED(buf, size, "a step track on component 3");

    memcpy(buf, valid, size);
    const uint32_t handle = nt_skeletal_assets_activate_clip(buf, size);
    TEST_ASSERT_NOT_EQUAL_UINT32_MESSAGE(0, handle, "a valid clip still activates");
    nt_skeletal_assets_deactivate_clip(handle);

    free(buf);
    free(valid);
}

/* The STEP tracks must partition the key table exactly, joints first and then
 * the object channels, or a track would read keys that belong to another. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_clip_key_partition_rejections(void) {
    reinit_assets(1);

    uint32_t size = 0;
    uint8_t *valid = encode_fixture_clip(&size);
    uint8_t *buf = (uint8_t *)malloc(size);
    TEST_ASSERT_NOT_NULL(buf);

    memcpy(buf, valid, size);
    wr_u32(buf + ANM_OFF_STEPS + 4, 0U);
    EXPECT_CLIP_REJECTED(buf, size, "a step track with no keys");

    memcpy(buf, valid, size);
    wr_u32(buf + ANM_OFF_STEPS, 1U);
    EXPECT_CLIP_REJECTED(buf, size, "a joint track that does not start at key 0");

    memcpy(buf, valid, size);
    wr_u32(buf + ANM_HDR_OBJECT_STEP_FIRST + 4U, 4U);
    EXPECT_CLIP_REJECTED(buf, size, "a gap between the joint and object tracks");

    memcpy(buf, valid, size);
    wr_u32(buf + ANM_HDR_OBJECT_STEP_FIRST + 4U, 2U);
    EXPECT_CLIP_REJECTED(buf, size, "an object track sharing a joint track's key");

    memcpy(buf, valid, size);
    wr_u32(buf + ANM_HDR_OBJECT_STEP_COUNT + 4U, 0U);
    EXPECT_CLIP_REJECTED(buf, size, "an object step channel with no keys");

    /* Dropping the object channel to ABSENT leaves two keys no track owns. */
    memcpy(buf, valid, size);
    buf[ANM_HDR_OBJECT_MODE + 1U] = NT_SKELETAL_CHANNEL_ABSENT;
    EXPECT_CLIP_REJECTED(buf, size, "keys no track references");

    memcpy(buf, valid, size);
    const uint32_t handle = nt_skeletal_assets_activate_clip(buf, size);
    TEST_ASSERT_NOT_EQUAL_UINT32_MESSAGE(0, handle, "a valid clip still activates");
    nt_skeletal_assets_deactivate_clip(handle);

    free(buf);
    free(valid);
}

/* A grid needs two samples over a positive duration, which a payload can only
 * claim consistently when it is built for a single sample from the start. */
static uint32_t build_one_row_clip(uint8_t **out, uint32_t sample_count, float duration) {
    NtAnmHeader header;
    memset(&header, 0, sizeof(header));
    header.magic = NT_ANM_MAGIC;
    header.version = NT_SKELETAL_FORMAT_VERSION;
    header.joint_count = 1;
    header.sample_count = sample_count;
    header.duration = duration;
    header.n_t = 1;

    const uint32_t size = (uint32_t)nt_anm_size(&header);
    uint8_t *buf = (uint8_t *)calloc(size, 1);
    TEST_ASSERT_NOT_NULL(buf);
    memcpy(buf, &header, sizeof(header));
    for (uint32_t i = 0; i < sample_count; ++i) {
        wr_f32(buf + sizeof(header) + ((size_t)i * 12U), (float)i);
    }
    *out = buf;
    return size;
}

void test_clip_sampled_rows_need_a_grid(void) {
    reinit_assets(1);

    uint8_t *one_sample = NULL;
    const uint32_t one_size = build_one_row_clip(&one_sample, 1U, 1.0F);
    EXPECT_CLIP_REJECTED(one_sample, one_size, "a sampled row with a single sample");
    free(one_sample);

    uint8_t *no_duration = NULL;
    const uint32_t no_duration_size = build_one_row_clip(&no_duration, 2U, 0.0F);
    EXPECT_CLIP_REJECTED(no_duration, no_duration_size, "a sampled row over a zero duration");
    free(no_duration);

    uint8_t *valid = NULL;
    const uint32_t valid_size = build_one_row_clip(&valid, 2U, 1.0F);
    const uint32_t handle = nt_skeletal_assets_activate_clip(valid, valid_size);
    TEST_ASSERT_NOT_EQUAL_UINT32_MESSAGE(0, handle, "two samples over a positive duration activate");
    nt_skeletal_assets_deactivate_clip(handle);
    free(valid);
}

#if NT_ASSERT_MODE == NT_ASSERT_FULL
/* Capacity is a game decision, so exhausting the pool is a programming error. */
void test_pool_overflow_asserts(void) {
    reinit_assets(1);

    uint32_t size = 0;
    uint8_t *payload = encode_fixture_clip(&size);
    const uint32_t handle = nt_skeletal_assets_activate_clip(payload, size);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, handle);
    NT_TEST_EXPECT_ASSERT(nt_skeletal_assets_activate_clip(payload, size));

    nt_skeletal_assets_deactivate_clip(handle);
    free(payload);
}
#endif
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
    clip.duration = 2.0F; /* tells the two clip views apart */
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
    TEST_ASSERT_TRUE(clip_run->duration != clip_walk->duration);

    nt_skeletal_trs_t defaults[CLIP_JOINTS];
    nt_skeletal_trs_t before[CLIP_JOINTS];
    nt_skeletal_trs_t after[CLIP_JOINTS];
    nt_skeletal_trs_t object_before;
    nt_skeletal_trs_t object_after;
    build_defaults(defaults, CLIP_JOINTS);
    nt_skeletal_sample(clip_run, 0.375, defaults, before);
    nt_skeletal_sample_object(&clip_run->object, 0.375, defaults, &object_before);

    /* Copy-out: both blobs may be dropped or overwritten after activation. */
    wipe_asset_payloads(pack_a);
    wipe_asset_payloads(pack_b);
    nt_skeletal_sample(clip_run, 0.375, defaults, after);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(before, after, sizeof(before), "the clip view must not read the pack blob");
    nt_skeletal_sample_object(&clip_run->object, 0.375, defaults, &object_after);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&object_before, &object_after, sizeof(object_after), "the object curve must not read the pack blob either");

    /* The skeleton tables are copies too, so they still read back in full. */
    for (uint16_t j = 0; j < SKEL_JOINTS; ++j) {
        TEST_ASSERT_EQUAL_UINT16(k_parent[j], skeleton->parent[j]);
        TEST_ASSERT_EQUAL_UINT16(k_subtree_end[j], skeleton->subtree_end[j]);
        TEST_ASSERT_EQUAL_HEX32(k_joint_id[j], skeleton->joint_id[j]);
        ASSERT_BITS_EQUAL(k_rest[j].t, skeleton->rest[j].t, 3);
        ASSERT_BITS_EQUAL(k_rest[j].q, skeleton->rest[j].q, 4);
        ASSERT_BITS_EQUAL(k_rest[j].s, skeleton->rest[j].s, 3);
    }

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
    (void)remove(path_a);
    (void)remove(path_b);
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
    RUN_TEST(test_clip_header_rejections);
    RUN_TEST(test_clip_write_index_rejections);
    RUN_TEST(test_clip_key_partition_rejections);
    RUN_TEST(test_clip_sampled_rows_need_a_grid);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_pool_overflow_asserts);
#endif
    RUN_TEST(test_two_packs_share_one_skeleton);
    return UNITY_END();
}
