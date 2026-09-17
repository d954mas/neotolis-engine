/* Wire-format coverage for the NSKL / NSKN / NANM encoders: every field is read
 * back from the payload bytes as little-endian, never through the header structs. */

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

/* The encoder asserts that the id matches these joints, so the fixture takes it
 * from the one implementation of the schema instead of inventing a number. */
static nt_skeletal_skeleton_t fixture_skeleton(void) {
    nt_skeletal_skeleton_t skel = {0};
    skel.parent = k_parent;
    skel.subtree_end = k_subtree_end;
    skel.joint_id = k_joint_id;
    skel.rest = k_rest;
    skel.joint_count = FIXTURE_JOINTS;
    uint8_t scratch[NT_SKELETAL_RIG_ID_BYTES(FIXTURE_JOINTS)];
    skel.rig_compat_id = nt_skeletal_rig_compat_id(&skel, scratch, (uint32_t)sizeof(scratch));
    return skel;
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
    binding.reach = 1.75F;
    binding.any_pose_radius = 4.5F;
    binding.palette_count = FIXTURE_PALETTE;
    return binding;
}

/* Asymmetric clip: three joints with a different mode per component, one STEP
 * track and an object curve whose translation is sampled. */
#define CLIP_JOINTS 3
#define CLIP_SAMPLES 5
#define CLIP_CHANNELS (3 * (CLIP_JOINTS + 1))

static const float k_q_samples[CLIP_SAMPLES * 4] = {
    0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.38268343F, 0.92387953F, 0.0F, 0.0F, 0.70710678F, 0.70710678F, 0.0F, 0.0F, 0.92387953F, 0.38268343F, 0.0F, 0.0F, 1.0F, 0.0F,
};
static const float k_t_samples[CLIP_SAMPLES * 3] = {0.0F, 0.0F, 0.0F, 1.0F, 0.5F, 0.0F, 2.0F, 1.0F, 0.0F, 3.0F, 1.5F, 0.0F, 4.0F, 2.0F, 0.0F};
static const float k_s_samples[CLIP_SAMPLES * 3] = {1.0F, 1.0F, 1.0F, 1.1F, 1.0F, 0.9F, 1.2F, 1.0F, 0.8F, 1.3F, 1.0F, 0.7F, 1.4F, 1.0F, 0.6F};
static const float k_object_t_samples[CLIP_SAMPLES * 3] = {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.25F, 0.0F, 0.0F, 0.5F, 0.0F, 0.0F, 0.75F, 0.0F, 0.0F, 1.0F};

static const float k_step_times[3] = {0.0F, 0.4F, 0.8F};
static const float k_step_values[3 * 4] = {0.0F, 0.0F, 0.0F, 0.0F, 5.0F, 0.0F, 0.0F, 0.0F, 5.0F, 5.0F, 0.0F, 0.0F};

