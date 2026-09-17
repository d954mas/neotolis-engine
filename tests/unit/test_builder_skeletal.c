/* Wire-format coverage for the NSKL / NSKN / NANM encoders: the payload bytes
 * are read back at hand-computed offsets, so a moved field or a reordered array
 * fails here and not only in the round-trip suite. */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Windows SDK must be included early (before stdnoreturn.h from C17 headers) */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/* clang-format off */
#include "nt_builder.h"
#include "nt_pack_format.h"
#include "nt_skeletal_format.h"
#include "unity.h"
/* clang-format on */

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

#define TMP_DIR "build/tests/tmp"
#define PACK_PATH TMP_DIR "/builder_skeletal.ntpack"

void setUp(void) {}
void tearDown(void) {}

// #region build-assert trap
/* Same shape as test_builder.c, without the context: the encoders own no
 * builder state and abort before they allocate, so nothing needs freeing. */
static jmp_buf s_build_assert_jmp;
static const char *s_build_assert_expr;

static void test_build_assert_handler(const char *expr, const char *file, int line) {
    s_build_assert_expr = expr;
    (void)file;
    (void)line;
    longjmp(s_build_assert_jmp, 1);
}

/* Which rule fired is the claim: a death test that only sees "some assert"
 * passes on an unrelated precondition too. */
#define EXPECT_BUILD_ASSERT_MATCH(code, expected)                                                                                                                                                      \
    do {                                                                                                                                                                                               \
        s_build_assert_expr = NULL;                                                                                                                                                                    \
        nt_build_assert_handler = test_build_assert_handler;                                                                                                                                           \
        if (setjmp(s_build_assert_jmp) == 0) {                                                                                                                                                         \
            code;                                                                                                                                                                                      \
            nt_build_assert_handler = NULL;                                                                                                                                                            \
            TEST_FAIL_MESSAGE("expected NT_BUILD_ASSERT to fire: " expected);                                                                                                                          \
        }                                                                                                                                                                                              \
        nt_build_assert_handler = NULL;                                                                                                                                                                \
        TEST_ASSERT_TRUE_MESSAGE(s_build_assert_expr &&strstr(s_build_assert_expr, (expected)), "a different NT_BUILD_ASSERT fired: " expected);                                                       \
    } while (0)
// #endregion

// #region little-endian readers
static uint16_t rd_u16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8)); }
static uint32_t rd_u32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t rd_u64(const uint8_t *p) { return (uint64_t)rd_u32(p) | ((uint64_t)rd_u32(p + 4) << 32); }

/* Wire floats are compared as raw bits: the test config excludes Unity's float
 * asserts, and a bit compare is the stronger claim for an encoder anyway. */
static uint32_t f32_bits(float v) {
    uint32_t bits = 0;
    memcpy(&bits, &v, sizeof(bits));
    return bits;
}
// #endregion

// #region fixtures
/* Two roots, one three-deep chain: joint 3 is a second root, so the root rule
 * (subtree ends where the next root starts) and the nesting rule are both
 * exercised by one skeleton. */
#define FIXTURE_JOINTS 4

static const uint16_t k_parent[FIXTURE_JOINTS] = {NT_SKELETAL_NO_PARENT, 0, 1, NT_SKELETAL_NO_PARENT};
static const uint16_t k_subtree_end[FIXTURE_JOINTS] = {3, 3, 3, 4};
static const uint32_t k_joint_id[FIXTURE_JOINTS] = {0x11111111U, 0x22222222U, 0x33333333U, 0x44444444U};

static const nt_skeletal_trs_t k_rest[FIXTURE_JOINTS] = {
    {{1.0F, 2.0F, 3.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}},
    {{0.0F, 0.5F, -0.25F}, {0.0F, 0.0F, -0.70710678F, 0.70710678F}, {2.0F, 1.0F, 0.5F}},
    {{-1.5F, 0.0F, 0.125F}, {0.70710678F, 0.0F, 0.0F, 0.70710678F}, {1.0F, 1.0F, 1.0F}},
    {{4.0F, -4.0F, 0.0F}, {0.0F, 1.0F, 0.0F, 0.0F}, {0.25F, 0.25F, 0.25F}},
};

/* rig_compat_id is an output of the encoder, so the fixture leaves the field at
 * a value that is deliberately not the rig's own identity. */
static nt_skeletal_skeleton_t fixture_skeleton(void) {
    nt_skeletal_skeleton_t skel = {0};
    skel.rig_compat_id = (nt_hash64_t){0xDEADBEEFDEADBEEFULL};
    skel.parent = k_parent;
    skel.subtree_end = k_subtree_end;
    skel.joint_id = k_joint_id;
    skel.rest = k_rest;
    skel.joint_count = FIXTURE_JOINTS;
    return skel;
}

