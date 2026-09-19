/* Wire-format coverage for the NSKL / NSKN / NANM encoders: the payload bytes
 * are read back at hand-computed offsets, so a moved field or a reordered array
 * fails here and not only in the round-trip suite. */

/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
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
#include "nt_builder_internal.h"
#include "nt_pack_format.h"
#include "nt_skeletal_format.h"
#include "test_helpers/build_assert_trap.h"
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
void tearDown(void) { nt_build_assert_handler = NULL; }

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

static float f32_from_bits(uint32_t bits) {
    float v = 0.0F;
    memcpy(&v, &bits, sizeof(v));
    return v;
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
/* Two different exact values, so a swapped pair of header floats fails here. */
#define FIXTURE_REACH 1.25F
#define FIXTURE_ANY_POSE_RADIUS 3.5F

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
    binding.reach = FIXTURE_REACH;
    binding.any_pose_radius = FIXTURE_ANY_POSE_RADIUS;
    binding.palette_count = FIXTURE_PALETTE;
    return binding;
}

/*
 * Asymmetric clip: two sampled translation rows against one sampled rotation
 * row and one sampled scale row, plus base-pose constants of two component
 * kinds -- so the block layout and the per-kind tables all carry more than
 * one element and cannot pass by coincidence.
 *
 *   joint 0: t sampled (t row 0), q base constant, s rest
 *   joint 1: t rest, q sampled (q row 0), s base constant
 *   joint 2: t sampled (t row 1), q rest, s sampled (s row 0)
 */
#define CLIP_JOINTS 3
#define CLIP_SAMPLES 5
#define CLIP_STRIDE 13

static const float k_t_row0[CLIP_SAMPLES * 3] = {0.0F, 0.0F, 0.0F, 1.0F, 0.5F, 0.0F, 2.0F, 1.0F, 0.0F, 3.0F, 1.5F, 0.0F, 4.0F, 2.0F, 0.0F};
static const float k_t_row1[CLIP_SAMPLES * 3] = {10.0F, -1.0F, 0.5F, 10.5F, -2.0F, 1.5F, 11.0F, -3.0F, 2.5F, 11.5F, -4.0F, 3.5F, 12.0F, -5.0F, 4.5F};
static const float k_q_row0[CLIP_SAMPLES * 4] = {
    0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.38268343F, 0.92387953F, 0.0F, 0.0F, 0.70710678F, 0.70710678F, 0.0F, 0.0F, 0.92387953F, 0.38268343F, 0.0F, 0.0F, 1.0F, 0.0F,
};
static const float k_s_row0[CLIP_SAMPLES * 3] = {1.0F, 1.0F, 1.0F, 2.0F, 0.5F, 3.0F, 0.25F, 4.0F, 0.5F, 1.5F, 1.5F, 1.5F, 2.0F, 2.0F, 2.0F};

/* The fixture rest with the two constants written in. */
static const nt_skeletal_trs_t k_base[CLIP_JOINTS] = {
    {{1.0F, 2.0F, 3.0F}, {0.0F, 0.0F, 0.70710678F, 0.70710678F}, {1.0F, 1.0F, 1.0F}},
    {{0.0F, 0.5F, -0.25F}, {0.0F, 0.0F, -0.70710678F, 0.70710678F}, {2.0F, 3.0F, 4.0F}},
    {{-1.5F, 0.0F, 0.125F}, {0.70710678F, 0.0F, 0.0F, 0.70710678F}, {1.0F, 1.0F, 1.0F}},
};

static const uint16_t k_t_joint[2] = {0, 2};
static const uint16_t k_q_joint[1] = {1};
static const uint16_t k_s_joint[1] = {2};
static float g_blocks[CLIP_SAMPLES * CLIP_STRIDE];

static void fixture_clip(nt_skeletal_clip_t *clip) {
    /* Sample-major: t row 0, t row 1, the q row, then the s row per block. */
    for (size_t i = 0; i < CLIP_SAMPLES; i++) {
        float *block = g_blocks + (i * CLIP_STRIDE);
        memcpy(block, &k_t_row0[i * 3], 3 * sizeof(float));
        memcpy(block + 3, &k_t_row1[i * 3], 3 * sizeof(float));
        memcpy(block + 6, &k_q_row0[i * 4], 4 * sizeof(float));
        memcpy(block + 10, &k_s_row0[i * 3], 3 * sizeof(float));
    }
    memset(clip, 0, sizeof(*clip));
    clip->rig_compat_id = (nt_hash64_t){0xABCDEF0123456789ULL};
    clip->duration = 1.0;
    clip->base = k_base;
    clip->blocks = g_blocks;
    clip->t_joint = k_t_joint;
    clip->q_joint = k_q_joint;
    clip->s_joint = k_s_joint;
    clip->sample_count = CLIP_SAMPLES;
    clip->joint_count = CLIP_JOINTS;
    clip->n_t = 2;
    clip->n_q = 1;
    clip->n_s = 1;
}

