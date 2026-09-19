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
#include "nt_builder_internal.h"
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
/* Two different exact values, so a swapped pair of header floats fails here. */
#define SKIN_REACH 1.25F
#define SKIN_ANY_POSE_RADIUS 3.5F

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
        .reach = SKIN_REACH,
        .any_pose_radius = SKIN_ANY_POSE_RADIUS,
        .palette_count = SKIN_PALETTE,
    };
}

/*
 * Asymmetric clip: a different mix per joint and component, two sampled rows of
 * the same kind and two base-pose constants of one kind, so a row index or a
 * per-kind table offset that is ignored cannot pass.
 *
 *   joint 0: t base constant, q sampled (q row 0), s base constant
 *   joint 1: t sampled (t row 0), q base constant, s rest
 *   joint 2: t sampled (t row 1), q sampled (q row 1), s sampled (s row 0)
 *   joint 3: t base constant, q rest, s rest
 *
 * duration 1 over 5 samples, so every grid time is binary exact. The rotation
 * rows turn about one axis in equal steps: the nlerp midpoint of two of them is
 * the rotation at their mean angle, which the test writes down as a literal
 * instead of reimplementing the kernel.
 */
#define CLIP_JOINTS 4
#define CLIP_SAMPLES 5
#define CLIP_STRIDE 17

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

/* nlerp midpoints of samples 1 and 2: 33.75 degrees about z, 56.25 about x. */
static const float k_q_row0_mid[4] = {0.0F, 0.0F, 0.29028483F, 0.95694034F};
static const float k_q_row1_mid[4] = {0.47139674F, 0.0F, 0.0F, 0.88192126F};

static const float k_const_t0[3] = {1.5F, -2.25F, 0.75F};
static const float k_const_s0[3] = {2.0F, 3.0F, 4.0F};
static const float k_const_q1[4] = {0.0F, 0.0F, 0.70710678F, 0.70710678F};
static const float k_const_t3[3] = {-5.5F, 6.25F, -7.125F};

/* The rest pose with the constants written in; the channels the rows drive
 * hold rest values the rows never produce, so a row that missed its joint
 * would show. */
static const nt_skeletal_trs_t k_base[CLIP_JOINTS] = {
    {{1.5F, -2.25F, 0.75F}, {0.0F, 0.0F, 0.0F, 1.0F}, {2.0F, 3.0F, 4.0F}},
    {{0.0F, 0.5F, -0.25F}, {0.0F, 0.0F, 0.70710678F, 0.70710678F}, {2.0F, 1.0F, 0.5F}},
    {{-1.5F, 0.0F, 0.125F}, {0.70710678F, 0.0F, 0.0F, 0.70710678F}, {1.0F, 1.0F, 1.0F}},
    {{-5.5F, 6.25F, -7.125F}, {0.0F, 1.0F, 0.0F, 0.0F}, {0.25F, 0.25F, 0.25F}},
};

static const uint16_t k_t_joint[2] = {1, 2};
static const uint16_t k_q_joint[2] = {0, 2};
static const uint16_t k_s_joint[1] = {2};
static float g_blocks[CLIP_SAMPLES * CLIP_STRIDE];

static void fixture_clip(nt_skeletal_clip_t *clip) {
    /* Sample-major: both t rows, both q rows, then the s row per block. */
    for (uint32_t i = 0; i < CLIP_SAMPLES; ++i) {
        float *block = g_blocks + ((size_t)i * CLIP_STRIDE);
        memcpy(block, &k_t_row0[(size_t)i * 3U], 3U * sizeof(float));
        memcpy(block + 3, &k_t_row1[(size_t)i * 3U], 3U * sizeof(float));
        memcpy(block + 6, &k_q_row0[(size_t)i * 4U], 4U * sizeof(float));
        memcpy(block + 10, &k_q_row1[(size_t)i * 4U], 4U * sizeof(float));
        memcpy(block + 14, &k_s_row0[(size_t)i * 3U], 3U * sizeof(float));
    }
    *clip = (nt_skeletal_clip_t){
        .rig_compat_id = (nt_hash64_t){.value = fixture_rig_id()},
        .duration = 1.0,
        .base = k_base,
        .blocks = g_blocks,
        .t_joint = k_t_joint,
        .q_joint = k_q_joint,
        .s_joint = k_s_joint,
        .r_joints = 1.5F,
        .r_root = 0.25F,
        .s_max = 2.0F,
        .sample_count = CLIP_SAMPLES,
        .joint_count = CLIP_JOINTS,
        .n_t = 2,
        .n_q = 2,
        .n_s = 1,
    };
}