static uint64_t fixture_rig_id(void) {
    nt_skeletal_skeleton_t skel = fixture_skeleton();
    uint8_t scratch[NT_SKELETAL_RIG_ID_BYTES(FIXTURE_JOINTS)];
    return nt_skeletal_rig_compat_id(&skel, scratch, (uint32_t)sizeof(scratch)).value;
}

#define FIXTURE_PALETTE 3

static const uint16_t k_remap[FIXTURE_PALETTE] = {0, 2, 1};

static const nt_skeletal_mat34_t k_inverse_bind[FIXTURE_PALETTE] = {
    {{{1.0F, 0.0F, 0.0F, -1.0F}, {0.0F, 1.0F, 0.0F, -2.0F}, {0.0F, 0.0F, 1.0F, -3.0F}}},
    {{{0.0F, -1.0F, 0.0F, 0.5F}, {1.0F, 0.0F, 0.0F, 0.25F}, {0.0F, 0.0F, 1.0F, 0.125F}}},
    {{{2.0F, 0.0F, 0.0F, 7.0F}, {0.0F, 2.0F, 0.0F, 8.0F}, {0.0F, 0.0F, 2.0F, 9.0F}}},
};

static nt_skin_binding_t fixture_binding(void) {
    nt_skin_binding_t binding = {0};
    binding.rig_compat_id = (nt_hash64_t){0xABCDEF0123456789ULL};
    binding.remap = k_remap;
    binding.inverse_bind = k_inverse_bind;
    binding.palette_count = FIXTURE_PALETTE;
    return binding;
}

/*
 * Asymmetric clip: two sampled translation rows against one sampled rotation
 * row, constants of two different component kinds, a joint STEP track and an
 * object curve that is sampled in t and stepped in q -- so the block layout,
 * the per-kind tables and the joint/object key partition all carry more than
 * one element and cannot pass by coincidence.
 *
 *   joint 0: t sampled (row 0), q constant, s absent
 *   joint 1: t absent, q sampled (row 0), s constant
 *   joint 2: t sampled (row 1), q absent, s step (3 keys)
 *   object : t sampled, q step (2 keys), s absent
 */
#define CLIP_JOINTS 3
#define CLIP_SAMPLES 5
#define CLIP_CHANNELS (3 * (CLIP_JOINTS + 1))

static const float k_t_row0[CLIP_SAMPLES * 3] = {0.0F, 0.0F, 0.0F, 1.0F, 0.5F, 0.0F, 2.0F, 1.0F, 0.0F, 3.0F, 1.5F, 0.0F, 4.0F, 2.0F, 0.0F};
static const float k_t_row1[CLIP_SAMPLES * 3] = {10.0F, -1.0F, 0.5F, 10.5F, -2.0F, 1.5F, 11.0F, -3.0F, 2.5F, 11.5F, -4.0F, 3.5F, 12.0F, -5.0F, 4.5F};
static const float k_q_row0[CLIP_SAMPLES * 4] = {
    0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.38268343F, 0.92387953F, 0.0F, 0.0F, 0.70710678F, 0.70710678F, 0.0F, 0.0F, 0.92387953F, 0.38268343F, 0.0F, 0.0F, 1.0F, 0.0F,
};
static const float k_object_t[CLIP_SAMPLES * 3] = {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.25F, 0.0F, 0.0F, 0.5F, 0.0F, 0.0F, 0.75F, 0.0F, 0.0F, 1.0F};

static const float k_const_q0[4] = {0.0F, 0.0F, 0.70710678F, 0.70710678F};
static const float k_const_s1[4] = {2.0F, 3.0F, 4.0F, 0.0F};

static const float k_step_times[3] = {0.0F, 0.4F, 0.8F};
static const float k_step_values[3 * 4] = {1.0F, 1.0F, 1.0F, 0.0F, 2.0F, 0.5F, 3.0F, 0.0F, 0.25F, 4.0F, 0.5F, 0.0F};
static const float k_object_step_times[2] = {0.0F, 0.5F};
static const float k_object_step_values[2 * 4] = {0.38268343F, 0.0F, 0.0F, 0.92387953F, 0.0F, 0.0F, 0.70710678F, 0.70710678F};