/* Hand-computed offsets of the fixture clip: header 44, three base entries,
 * blocks 5 x 13 floats, then the three joint tables. */
enum {
    FIX_OFF_BASE = 44,
    FIX_OFF_BLOCKS = FIX_OFF_BASE + (CLIP_JOINTS * 40),
    FIX_OFF_T_JOINT = FIX_OFF_BLOCKS + (CLIP_SAMPLES * CLIP_STRIDE * 4),
    FIX_OFF_Q_JOINT = FIX_OFF_T_JOINT + 4,
    FIX_OFF_S_JOINT = FIX_OFF_Q_JOINT + 2,
    FIX_SIZE = FIX_OFF_S_JOINT + 2,
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

    TEST_ASSERT_EQUAL_UINT32(24U + (50U * FIXTURE_PALETTE), size);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)NT_SKN_SIZE(FIXTURE_PALETTE), size);
    TEST_ASSERT_EQUAL_HEX32(NT_SKN_MAGIC, rd_u32(payload));
    TEST_ASSERT_EQUAL_UINT16(NT_SKELETAL_FORMAT_VERSION, rd_u16(payload + 4));
    TEST_ASSERT_EQUAL_UINT16(FIXTURE_PALETTE, rd_u16(payload + 6));
    TEST_ASSERT_EQUAL_HEX64(0xABCDEF0123456789ULL, rd_u64(payload + 8));
    TEST_ASSERT_EQUAL_HEX32(f32_bits(FIXTURE_REACH), rd_u32(payload + 16));
    TEST_ASSERT_EQUAL_HEX32(f32_bits(FIXTURE_ANY_POSE_RADIUS), rd_u32(payload + 20));

    /* Matrices come first, so the u16 remap can end the payload unpadded. */
    const uint8_t *inverse_bind = payload + 24;
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
void test_encode_clip_header_counts(void) {
    nt_skeletal_clip_t clip;
    fixture_clip(&clip);

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
    TEST_ASSERT_EQUAL_HEX32(0U, f32_bits(header.r_joints));
    TEST_ASSERT_EQUAL_HEX32(0U, f32_bits(header.r_root));
    TEST_ASSERT_EQUAL_HEX32(0U, f32_bits(header.s_max));

    TEST_ASSERT_EQUAL_UINT16(2, header.n_t);
    TEST_ASSERT_EQUAL_UINT16(1, header.n_q);
    TEST_ASSERT_EQUAL_UINT16(1, header.n_s);
    TEST_ASSERT_EQUAL_UINT16(0, header._pad);

    free(payload);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_encode_clip_array_layout(void) {
    nt_skeletal_clip_t clip;
    fixture_clip(&clip);

    uint8_t *payload = NULL;
    uint32_t size = 0;
    nt_builder_encode_clip(&clip, &payload, &size);
    TEST_ASSERT_NOT_NULL(payload);

    /* The base pose comes first, one 40-byte TRS per joint. */
    for (size_t j = 0; j < CLIP_JOINTS; j++) {
        const uint8_t *trs = payload + FIX_OFF_BASE + (40 * j);
        for (size_t c = 0; c < 3; c++) {
            TEST_ASSERT_EQUAL_HEX32(f32_bits(k_base[j].t[c]), rd_u32(trs + (4 * c)));
            TEST_ASSERT_EQUAL_HEX32(f32_bits(k_base[j].s[c]), rd_u32(trs + 28 + (4 * c)));
        }
        for (size_t c = 0; c < 4; c++) {
            TEST_ASSERT_EQUAL_HEX32(f32_bits(k_base[j].q[c]), rd_u32(trs + 12 + (4 * c)));
        }
    }

    /* Blocks are sample-major: t row 0, t row 1, the q row, then the s row. */
    for (size_t i = 0; i < CLIP_SAMPLES; i++) {
        const uint8_t *block = payload + FIX_OFF_BLOCKS + (i * CLIP_STRIDE * 4);
        for (size_t c = 0; c < 3; c++) {
            TEST_ASSERT_EQUAL_HEX32(f32_bits(k_t_row0[(i * 3) + c]), rd_u32(block + (4 * c)));
            TEST_ASSERT_EQUAL_HEX32(f32_bits(k_t_row1[(i * 3) + c]), rd_u32(block + 12 + (4 * c)));
            TEST_ASSERT_EQUAL_HEX32(f32_bits(k_s_row0[(i * 3) + c]), rd_u32(block + 40 + (4 * c)));
        }
        for (size_t c = 0; c < 4; c++) {
            TEST_ASSERT_EQUAL_HEX32(f32_bits(k_q_row0[(i * 4) + c]), rd_u32(block + 24 + (4 * c)));
        }
    }

    TEST_ASSERT_EQUAL_UINT16(0, rd_u16(payload + FIX_OFF_T_JOINT));
    TEST_ASSERT_EQUAL_UINT16(2, rd_u16(payload + FIX_OFF_T_JOINT + 2));
    TEST_ASSERT_EQUAL_UINT16(1, rd_u16(payload + FIX_OFF_Q_JOINT));
    TEST_ASSERT_EQUAL_UINT16(2, rd_u16(payload + FIX_OFF_S_JOINT));

    free(payload);
}

/* A clip whose every channel folded into the base keeps its duration while
 * the grid holds a single sample and no block: header plus base only. */
void test_encode_clip_single_sample_has_no_blocks(void) {
    nt_skeletal_clip_t clip;
    memset(&clip, 0, sizeof(clip));
    clip.rig_compat_id = (nt_hash64_t){7ULL};
    clip.duration = 2.0;
    clip.base = k_base;
    clip.sample_count = 1;
    clip.joint_count = 1;

    uint8_t *payload = NULL;
    uint32_t size = 0;
    nt_builder_encode_clip(&clip, &payload, &size);
    TEST_ASSERT_NOT_NULL(payload);

    TEST_ASSERT_EQUAL_UINT32(44U + 40U, size);
    NtAnmHeader header;
    memcpy(&header, payload, sizeof(header));
    TEST_ASSERT_EQUAL_UINT32(1U, header.sample_count);
    TEST_ASSERT_EQUAL_HEX32(f32_bits(2.0F), f32_bits(header.duration));
    TEST_ASSERT_EQUAL_UINT16(0, header.n_t);
    TEST_ASSERT_EQUAL_UINT16(0, header.n_q);
    TEST_ASSERT_EQUAL_UINT16(0, header.n_s);
    for (size_t c = 0; c < 3; c++) {
        TEST_ASSERT_EQUAL_HEX32(f32_bits(k_base[0].t[c]), rd_u32(payload + 44 + (4 * c)));
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
    nt_skeletal_clip_t clip;
    fixture_clip(&clip);

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

    /* dump walks the three per-type detail printers over this pack. */
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

/* Both radii bound a culling sphere, so a NaN or a negative one would hide the
 * character instead of drawing it. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_encode_skin_binding_asserts_on_broken_radii(void) {
    uint8_t *payload = NULL;
    uint32_t size = 0;

    nt_skin_binding_t nan_reach = fixture_binding();
    nan_reach.reach = f32_from_bits(0x7FC00000U);
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_encode_skin_binding(&nan_reach, &payload, &size), "reach must be finite and non-negative");
    TEST_ASSERT_NULL(payload);

    nt_skin_binding_t negative_radius = fixture_binding();
    negative_radius.any_pose_radius = -0.5F;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_encode_skin_binding(&negative_radius, &payload, &size), "any_pose_radius must be finite and non-negative");
    TEST_ASSERT_NULL(payload);
}

/* The clip encoder asserts what a view needs to address memory; the values
 * inside are the importer's. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void test_encode_clip_asserts_on_broken_structure(void) {
    nt_skeletal_clip_t clip;
    uint8_t *payload = NULL;
    uint32_t size = 0;

    fixture_clip(&clip);
    clip.base = NULL;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_encode_clip(&clip, &payload, &size), "clip has no base pose");

    /* One sample cannot carry a sampled row, whatever the duration says. */
    fixture_clip(&clip);
    clip.sample_count = 1;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_encode_clip(&clip, &payload, &size), "sampled rows need frame blocks, at least two samples and a positive duration");

    fixture_clip(&clip);
    clip.blocks = NULL;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_encode_clip(&clip, &payload, &size), "sampled rows need frame blocks, at least two samples and a positive duration");

    fixture_clip(&clip);
    clip.q_joint = NULL;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_encode_clip(&clip, &payload, &size), "a joint table is NULL while its row count is not");

    /* The header stores a float duration; a double the float cannot hold would
     * move the grid the samples were taken on. */
    fixture_clip(&clip);
    clip.duration = 1.0 + 1e-12;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_encode_clip(&clip, &payload, &size), "exactly representable as float");

    fixture_clip(&clip);
    clip.r_root = -0.5F;
    EXPECT_BUILD_ASSERT_MATCH(nt_builder_encode_clip(&clip, &payload, &size), "clip bounds must be finite and non-negative");

    TEST_ASSERT_NULL(payload);
}
// #endregion

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_encode_skeleton_wire_layout);
    RUN_TEST(test_encode_skin_binding_wire_layout);
    RUN_TEST(test_encode_clip_header_counts);
    RUN_TEST(test_encode_clip_array_layout);
    RUN_TEST(test_encode_clip_single_sample_has_no_blocks);
    RUN_TEST(test_add_skeletal_assets_writes_typed_entries);
    RUN_TEST(test_encode_skeleton_asserts_on_a_broken_hierarchy);
    RUN_TEST(test_encode_skin_binding_asserts_on_broken_radii);
    RUN_TEST(test_encode_clip_asserts_on_broken_structure);
    return UNITY_END();
}