/* channels[c]: joint c/3, component c%3 (0 = t, 1 = q, 2 = s). */
static void fixture_clip(nt_builder_clip_t *clip, nt_builder_anim_channel_t channels[CLIP_CHANNELS]) {
    memset(channels, 0, sizeof(nt_builder_anim_channel_t) * (size_t)CLIP_CHANNELS);

    channels[0].mode = NT_ANM_CHANNEL_ABSENT;
    channels[1].mode = NT_ANM_CHANNEL_SAMPLED;
    channels[1].samples = k_q_samples;
    channels[2].mode = NT_ANM_CHANNEL_CONSTANT;
    channels[2].constant[0] = 2.0F;
    channels[2].constant[1] = 2.0F;
    channels[2].constant[2] = 2.0F;

    channels[3].mode = NT_ANM_CHANNEL_SAMPLED;
    channels[3].samples = k_t_samples;
    channels[4].mode = NT_ANM_CHANNEL_CONSTANT;
    channels[4].constant[3] = 1.0F; /* identity rotation */
    channels[5].mode = NT_ANM_CHANNEL_ABSENT;

    channels[6].mode = NT_ANM_CHANNEL_STEP;
    channels[6].step_times = k_step_times;
    channels[6].step_values = k_step_values;
    channels[6].step_count = 3;
    channels[7].mode = NT_ANM_CHANNEL_ABSENT;
    channels[8].mode = NT_ANM_CHANNEL_SAMPLED;
    channels[8].samples = k_s_samples;

    channels[9].mode = NT_ANM_CHANNEL_SAMPLED;
    channels[9].samples = k_object_t_samples;
    channels[10].mode = NT_ANM_CHANNEL_ABSENT;
    channels[11].mode = NT_ANM_CHANNEL_ABSENT;

    memset(clip, 0, sizeof(*clip));
    clip->rig_compat_id = 0xABCDEF0123456789ULL;
    clip->additive_ref_id = 0;
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
// #endregion

// #region NSKL
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_encode_skeleton_wire_layout(void) {
    nt_skeletal_skeleton_t skel = fixture_skeleton();
    uint8_t *payload = NULL;
    uint32_t size = 0;
    nt_builder_encode_skeleton(&skel, &payload, &size);
    TEST_ASSERT_NOT_NULL(payload);

    TEST_ASSERT_EQUAL_UINT32(16U + (48U * FIXTURE_JOINTS), size);
    TEST_ASSERT_EQUAL_UINT32(NT_SKL_SIZE(FIXTURE_JOINTS), size);
    TEST_ASSERT_EQUAL_HEX32(NT_SKL_MAGIC, rd_u32(payload));
    TEST_ASSERT_EQUAL_UINT16(NT_SKELETAL_FORMAT_VERSION, rd_u16(payload + 4));
    TEST_ASSERT_EQUAL_UINT16(FIXTURE_JOINTS, rd_u16(payload + 6));
    TEST_ASSERT_EQUAL_HEX64(skel.rig_compat_id.value, rd_u64(payload + 8));

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

    TEST_ASSERT_EQUAL_UINT32(28U + (50U * FIXTURE_PALETTE), size);
    TEST_ASSERT_EQUAL_UINT32(NT_SKN_SIZE(FIXTURE_PALETTE), size);
    TEST_ASSERT_EQUAL_HEX32(NT_SKN_MAGIC, rd_u32(payload));
    TEST_ASSERT_EQUAL_UINT16(NT_SKELETAL_FORMAT_VERSION, rd_u16(payload + 4));
    TEST_ASSERT_EQUAL_UINT16(FIXTURE_PALETTE, rd_u16(payload + 6));
    TEST_ASSERT_EQUAL_HEX64(0xABCDEF0123456789ULL, rd_u64(payload + 8));
    TEST_ASSERT_EQUAL_UINT8(NT_SKN_MESH_SPACE_GLTF_NODE, payload[16]);
    TEST_ASSERT_EQUAL_UINT8(0, payload[17]);
    TEST_ASSERT_EQUAL_UINT8(0, payload[18]);
    TEST_ASSERT_EQUAL_UINT8(0, payload[19]);
    TEST_ASSERT_EQUAL_HEX32(f32_bits(1.75F), rd_u32(payload + 20));
    TEST_ASSERT_EQUAL_HEX32(f32_bits(4.5F), rd_u32(payload + 24));

    const uint8_t *remap = payload + 28;
    const uint8_t *inverse_bind = remap + (size_t)(2 * FIXTURE_PALETTE);
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
/* Section descriptor s of the payload. */
static const uint8_t *section_at(const uint8_t *payload, size_t s) { return payload + sizeof(NtAnmHeader) + (s * sizeof(NtAnmSection)); }

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_encode_clip_asymmetric_sections(void) {
    nt_builder_clip_t clip;
    nt_builder_anim_channel_t channels[CLIP_CHANNELS];
    fixture_clip(&clip, channels);

    uint8_t *payload = NULL;
    uint32_t size = 0;
    nt_builder_encode_clip(&clip, &payload, &size);
    TEST_ASSERT_NOT_NULL(payload);

    /* 56 header + 7*16 table + 12*4 CHAN + 2*60 PLNT + 1*80 PLNQ + 1*60 PLNS
     * + 2*16 CNST + 1*8 STPT + 3*20 STPK */
    TEST_ASSERT_EQUAL_UINT32(576U, size);

    TEST_ASSERT_EQUAL_HEX32(NT_ANM_MAGIC, rd_u32(payload));
    TEST_ASSERT_EQUAL_UINT16(NT_SKELETAL_FORMAT_VERSION, rd_u16(payload + 4));
    TEST_ASSERT_EQUAL_UINT16(NT_ANM_SECTION_COUNT, rd_u16(payload + 6));
    TEST_ASSERT_EQUAL_UINT16(CLIP_JOINTS, rd_u16(payload + 8));
    TEST_ASSERT_EQUAL_UINT8(NT_ANM_KIND_ABSOLUTE, payload[10]);
    TEST_ASSERT_EQUAL_UINT8(NT_ANM_CODEC_F32, payload[11]);
    TEST_ASSERT_EQUAL_HEX64(0xABCDEF0123456789ULL, rd_u64(payload + 12));
    TEST_ASSERT_EQUAL_HEX64(0ULL, rd_u64(payload + 20));
    TEST_ASSERT_EQUAL_HEX32(f32_bits(1.0F), rd_u32(payload + 28));
    TEST_ASSERT_EQUAL_UINT32(CLIP_SAMPLES, rd_u32(payload + 32));
    TEST_ASSERT_EQUAL_HEX32(f32_bits(3.5F), rd_u32(payload + 36));
    TEST_ASSERT_EQUAL_HEX32(f32_bits(4.0F), rd_u32(payload + 40));
    TEST_ASSERT_EQUAL_HEX32(f32_bits(2.5F), rd_u32(payload + 44));
    TEST_ASSERT_EQUAL_HEX32(f32_bits(30.0F), rd_u32(payload + 48));
    TEST_ASSERT_EQUAL_HEX32(f32_bits(1.25F), rd_u32(payload + 52));

    const uint32_t tags[NT_ANM_SECTION_COUNT] = {NT_ANM_TAG_CHAN, NT_ANM_TAG_PLNT, NT_ANM_TAG_PLNQ, NT_ANM_TAG_PLNS, NT_ANM_TAG_CNST, NT_ANM_TAG_STPT, NT_ANM_TAG_STPK};
    const uint32_t counts[NT_ANM_SECTION_COUNT] = {CLIP_CHANNELS, 2, 1, 1, 2, 1, 3};
    const uint32_t strides[NT_ANM_SECTION_COUNT] = {4, CLIP_SAMPLES * 12, CLIP_SAMPLES * 16, CLIP_SAMPLES * 12, 16, 8, 20};
    uint32_t expect_offset = (uint32_t)sizeof(NtAnmHeader) + (NT_ANM_SECTION_COUNT * (uint32_t)sizeof(NtAnmSection));
    uint32_t section_offset[NT_ANM_SECTION_COUNT];
    for (size_t s = 0; s < NT_ANM_SECTION_COUNT; s++) {
        const uint8_t *sec = section_at(payload, s);
        TEST_ASSERT_EQUAL_HEX32(tags[s], rd_u32(sec));
        TEST_ASSERT_EQUAL_UINT32(expect_offset, rd_u32(sec + 4));
        TEST_ASSERT_EQUAL_UINT32(counts[s], rd_u32(sec + 8));
        TEST_ASSERT_EQUAL_UINT32(strides[s], rd_u32(sec + 12));
        TEST_ASSERT_EQUAL_UINT32(0, rd_u32(sec + 4) % 4U);
        section_offset[s] = expect_offset;
        expect_offset += counts[s] * strides[s];
    }
    TEST_ASSERT_EQUAL_UINT32(size, expect_offset);

    /* CHAN: mode per channel, index counting within its own table. */
    const uint8_t k_expect_mode[CLIP_CHANNELS] = {
        NT_ANM_CHANNEL_ABSENT, NT_ANM_CHANNEL_SAMPLED, NT_ANM_CHANNEL_CONSTANT, NT_ANM_CHANNEL_SAMPLED, NT_ANM_CHANNEL_CONSTANT, NT_ANM_CHANNEL_ABSENT,
        NT_ANM_CHANNEL_STEP,   NT_ANM_CHANNEL_ABSENT,  NT_ANM_CHANNEL_SAMPLED,  NT_ANM_CHANNEL_SAMPLED, NT_ANM_CHANNEL_ABSENT,   NT_ANM_CHANNEL_ABSENT,
    };
    const uint16_t k_expect_index[CLIP_CHANNELS] = {0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0};
    const uint8_t *chan = payload + section_offset[0];
    for (size_t c = 0; c < (size_t)CLIP_CHANNELS; c++) {
        TEST_ASSERT_EQUAL_UINT8(k_expect_mode[c], chan[4 * c]);
        TEST_ASSERT_EQUAL_UINT8(0, chan[(4 * c) + 1]);
        TEST_ASSERT_EQUAL_UINT16(k_expect_index[c], rd_u16(chan + (4 * c) + 2));
    }

    /* PLNT rows follow channel order: joint 1 translation, then the object curve. */
    const uint8_t *plnt = payload + section_offset[1];
    for (size_t i = 0; i < (size_t)(CLIP_SAMPLES * 3); i++) {
        TEST_ASSERT_EQUAL_HEX32(f32_bits(k_t_samples[i]), rd_u32(plnt + (4 * i)));
        TEST_ASSERT_EQUAL_HEX32(f32_bits(k_object_t_samples[i]), rd_u32(plnt + strides[1] + (4 * i)));
    }
    const uint8_t *plnq = payload + section_offset[2];
    for (size_t i = 0; i < (size_t)(CLIP_SAMPLES * 4); i++) {
        TEST_ASSERT_EQUAL_HEX32(f32_bits(k_q_samples[i]), rd_u32(plnq + (4 * i)));
    }
    const uint8_t *plns = payload + section_offset[3];
    for (size_t i = 0; i < (size_t)(CLIP_SAMPLES * 3); i++) {
        TEST_ASSERT_EQUAL_HEX32(f32_bits(k_s_samples[i]), rd_u32(plns + (4 * i)));
    }

    /* CNST holds joint 0 scale then joint 1 rotation, four floats each. */
    const uint8_t *cnst = payload + section_offset[4];
    const float k_expect_const[8] = {2.0F, 2.0F, 2.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
    for (size_t i = 0; i < 8; i++) {
        TEST_ASSERT_EQUAL_HEX32(f32_bits(k_expect_const[i]), rd_u32(cnst + (4 * i)));
    }

    const uint8_t *stpt = payload + section_offset[5];
    TEST_ASSERT_EQUAL_UINT32(0, rd_u32(stpt));
    TEST_ASSERT_EQUAL_UINT32(3, rd_u32(stpt + 4));

    const uint8_t *stpk = payload + section_offset[6];
    for (size_t k = 0; k < 3; k++) {
        TEST_ASSERT_EQUAL_HEX32(f32_bits(k_step_times[k]), rd_u32(stpk + (20 * k)));
        for (size_t i = 0; i < 4; i++) {
            TEST_ASSERT_EQUAL_HEX32(f32_bits(k_step_values[(4 * k) + i]), rd_u32(stpk + (20 * k) + 4 + (4 * i)));
        }
    }

    free(payload);
}

/* A clip made only of constant and step channels keeps its duration while every
 * plane stays empty at sample_count 1. */
void test_encode_clip_single_sample_has_empty_planes(void) {
    nt_builder_anim_channel_t channels[6];
    memset(channels, 0, sizeof(channels));
    channels[1].mode = NT_ANM_CHANNEL_CONSTANT;
    channels[1].constant[3] = 1.0F;
    channels[3].mode = NT_ANM_CHANNEL_STEP;
    channels[3].step_times = k_step_times;
    channels[3].step_values = k_step_values;
    channels[3].step_count = 2;

    nt_builder_clip_t clip;
    memset(&clip, 0, sizeof(clip));
    clip.rig_compat_id = 7ULL;
    clip.kind = NT_ANM_KIND_ABSOLUTE;
    clip.joint_count = 1;
    clip.sample_count = 1;
    clip.duration = 2.0F;
    clip.channels = channels;

    uint8_t *payload = NULL;
    uint32_t size = 0;
    nt_builder_encode_clip(&clip, &payload, &size);
    TEST_ASSERT_NOT_NULL(payload);

    /* 56 + 112 + 6*4 CHAN + 0 planes + 16 CNST + 8 STPT + 2*20 STPK */
    TEST_ASSERT_EQUAL_UINT32(256U, size);
    TEST_ASSERT_EQUAL_UINT32(1U, rd_u32(payload + 32));
    TEST_ASSERT_EQUAL_HEX32(f32_bits(2.0F), rd_u32(payload + 28));
    for (size_t s = 1; s <= 3; s++) {
        TEST_ASSERT_EQUAL_UINT32(0, rd_u32(section_at(payload, s) + 8));
    }

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

    nt_builder_add_skeleton(ctx, &skel, "rigs/hero.nskl");
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

void test_encode_skeleton_asserts_when_the_rig_id_is_not_its_own(void) {
    nt_skeletal_skeleton_t skel = fixture_skeleton();
    skel.rig_compat_id.value ^= 1ULL;

    uint8_t *payload = NULL;
    uint32_t size = 0;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_encode_skeleton(&skel, &payload, &size), "rig_compat_id does not match its own joints");
    TEST_ASSERT_NULL(payload);
}

void test_encode_skin_binding_asserts_on_a_negative_reach(void) {
    nt_skin_binding_t binding = fixture_binding();
    uint8_t *payload = NULL;
    uint32_t size = 0;

    binding.reach = -1.0F;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_encode_skin_binding(&binding, &payload, &size), "reach must be finite and non-negative");

    binding.reach = 1.75F;
    binding.any_pose_radius = -1.0F;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_encode_skin_binding(&binding, &payload, &size), "any_pose_radius must be finite and non-negative");
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
    channels[1].samples = k_non_unit_q;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_encode_clip(&clip, &payload, &size), "sampled rotation is not a unit quaternion");

    /* One sample cannot carry a sampled channel, whatever the duration says. */
    fixture_clip(&clip, channels);
    clip.sample_count = 1;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_encode_clip(&clip, &payload, &size), "a sampled channel needs at least two samples over a positive duration");

    fixture_clip(&clip, channels);
    clip.kind = NT_ANM_KIND_ADDITIVE;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_encode_clip(&clip, &payload, &size), "additive_ref_id is set exactly for an additive clip");

    static const float k_late_first_key[3] = {0.1F, 0.4F, 0.8F};
    fixture_clip(&clip, channels);
    channels[6].step_times = k_late_first_key;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_encode_clip(&clip, &payload, &size), "the first step key must sit at time 0");

    static const float k_flat_step_times[3] = {0.0F, 0.4F, 0.4F};
    fixture_clip(&clip, channels);
    channels[6].step_times = k_flat_step_times;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_encode_clip(&clip, &payload, &size), "step times must increase strictly");

    TEST_ASSERT_NULL(payload);
}
// #endregion

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_encode_skeleton_wire_layout);
    RUN_TEST(test_encode_skin_binding_wire_layout);
    RUN_TEST(test_encode_clip_asymmetric_sections);
    RUN_TEST(test_encode_clip_single_sample_has_empty_planes);
    RUN_TEST(test_add_skeletal_assets_writes_typed_entries);
    RUN_TEST(test_encode_skeleton_asserts_on_a_broken_hierarchy);
    RUN_TEST(test_encode_skeleton_asserts_when_the_rig_id_is_not_its_own);
    RUN_TEST(test_encode_skin_binding_asserts_on_a_negative_reach);
    RUN_TEST(test_encode_clip_asserts_on_broken_channels);
    return UNITY_END();
}