/* channels[c]: joint c/3, component c%3 (0 = t, 1 = q, 2 = s). */
static void fixture_clip(nt_builder_clip_t *clip, nt_builder_anim_channel_t channels[CLIP_CHANNELS]) {
    memset(channels, 0, sizeof(nt_builder_anim_channel_t) * (size_t)CLIP_CHANNELS);

    channels[0].mode = NT_SKELETAL_CHANNEL_SAMPLED;
    channels[0].samples = k_t_row0;
    channels[1].mode = NT_SKELETAL_CHANNEL_CONSTANT;
    memcpy(channels[1].constant, k_const_q0, sizeof(k_const_q0));

    channels[4].mode = NT_SKELETAL_CHANNEL_SAMPLED;
    channels[4].samples = k_q_row0;
    channels[5].mode = NT_SKELETAL_CHANNEL_CONSTANT;
    memcpy(channels[5].constant, k_const_s1, sizeof(k_const_s1));

    channels[6].mode = NT_SKELETAL_CHANNEL_SAMPLED;
    channels[6].samples = k_t_row1;
    channels[8].mode = NT_SKELETAL_CHANNEL_STEP;
    channels[8].step_times = k_step_times;
    channels[8].step_values = k_step_values;
    channels[8].step_count = 3;

    channels[9].mode = NT_SKELETAL_CHANNEL_SAMPLED;
    channels[9].samples = k_object_t;
    channels[10].mode = NT_SKELETAL_CHANNEL_STEP;
    channels[10].step_times = k_object_step_times;
    channels[10].step_values = k_object_step_values;
    channels[10].step_count = 2;

    memset(clip, 0, sizeof(*clip));
    clip->rig_compat_id = 0xABCDEF0123456789ULL;
    clip->additive_ref_id = 0;
    clip->joint_count = CLIP_JOINTS;
    clip->sample_count = CLIP_SAMPLES;
    clip->duration = 1.0F;
    clip->channels = channels;
}

/* Hand-computed offsets of the fixture clip: header 120, blocks 5 x 10 floats,
 * no ct, one cq, one cs, one step track, five keys, five object samples, then
 * the joint tables. */
enum {
    FIX_OFF_BLOCKS = 120,
    FIX_OFF_CT = FIX_OFF_BLOCKS + (CLIP_SAMPLES * 10 * 4),
    FIX_OFF_CQ = FIX_OFF_CT,
    FIX_OFF_CS = FIX_OFF_CQ + 16,
    FIX_OFF_STEPS = FIX_OFF_CS + 12,
    FIX_OFF_KEYS = FIX_OFF_STEPS + 12,
    FIX_OFF_OBJECT = FIX_OFF_KEYS + (5 * 20),
    FIX_OFF_T_JOINT = FIX_OFF_OBJECT + (CLIP_SAMPLES * 40),
    FIX_OFF_Q_JOINT = FIX_OFF_T_JOINT + 4,
    FIX_OFF_CQ_JOINT = FIX_OFF_Q_JOINT + 2,
    FIX_OFF_CS_JOINT = FIX_OFF_CQ_JOINT + 2,
    FIX_SIZE = FIX_OFF_CS_JOINT + 2,
};
// #endregion

// #region NSKL
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_encode_skeleton_wire_layout(void) {
    nt_skeletal_skeleton_t skel = fixture_skeleton();
    uint8_t *payload = NULL;
    uint32_t size = 0;
    const nt_hash64_t rig = nt_builder_encode_skeleton(&skel, &payload, &size);
    TEST_ASSERT_NOT_NULL(payload);

    /* The encoder computes the identity and ignores the field it was handed. */
    TEST_ASSERT_EQUAL_HEX64(fixture_rig_id(), rig.value);
    TEST_ASSERT_EQUAL_HEX64(rig.value, rd_u64(payload + 8));

    TEST_ASSERT_EQUAL_UINT32(16U + (48U * FIXTURE_JOINTS), size);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)NT_SKL_SIZE(FIXTURE_JOINTS), size);
    TEST_ASSERT_EQUAL_HEX32(NT_SKL_MAGIC, rd_u32(payload));
    TEST_ASSERT_EQUAL_UINT16(NT_SKELETAL_FORMAT_VERSION, rd_u16(payload + 4));
    TEST_ASSERT_EQUAL_UINT16(FIXTURE_JOINTS, rd_u16(payload + 6));

    const uint8_t *parent = payload + 16;
    const uint8_t *subtree_end = parent + (size_t)(2 * FIXTURE_JOINTS);
    const uint8_t *joint_id = subtree_end + (size_t)(2 * FIXTURE_JOINTS);
    const uint8_t *rest = joint_id + (size_t)(4 * FIXTURE_JOINTS);
    for (size_t j = 0; j < FIXTURE_JOINTS; j++) {
        TEST_ASSERT_EQUAL_UINT16(k_parent[j], rd_u16(parent + (2 * j)));
        TEST_ASSERT_EQUAL_UINT16(k_subtree_end[j], rd_u16(subtree_end + (2 * j)));
        TEST_ASSERT_EQUAL_HEX32(k_joint_id[j], rd_u32(joint_id + (4 * j)));
        const uint8_t *trs = rest + (40 * j);
        for (size_t c = 0; c < 3; c++) {
            TEST_ASSERT_EQUAL_HEX32(f32_bits(k_rest[j].t[c]), rd_u32(trs + (4 * c)));
        }
        for (size_t c = 0; c < 4; c++) {
            TEST_ASSERT_EQUAL_HEX32(f32_bits(k_rest[j].q[c]), rd_u32(trs + 12 + (4 * c)));
        }
        for (size_t c = 0; c < 3; c++) {
            TEST_ASSERT_EQUAL_HEX32(f32_bits(k_rest[j].s[c]), rd_u32(trs + 28 + (4 * c)));
        }
    }

    free(payload);
}
// #endregion