static uint8_t *encode_fixture_clip(uint32_t *out_size) {
    nt_skeletal_clip_t clip;
    fixture_clip(&clip);
    uint8_t *payload = NULL;
    nt_builder_encode_clip(&clip, &payload, out_size);
    TEST_ASSERT_NOT_NULL(payload);
    return payload;
}

/* Offsets of the fixture clip's payload, in the order §16 lists them; the
 * rejection tests patch bytes through these. */
enum {
    ANM_OFF_BASE = 44,
    ANM_OFF_BLOCKS = ANM_OFF_BASE + (CLIP_JOINTS * 40),
    ANM_OFF_T_JOINT = ANM_OFF_BLOCKS + (CLIP_SAMPLES * CLIP_STRIDE * 4),
    ANM_OFF_Q_JOINT = ANM_OFF_T_JOINT + 4,
    ANM_OFF_S_JOINT = ANM_OFF_Q_JOINT + 4,
    ANM_SIZE = ANM_OFF_S_JOINT + 2,
};

/* Header field offsets, from NtAnmHeader. */
enum {
    ANM_HDR_JOINT_COUNT = 6,
    ANM_HDR_SAMPLE_COUNT = 8,
    ANM_HDR_DURATION = 12,
    ANM_HDR_R_JOINTS = 24,
    ANM_HDR_R_ROOT = 28,
    ANM_HDR_S_MAX = 32,
    ANM_HDR_N_T = 36,
};

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
    const float k_radii[2] = {SKIN_REACH, SKIN_ANY_POSE_RADIUS};
    ASSERT_BITS_EQUAL(k_radii, &view->reach, 1);
    ASSERT_BITS_EQUAL(&k_radii[1], &view->any_pose_radius, 1);
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
    TEST_ASSERT_EQUAL_UINT16(CLIP_JOINTS, clip->joint_count);
    TEST_ASSERT_EQUAL_UINT32(CLIP_SAMPLES, clip->sample_count);
    TEST_ASSERT_TRUE(clip->duration == 1.0);
    TEST_ASSERT_TRUE(clip->r_joints == 1.5F && clip->r_root == 0.25F && clip->s_max == 2.0F);

    TEST_ASSERT_EQUAL_UINT16(2, clip->n_t);
    TEST_ASSERT_EQUAL_UINT16(2, clip->n_q);
    TEST_ASSERT_EQUAL_UINT16(1, clip->n_s);
    TEST_ASSERT_EQUAL_UINT16(1, clip->t_joint[0]);
    TEST_ASSERT_EQUAL_UINT16(2, clip->t_joint[1]);
    TEST_ASSERT_EQUAL_UINT16(0, clip->q_joint[0]);
    TEST_ASSERT_EQUAL_UINT16(2, clip->q_joint[1]);
    TEST_ASSERT_EQUAL_UINT16(2, clip->s_joint[0]);

    TEST_ASSERT_EQUAL_MEMORY(k_base, clip->base, sizeof(k_base));

    /* Frame blocks hold both t rows, both q rows, then the s row. */
    for (uint32_t i = 0; i < CLIP_SAMPLES; ++i) {
        const float *block = clip->blocks + ((size_t)i * CLIP_STRIDE);
        ASSERT_BITS_EQUAL(&k_t_row0[(size_t)i * 3U], block, 3);
        ASSERT_BITS_EQUAL(&k_t_row1[(size_t)i * 3U], block + 3, 3);
        ASSERT_BITS_EQUAL(&k_q_row0[(size_t)i * 4U], block + 6, 4);
        ASSERT_BITS_EQUAL(&k_q_row1[(size_t)i * 4U], block + 10, 4);
        ASSERT_BITS_EQUAL(&k_s_row0[(size_t)i * 3U], block + 14, 3);
    }

    nt_skeletal_assets_deactivate_clip(handle);
    free(payload);
}