// #region NSKN
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_encode_skin_binding_wire_layout(void) {
    nt_skin_binding_t binding = fixture_binding();
    uint8_t *payload = NULL;
    uint32_t size = 0;
    nt_builder_encode_skin_binding(&binding, &payload, &size);
    TEST_ASSERT_NOT_NULL(payload);

    TEST_ASSERT_EQUAL_UINT32(16U + (50U * FIXTURE_PALETTE), size);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)NT_SKN_SIZE(FIXTURE_PALETTE), size);
    TEST_ASSERT_EQUAL_HEX32(NT_SKN_MAGIC, rd_u32(payload));
    TEST_ASSERT_EQUAL_UINT16(NT_SKELETAL_FORMAT_VERSION, rd_u16(payload + 4));
    TEST_ASSERT_EQUAL_UINT16(FIXTURE_PALETTE, rd_u16(payload + 6));
    TEST_ASSERT_EQUAL_HEX64(0xABCDEF0123456789ULL, rd_u64(payload + 8));

    /* Matrices come first, so the u16 remap can end the payload unpadded. */
    const uint8_t *inverse_bind = payload + 16;
    const uint8_t *remap = inverse_bind + (size_t)(48 * FIXTURE_PALETTE);
    for (size_t p = 0; p < FIXTURE_PALETTE; p++) {
        TEST_ASSERT_EQUAL_UINT16(k_remap[p], rd_u16(remap + (2 * p)));
        for (size_t row = 0; row < 3; row++) {
            for (size_t col = 0; col < 4; col++) {
                TEST_ASSERT_EQUAL_HEX32(f32_bits(k_inverse_bind[p].r[row][col]), rd_u32(inverse_bind + (48 * p) + (16 * row) + (4 * col)));
            }
        }
    }

    free(payload);
}
// #endregion

// #region NANM
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_encode_clip_header_counts(void) {
    nt_builder_clip_t clip;
    nt_builder_anim_channel_t channels[CLIP_CHANNELS];
    fixture_clip(&clip, channels);

    uint8_t *payload = NULL;
    uint32_t size = 0;
    nt_builder_encode_clip(&clip, &payload, &size);
    TEST_ASSERT_NOT_NULL(payload);
    TEST_ASSERT_EQUAL_UINT32(FIX_SIZE, size);

    NtAnmHeader header;
    memcpy(&header, payload, sizeof(header));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)nt_anm_size(&header), size);
    TEST_ASSERT_EQUAL_HEX32(NT_ANM_MAGIC, header.magic);
    TEST_ASSERT_EQUAL_UINT16(NT_SKELETAL_FORMAT_VERSION, header.version);
    TEST_ASSERT_EQUAL_UINT16(CLIP_JOINTS, header.joint_count);
    TEST_ASSERT_EQUAL_UINT32(CLIP_SAMPLES, header.sample_count);
    TEST_ASSERT_EQUAL_HEX32(f32_bits(1.0F), f32_bits(header.duration));
    TEST_ASSERT_EQUAL_HEX64(0xABCDEF0123456789ULL, header.rig_compat_id);
    TEST_ASSERT_EQUAL_HEX64(0ULL, header.additive_ref_id);

    TEST_ASSERT_EQUAL_UINT16(2, header.n_t);
    TEST_ASSERT_EQUAL_UINT16(1, header.n_q);
    TEST_ASSERT_EQUAL_UINT16(0, header.n_s);
    TEST_ASSERT_EQUAL_UINT16(0, header.n_ct);
    TEST_ASSERT_EQUAL_UINT16(1, header.n_cq);
    TEST_ASSERT_EQUAL_UINT16(1, header.n_cs);
    TEST_ASSERT_EQUAL_UINT32(1, header.n_steps);
    TEST_ASSERT_EQUAL_UINT32(5, header.n_keys);

    TEST_ASSERT_EQUAL_UINT8(NT_SKELETAL_CHANNEL_SAMPLED, header.object_mode[0]);
    TEST_ASSERT_EQUAL_UINT8(NT_SKELETAL_CHANNEL_STEP, header.object_mode[1]);
    TEST_ASSERT_EQUAL_UINT8(NT_SKELETAL_CHANNEL_ABSENT, header.object_mode[2]);
    TEST_ASSERT_EQUAL_UINT8(0, header._pad);
    for (size_t i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL_HEX32(0U, f32_bits(header.object_constant[i]));
    }
    /* The joint track owns keys [0, 3), the object rotation [3, 5). */
    TEST_ASSERT_EQUAL_UINT32(0, header.object_step_first[0]);
    TEST_ASSERT_EQUAL_UINT32(0, header.object_step_count[0]);
    TEST_ASSERT_EQUAL_UINT32(3, header.object_step_first[1]);
    TEST_ASSERT_EQUAL_UINT32(2, header.object_step_count[1]);

    free(payload);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_encode_clip_array_layout(void) {
    nt_builder_clip_t clip;
    nt_builder_anim_channel_t channels[CLIP_CHANNELS];
    fixture_clip(&clip, channels);

    uint8_t *payload = NULL;
    uint32_t size = 0;
    nt_builder_encode_clip(&clip, &payload, &size);
    TEST_ASSERT_NOT_NULL(payload);

    /* Blocks are sample-major: t row 0, t row 1, then the q row. */
    for (size_t i = 0; i < CLIP_SAMPLES; i++) {
        const uint8_t *block = payload + FIX_OFF_BLOCKS + (i * 10 * 4);
        for (size_t c = 0; c < 3; c++) {
            TEST_ASSERT_EQUAL_HEX32(f32_bits(k_t_row0[(i * 3) + c]), rd_u32(block + (4 * c)));
            TEST_ASSERT_EQUAL_HEX32(f32_bits(k_t_row1[(i * 3) + c]), rd_u32(block + 12 + (4 * c)));
        }
        for (size_t c = 0; c < 4; c++) {
            TEST_ASSERT_EQUAL_HEX32(f32_bits(k_q_row0[(i * 4) + c]), rd_u32(block + 24 + (4 * c)));
        }
    }

    for (size_t c = 0; c < 4; c++) {
        TEST_ASSERT_EQUAL_HEX32(f32_bits(k_const_q0[c]), rd_u32(payload + FIX_OFF_CQ + (4 * c)));
    }
    for (size_t c = 0; c < 3; c++) {
        TEST_ASSERT_EQUAL_HEX32(f32_bits(k_const_s1[c]), rd_u32(payload + FIX_OFF_CS + (4 * c)));
    }

    /* One step track: keys [0, 3) of joint 2's scale, with a zero pad byte. */
    TEST_ASSERT_EQUAL_UINT32(0, rd_u32(payload + FIX_OFF_STEPS));
    TEST_ASSERT_EQUAL_UINT32(3, rd_u32(payload + FIX_OFF_STEPS + 4));
    TEST_ASSERT_EQUAL_UINT16(2, rd_u16(payload + FIX_OFF_STEPS + 8));
    TEST_ASSERT_EQUAL_UINT8(2, payload[FIX_OFF_STEPS + 10]);
    TEST_ASSERT_EQUAL_UINT8(0, payload[FIX_OFF_STEPS + 11]);

    /* Keys: the joint track first, then the object rotation. */
    for (size_t k = 0; k < 3; k++) {
        const uint8_t *key = payload + FIX_OFF_KEYS + (20 * k);
        TEST_ASSERT_EQUAL_HEX32(f32_bits(k_step_times[k]), rd_u32(key));
        for (size_t c = 0; c < 4; c++) {
            TEST_ASSERT_EQUAL_HEX32(f32_bits(k_step_values[(4 * k) + c]), rd_u32(key + 4 + (4 * c)));
        }
    }
    for (size_t k = 0; k < 2; k++) {
        const uint8_t *key = payload + FIX_OFF_KEYS + (20 * (3 + k));
        TEST_ASSERT_EQUAL_HEX32(f32_bits(k_object_step_times[k]), rd_u32(key));
        for (size_t c = 0; c < 4; c++) {
            TEST_ASSERT_EQUAL_HEX32(f32_bits(k_object_step_values[(4 * k) + c]), rd_u32(key + 4 + (4 * c)));
        }
    }

    /* The object sampled array is one TRS per sample; the channels no object
     * mode drives stay at the zeroed payload. */
    for (size_t i = 0; i < CLIP_SAMPLES; i++) {
        const uint8_t *trs = payload + FIX_OFF_OBJECT + (i * 40);
        for (size_t c = 0; c < 3; c++) {
            TEST_ASSERT_EQUAL_HEX32(f32_bits(k_object_t[(i * 3) + c]), rd_u32(trs + (4 * c)));
        }
        for (size_t c = 0; c < 7; c++) {
            TEST_ASSERT_EQUAL_HEX32(0U, rd_u32(trs + 12 + (4 * c)));
        }
    }

    TEST_ASSERT_EQUAL_UINT16(0, rd_u16(payload + FIX_OFF_T_JOINT));
    TEST_ASSERT_EQUAL_UINT16(2, rd_u16(payload + FIX_OFF_T_JOINT + 2));
    TEST_ASSERT_EQUAL_UINT16(1, rd_u16(payload + FIX_OFF_Q_JOINT));
    TEST_ASSERT_EQUAL_UINT16(0, rd_u16(payload + FIX_OFF_CQ_JOINT));
    TEST_ASSERT_EQUAL_UINT16(1, rd_u16(payload + FIX_OFF_CS_JOINT));

    free(payload);
}