/* The activator and the builder read one payload through nt_skeletal_clip_view:
 * the view over the caller's bytes and the activated view agree field by field,
 * pointers as offsets into their own copy. */
#define ASSERT_SAME_OFFSET(name) TEST_ASSERT_EQUAL_MESSAGE((const uint8_t *)direct.name - payload, (const uint8_t *)clip->name - base, #name)
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_clip_view_matches_activator(void) {
    uint32_t size = 0;
    uint8_t *payload = encode_fixture_clip(&size);
    nt_skeletal_clip_t direct;
    nt_skeletal_clip_view(payload, &direct);

    const uint32_t handle = nt_skeletal_assets_activate_clip(payload, size);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, handle);
    const nt_skeletal_clip_t *clip = nt_skeletal_assets_clip(publish_handle("clips/view.nanm", NT_ASSET_CLIP, handle));
    /* The activated copy starts where its base starts, one header back. */
    const uint8_t *base = (const uint8_t *)clip->base - ((const uint8_t *)direct.base - payload);
    TEST_ASSERT_EQUAL_MEMORY(payload, base, size);

    ASSERT_SAME_OFFSET(base);
    ASSERT_SAME_OFFSET(blocks);
    ASSERT_SAME_OFFSET(t_joint);
    ASSERT_SAME_OFFSET(q_joint);
    ASSERT_SAME_OFFSET(s_joint);

    TEST_ASSERT_EQUAL_HEX64(direct.rig_compat_id.value, clip->rig_compat_id.value);
    TEST_ASSERT_TRUE(direct.duration == clip->duration);
    TEST_ASSERT_TRUE(direct.r_joints == clip->r_joints && direct.r_root == clip->r_root && direct.s_max == clip->s_max);
    TEST_ASSERT_EQUAL_UINT32(direct.sample_count, clip->sample_count);
    TEST_ASSERT_EQUAL_UINT16(direct.joint_count, clip->joint_count);
    TEST_ASSERT_TRUE(direct.n_t == clip->n_t && direct.n_q == clip->n_q && direct.n_s == clip->n_s);

    nt_skeletal_assets_deactivate_clip(handle);
    free(payload);
}
// #endregion

// #region sampling
/* Every grid time reproduces the source samples bit for bit and every channel
 * without a row keeps the base. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_clip_samples_grid_times_exactly(void) {
    uint32_t size = 0;
    uint8_t *payload = encode_fixture_clip(&size);
    const uint32_t handle = nt_skeletal_assets_activate_clip(payload, size);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, handle);
    const nt_skeletal_clip_t *clip = nt_skeletal_assets_clip(publish_handle("clips/b.nanm", NT_ASSET_CLIP, handle));

    nt_skeletal_trs_t pose[CLIP_JOINTS];
    for (uint32_t i = 0; i < CLIP_SAMPLES; ++i) {
        const double time = (double)i * 0.25;
        nt_skeletal_sample(clip, time, pose);

        ASSERT_BITS_EQUAL(k_const_t0, pose[0].t, 3);
        ASSERT_BITS_EQUAL(&k_q_row0[(size_t)i * 4U], pose[0].q, 4);
        ASSERT_BITS_EQUAL(k_const_s0, pose[0].s, 3);

        ASSERT_BITS_EQUAL(&k_t_row0[(size_t)i * 3U], pose[1].t, 3);
        ASSERT_BITS_EQUAL(k_const_q1, pose[1].q, 4);
        ASSERT_BITS_EQUAL(k_rest[1].s, pose[1].s, 3); /* no row */

        ASSERT_BITS_EQUAL(&k_t_row1[(size_t)i * 3U], pose[2].t, 3);
        ASSERT_BITS_EQUAL(&k_q_row1[(size_t)i * 4U], pose[2].q, 4);
        ASSERT_BITS_EQUAL(&k_s_row0[(size_t)i * 3U], pose[2].s, 3);

        ASSERT_BITS_EQUAL(k_const_t3, pose[3].t, 3);
        ASSERT_BITS_EQUAL(k_rest[3].q, pose[3].q, 4); /* no row */
        ASSERT_BITS_EQUAL(k_rest[3].s, pose[3].s, 3); /* no row */
    }

    nt_skeletal_assets_deactivate_clip(handle);
    free(payload);
}

/* Between grid points the decoded blocks interpolate. */
void test_clip_interpolates_between_samples(void) {
    uint32_t size = 0;
    uint8_t *payload = encode_fixture_clip(&size);
    const uint32_t handle = nt_skeletal_assets_activate_clip(payload, size);
    const nt_skeletal_clip_t *clip = nt_skeletal_assets_clip(publish_handle("clips/c.nanm", NT_ASSET_CLIP, handle));

    nt_skeletal_trs_t pose[CLIP_JOINTS];
    /* 0.375 = block 1 plus half an interval, so u is exactly 0.5. */
    nt_skeletal_sample(clip, 0.375, pose);

    for (uint32_t c = 0; c < 3; ++c) {
        ASSERT_FLOAT_NEAR(mid(&k_t_row0[3], c), pose[1].t[c], 1e-6F);
        ASSERT_FLOAT_NEAR(mid(&k_t_row1[3], c), pose[2].t[c], 1e-6F);
        ASSERT_FLOAT_NEAR(mid(&k_s_row0[3], c), pose[2].s[c], 1e-6F);
    }
    for (uint32_t c = 0; c < 4; ++c) {
        ASSERT_FLOAT_NEAR(k_q_row0_mid[c], pose[0].q[c], 1e-6F);
        ASSERT_FLOAT_NEAR(k_q_row1_mid[c], pose[2].q[c], 1e-6F);
    }
    ASSERT_BITS_EQUAL(k_const_t0, pose[0].t, 3);
    ASSERT_BITS_EQUAL(k_base[3].s, pose[3].s, 3);

    nt_skeletal_assets_deactivate_clip(handle);
    free(payload);
}
// #endregion

// #region rejections
/* Only structure is checked at activation: a payload whose size, magic,
 * version, grid or write indices do not hold could make the sampler read or
 * write outside the allocation. Values inside a sound structure belong to the
 * builder and to NT_SKELETAL_CHECKS.
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

    /* A radius is a value, not structure: a NaN one activates and is the
     * builder's bug to have caught. */
    memcpy(buf, valid, size);
    wr_u32(buf + 20, 0x7FC00000U);
    const uint32_t nan_handle = nt_skeletal_assets_activate_skin_binding(buf, size);
    TEST_ASSERT_NOT_EQUAL_UINT32_MESSAGE(0, nan_handle, "a NaN any_pose_radius is not the activator's business");
    nt_skeletal_assets_deactivate_skin_binding(nan_handle);

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
    EXPECT_CLIP_REJECTED(buf, 43U, "shorter than the header");

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

    /* A count that no longer matches the arrays changes the payload size. */
    memcpy(buf, valid, size);
    wr_u16(buf + ANM_HDR_N_T, 3U);
    EXPECT_CLIP_REJECTED(buf, size, "one sampled t row too many");

    /* Bounds are values: a broken one activates and is the builder's bug to
     * have caught, not the activator's. */
    memcpy(buf, valid, size);
    wr_f32(buf + ANM_HDR_R_ROOT, -1.0F);
    wr_u32(buf + ANM_HDR_R_JOINTS, 0x7FC00000U);
    wr_u32(buf + ANM_HDR_S_MAX, 0x7F800000U);
    const uint32_t bounds_handle = nt_skeletal_assets_activate_clip(buf, size);
    TEST_ASSERT_NOT_EQUAL_UINT32_MESSAGE(0, bounds_handle, "broken bounds are not the activator's business");
    nt_skeletal_assets_deactivate_clip(bounds_handle);

    memcpy(buf, valid, size);
    const uint32_t handle = nt_skeletal_assets_activate_clip(buf, size);
    TEST_ASSERT_NOT_EQUAL_UINT32_MESSAGE(0, handle, "a valid clip still activates");
    nt_skeletal_assets_deactivate_clip(handle);

    free(buf);
    free(valid);
}