/* A clip whose only sampled signal is the object curve: no joint row, no frame
 * block, and the object array carries the whole grid. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_encode_clip_with_only_an_object_sampled_channel(void) {
    static const float k_object_only[3 * 3] = {1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F, 7.0F, 8.0F, 9.0F};
    nt_builder_anim_channel_t channels[6];
    memset(channels, 0, sizeof(channels));
    channels[3].mode = NT_SKELETAL_CHANNEL_SAMPLED;
    channels[3].samples = k_object_only;

    nt_builder_clip_t clip;
    memset(&clip, 0, sizeof(clip));
    clip.rig_compat_id = 7ULL;
    clip.joint_count = 1;
    clip.sample_count = 3;
    clip.duration = 1.0F;
    clip.channels = channels;

    uint8_t *payload = NULL;
    uint32_t size = 0;
    nt_builder_encode_clip(&clip, &payload, &size);
    TEST_ASSERT_NOT_NULL(payload);
    TEST_ASSERT_EQUAL_UINT32(120U + (3U * 40U), size);

    NtAnmHeader header;
    memcpy(&header, payload, sizeof(header));
    TEST_ASSERT_EQUAL_UINT16(0, header.n_t);
    TEST_ASSERT_EQUAL_UINT32(0, header.n_steps);
    TEST_ASSERT_EQUAL_UINT32(0, header.n_keys);
    TEST_ASSERT_EQUAL_UINT8(NT_SKELETAL_CHANNEL_SAMPLED, header.object_mode[0]);
    TEST_ASSERT_TRUE(nt_anm_object_sampled(&header));

    for (size_t i = 0; i < 3; i++) {
        const uint8_t *trs = payload + 120 + (i * 40);
        for (size_t c = 0; c < 3; c++) {
            TEST_ASSERT_EQUAL_HEX32(f32_bits(k_object_only[(i * 3) + c]), rd_u32(trs + (4 * c)));
        }
    }

    free(payload);
}

/* A clip made only of constant and step channels keeps its duration while the
 * grid holds a single sample and no block. */
void test_encode_clip_single_sample_has_no_blocks(void) {
    nt_builder_anim_channel_t channels[6];
    memset(channels, 0, sizeof(channels));
    channels[1].mode = NT_SKELETAL_CHANNEL_CONSTANT;
    channels[1].constant[3] = 1.0F;
    channels[3].mode = NT_SKELETAL_CHANNEL_STEP;
    channels[3].step_times = k_step_times;
    channels[3].step_values = k_step_values;
    channels[3].step_count = 2;

    nt_builder_clip_t clip;
    memset(&clip, 0, sizeof(clip));
    clip.rig_compat_id = 7ULL;
    clip.joint_count = 1;
    clip.sample_count = 1;
    clip.duration = 2.0F;
    clip.channels = channels;

    uint8_t *payload = NULL;
    uint32_t size = 0;
    nt_builder_encode_clip(&clip, &payload, &size);
    TEST_ASSERT_NOT_NULL(payload);

    /* 120 header + 16 cq + 2 x 20 keys + 2 cq_joint */
    TEST_ASSERT_EQUAL_UINT32(178U, size);
    NtAnmHeader header;
    memcpy(&header, payload, sizeof(header));
    TEST_ASSERT_EQUAL_UINT32(1U, header.sample_count);
    TEST_ASSERT_EQUAL_HEX32(f32_bits(2.0F), f32_bits(header.duration));
    TEST_ASSERT_EQUAL_UINT16(0, header.n_t);
    TEST_ASSERT_EQUAL_UINT16(0, header.n_q);
    TEST_ASSERT_EQUAL_UINT16(0, header.n_s);
    TEST_ASSERT_EQUAL_UINT32(2, header.n_keys);
    TEST_ASSERT_EQUAL_UINT32(0, header.object_step_first[0]);

    free(payload);
}
// #endregion