/* Every table entry the sampler turns into a write index into the caller's
 * pose. A table rejection happens after the slot copy, against a pool of one:
 * the slot must come back for the valid payload. */
void test_clip_write_index_rejections(void) {
    reinit_assets(1);

    uint32_t size = 0;
    uint8_t *valid = encode_fixture_clip(&size);
    uint8_t *buf = (uint8_t *)malloc(size);
    TEST_ASSERT_NOT_NULL(buf);

    const uint32_t tables[3] = {ANM_OFF_T_JOINT, ANM_OFF_Q_JOINT, ANM_OFF_S_JOINT};
    for (uint32_t t = 0; t < 3; ++t) {
        memcpy(buf, valid, size);
        wr_u16(buf + tables[t], CLIP_JOINTS);
        EXPECT_CLIP_REJECTED(buf, size, "a joint table entry at joint_count");
    }

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
    memcpy(buf + sizeof(header), &k_rest[0], sizeof(k_rest[0]));
    for (uint32_t i = 0; i < sample_count; ++i) {
        wr_f32(buf + sizeof(header) + sizeof(k_rest[0]) + ((size_t)i * 12U), (float)i);
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
    nt_skeletal_clip_t clip;
    fixture_clip(&clip);
    nt_builder_add_skeleton(ctx, &skel, "rigs/hero.nskl");
    nt_builder_add_clip(ctx, &clip, "clips/run.nanm");
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    nt_builder_free_pack(ctx);
}

static void build_pack_b(const char *path) {
    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL(ctx);
    nt_skeletal_clip_t clip;
    fixture_clip(&clip);
    clip.duration = 2.0; /* tells the two clip views apart */
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

    nt_skeletal_trs_t before[CLIP_JOINTS];
    nt_skeletal_trs_t after[CLIP_JOINTS];
    nt_skeletal_sample(clip_run, 0.375, before);

    /* Copy-out: both blobs may be dropped or overwritten after activation. */
    wipe_asset_payloads(pack_a);
    wipe_asset_payloads(pack_b);
    nt_skeletal_sample(clip_run, 0.375, after);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(before, after, sizeof(before), "the clip view must not read the pack blob");

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
    nt_skeletal_sample(clip_run_again, 0.375, after);
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
    RUN_TEST(test_clip_view_matches_activator);
    RUN_TEST(test_clip_samples_grid_times_exactly);
    RUN_TEST(test_clip_interpolates_between_samples);
    RUN_TEST(test_skeleton_rejections);
    RUN_TEST(test_skin_binding_rejections);
    RUN_TEST(test_clip_header_rejections);
    RUN_TEST(test_clip_write_index_rejections);
    RUN_TEST(test_clip_sampled_rows_need_a_grid);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_pool_overflow_asserts);
#endif
    RUN_TEST(test_two_packs_share_one_skeleton);
    return UNITY_END();
}