// #region pack entry types
static uint8_t *read_file_bytes(const char *path, uint32_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    (void)fseek(f, 0, SEEK_END);
    long len = ftell(f);
    (void)fseek(f, 0, SEEK_SET);
    if (len <= 0) {
        (void)fclose(f);
        return NULL;
    }
    uint8_t *buf = (uint8_t *)malloc((size_t)len);
    if (!buf) {
        (void)fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)len, f);
    (void)fclose(f);
    if (got != (size_t)len) {
        free(buf);
        return NULL;
    }
    *out_size = (uint32_t)len;
    return buf;
}

/* add_* registers each payload under its own asset type, and the pack stores
 * the encoder's bytes unchanged. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_add_skeletal_assets_writes_typed_entries(void) {
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);
    (void)remove(PACK_PATH);

    NtBuilderContext *ctx = nt_builder_start_pack(PACK_PATH);
    TEST_ASSERT_NOT_NULL(ctx);

    nt_skeletal_skeleton_t skel = fixture_skeleton();
    nt_skin_binding_t binding = fixture_binding();
    nt_builder_clip_t clip;
    nt_builder_anim_channel_t channels[CLIP_CHANNELS];
    fixture_clip(&clip, channels);

    const nt_hash64_t rig = nt_builder_add_skeleton(ctx, &skel, "rigs/hero.nskl");
    TEST_ASSERT_EQUAL_HEX64(fixture_rig_id(), rig.value);
    nt_builder_add_skin_binding(ctx, &binding, "rigs/hero.nskn");
    nt_builder_add_clip(ctx, &clip, "clips/hero_run.nanm");

    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    nt_builder_free_pack(ctx);

    uint32_t file_size = 0;
    uint8_t *buf = read_file_bytes(PACK_PATH, &file_size);
    TEST_ASSERT_NOT_NULL(buf);

    const NtPackHeader *pack = (const NtPackHeader *)buf;
    TEST_ASSERT_EQUAL_HEX32(NT_PACK_MAGIC, pack->magic);
    TEST_ASSERT_EQUAL_UINT16(3, pack->asset_count);

    const NtAssetEntry *entries = (const NtAssetEntry *)(buf + sizeof(NtPackHeader));
    const uint8_t k_expect_type[3] = {NT_ASSET_SKELETON, NT_ASSET_SKIN_BINDING, NT_ASSET_CLIP};
    const uint32_t k_expect_magic[3] = {NT_SKL_MAGIC, NT_SKN_MAGIC, NT_ANM_MAGIC};
    for (uint32_t t = 0; t < 3; t++) {
        const NtAssetEntry *found = NULL;
        for (uint32_t i = 0; i < pack->asset_count; i++) {
            if (entries[i].asset_type == k_expect_type[t]) {
                found = &entries[i];
                break;
            }
        }
        TEST_ASSERT_NOT_NULL(found);
        TEST_ASSERT_TRUE(found->offset + found->size <= file_size);
        TEST_ASSERT_EQUAL_HEX32(k_expect_magic[t], rd_u32(buf + found->offset));
    }

    /* The clip entry is byte-identical to what the encoder produced. */
    uint8_t *expected = NULL;
    uint32_t expected_size = 0;
    nt_builder_encode_clip(&clip, &expected, &expected_size);
    const NtAssetEntry *clip_entry = NULL;
    for (uint32_t i = 0; i < pack->asset_count; i++) {
        if (entries[i].asset_type == NT_ASSET_CLIP) {
            clip_entry = &entries[i];
            break;
        }
    }
    TEST_ASSERT_NOT_NULL(clip_entry);
    TEST_ASSERT_EQUAL_UINT32(expected_size, clip_entry->size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, buf + clip_entry->offset, expected_size);

    free(expected);
    free(buf);

    /* dump walks the three new per-type detail printers over this pack. */
    TEST_ASSERT_EQUAL(NT_BUILD_OK, nt_builder_dump_pack(PACK_PATH));
    (void)remove(PACK_PATH);
}
// #endregion

// #region content rules are asserts
/* The importer is the only producer of this data, so every wire rule it can
 * break is an NT_BUILD_ASSERT and not a return code. Each case violates exactly
 * one rule and the trap checks that this is the rule that fired. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_encode_skeleton_asserts_on_a_broken_hierarchy(void) {
    uint16_t parent[FIXTURE_JOINTS];
    memcpy(parent, k_parent, sizeof(parent));
    nt_skeletal_skeleton_t skel = fixture_skeleton();
    skel.parent = parent;

    uint8_t *payload = NULL;
    uint32_t size = 0;

    /* Forward parent: joint 2 names a joint that does not exist yet. */
    parent[2] = 3;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_encode_skeleton(&skel, &payload, &size), "innermost joint whose subtree range is still open");

    /* Sibling over-claim: joint 1 is still open at joint 2, so joint 2 may not
     * reach past it to joint 0. Range nesting alone accepts this. */
    parent[2] = 0;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_encode_skeleton(&skel, &payload, &size), "innermost joint whose subtree range is still open");

    memcpy(parent, k_parent, sizeof(parent));
    uint16_t subtree_end[FIXTURE_JOINTS];
    memcpy(subtree_end, k_subtree_end, sizeof(subtree_end));
    skel.subtree_end = subtree_end;
    subtree_end[2] = 4; /* leaves the range of parent 1, which ends at 3 */
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_encode_skeleton(&skel, &payload, &size), "child lies outside its parent's subtree range");

    TEST_ASSERT_NULL(payload);
}

/* Joint ids are how clips name joints, so a duplicate makes a rig that cannot
 * be addressed. */
void test_encode_skeleton_asserts_on_a_duplicate_joint_id(void) {
    uint32_t joint_id[FIXTURE_JOINTS];
    memcpy(joint_id, k_joint_id, sizeof(joint_id));
    joint_id[3] = joint_id[1];

    nt_skeletal_skeleton_t skel = fixture_skeleton();
    skel.joint_id = joint_id;

    uint8_t *payload = NULL;
    uint32_t size = 0;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_encode_skeleton(&skel, &payload, &size), "two joints share one joint_id");
    TEST_ASSERT_NULL(payload);
}

void test_encode_skin_binding_asserts_on_a_non_finite_matrix(void) {
    nt_skeletal_mat34_t inverse_bind[FIXTURE_PALETTE];
    memcpy(inverse_bind, k_inverse_bind, sizeof(inverse_bind));
    inverse_bind[1].r[2][3] = (float)(1e300 * 1e300);

    nt_skin_binding_t binding = fixture_binding();
    binding.inverse_bind = inverse_bind;

    uint8_t *payload = NULL;
    uint32_t size = 0;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_encode_skin_binding(&binding, &payload, &size), "inverse bind matrix is not finite");
    TEST_ASSERT_NULL(payload);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_encode_clip_asserts_on_broken_channels(void) {
    nt_builder_clip_t clip;
    nt_builder_anim_channel_t channels[CLIP_CHANNELS];
    uint8_t *payload = NULL;
    uint32_t size = 0;

    static const float k_non_unit_q[CLIP_SAMPLES * 4] = {
        0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.5F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F,
    };
    fixture_clip(&clip, channels);
    channels[4].samples = k_non_unit_q;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_encode_clip(&clip, &payload, &size), "sampled rotation is not a unit quaternion");

    /* One sample cannot carry a sampled channel, whatever the duration says. */
    fixture_clip(&clip, channels);
    clip.sample_count = 1;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_encode_clip(&clip, &payload, &size), "a sampled channel needs at least two samples over a positive duration");

    static const float k_late_first_key[3] = {0.1F, 0.4F, 0.8F};
    fixture_clip(&clip, channels);
    channels[8].step_times = k_late_first_key;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_encode_clip(&clip, &payload, &size), "the first step key must sit at time 0");

    static const float k_flat_step_times[3] = {0.0F, 0.4F, 0.4F};
    fixture_clip(&clip, channels);
    channels[8].step_times = k_flat_step_times;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_encode_clip(&clip, &payload, &size), "step times must increase strictly");

    static const float k_late_last_key[3] = {0.0F, 0.4F, 1.5F};
    fixture_clip(&clip, channels);
    channels[8].step_times = k_late_last_key;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_encode_clip(&clip, &payload, &size), "the last step key lies past the clip duration");

    TEST_ASSERT_NULL(payload);
}
// #endregion

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_encode_skeleton_wire_layout);
    RUN_TEST(test_encode_skin_binding_wire_layout);
    RUN_TEST(test_encode_clip_header_counts);
    RUN_TEST(test_encode_clip_array_layout);
    RUN_TEST(test_encode_clip_with_only_an_object_sampled_channel);
    RUN_TEST(test_encode_clip_single_sample_has_no_blocks);
    RUN_TEST(test_add_skeletal_assets_writes_typed_entries);
    RUN_TEST(test_encode_skeleton_asserts_on_a_broken_hierarchy);
    RUN_TEST(test_encode_skeleton_asserts_on_a_duplicate_joint_id);
    RUN_TEST(test_encode_skin_binding_asserts_on_a_non_finite_matrix);
    RUN_TEST(test_encode_clip_asserts_on_broken_channels);
    return UNITY_END();
}
