#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "core/nt_assert.h"
#include "hash/nt_hash.h"
#include "nt_pack_format.h"
#include "nt_skeletal_format.h"
#include "resource/nt_resource.h"
#include "skeletal/nt_skeletal.h"
#include "skeletal_assets/nt_skeletal_assets.h"
#include "test_helpers/nt_assert_trap.h"
#include "unity.h"

enum { JOINTS = 4, SAMPLES = 3, STRIDE = 17, STORAGE_BYTES = 374, MAX_WIRE_BYTES = 422 };

static const nt_skeletal_trs_t k_base[JOINTS] = {
    {{1.25F, -2.5F, 3.75F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}},
    {{-0.0F, 7.25F, 0.125F}, {0.0F, 0.0F, 0.0F, 1.0F}, {2.0F, 3.0F, 4.0F}},
    {{9.0F, 0.25F, -4.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {0.5F, 1.5F, 2.5F}},
    {{-3.0F, 2.0F, 1.0F}, {0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F}},
};

/* One T row, two Q rows, two S rows: half T before float Q makes the frame
 * stride 62 bytes and alternates the alignment of the following frame. */
static const float k_blocks[SAMPLES][STRIDE] = {
    {1.0003F, -0.0F, 0x1p-25F, 0.25F, 0.5F, 0.5F, 0.6614378F, 0.0F, 0.0F, 0.70710677F, 0.70710677F, 1.0003F, 2.0F, 0.5F, 0.75F, 1.5F, -1.0F},
    {2.0003F, -2.0F, 0x1p-24F, -0.25F, -0.5F, -0.5F, -0.6614378F, 0.0F, -0.70710677F, 0.0F, 0.70710677F, 1.5F, 3.0F, 0.25F, 1.0F, 2.0F, -0.5F},
    {3.0003F, -3.0F, -0x1p-26F, 0.5F, 0.25F, 0.6614378F, 0.5F, 0.70710677F, 0.0F, 0.0F, 0.70710677F, 2.0F, 4.0F, 0.125F, 1.25F, 2.5F, -0.25F},
};

/* Literal binary16 bytes and their exact widened values are independent of
 * both the builder and the production half converter. */
static const uint16_t k_half[SAMPLES][STRIDE] = {
    {0x3C00, 0x8000, 0x0000, 0x3400, 0x3800, 0x3800, 0x394B, 0x0000, 0x0000, 0x39A8, 0x39A8, 0x3C00, 0x4000, 0x3800, 0x3A00, 0x3E00, 0xBC00},
    {0x4000, 0xC000, 0x0001, 0xB400, 0xB800, 0xB800, 0xB94B, 0x0000, 0xB9A8, 0x0000, 0x39A8, 0x3E00, 0x4200, 0x3400, 0x3C00, 0x4000, 0xB800},
    {0x4200, 0xC200, 0x8000, 0x3800, 0x3400, 0x394B, 0x3800, 0x39A8, 0x0000, 0x0000, 0x39A8, 0x4000, 0x4400, 0x3000, 0x3D00, 0x4100, 0xB400},
};

static const float k_widened[SAMPLES][STRIDE] = {
    {1.0F, -0.0F, 0.0F, 0.25F, 0.5F, 0.5F, 0.66162109375F, 0.0F, 0.0F, 0.70703125F, 0.70703125F, 1.0F, 2.0F, 0.5F, 0.75F, 1.5F, -1.0F},
    {2.0F, -2.0F, 0x1p-24F, -0.25F, -0.5F, -0.5F, -0.66162109375F, 0.0F, -0.70703125F, 0.0F, 0.70703125F, 1.5F, 3.0F, 0.25F, 1.0F, 2.0F, -0.5F},
    {3.0F, -3.0F, -0.0F, 0.5F, 0.25F, 0.66162109375F, 0.5F, 0.70703125F, 0.0F, 0.0F, 0.70703125F, 2.0F, 4.0F, 0.125F, 1.25F, 2.5F, -0.25F},
};

static const uint16_t k_tables[5] = {3, 2, 0, 1, 3};

static void write_u16(uint8_t *at, uint16_t value) {
    at[0] = (uint8_t)value;
    at[1] = (uint8_t)(value >> 8);
}

static NtAnmHeader fixture_header(uint32_t layout) {
    return (NtAnmHeader){
        .magic = NT_ANM_MAGIC,
        .version = NT_SKELETAL_FORMAT_VERSION,
        .joint_count = JOINTS,
        .sample_count = SAMPLES,
        .duration = 0.7F,
        .rig_compat_id = UINT64_C(0x0123456789ABCDEF),
        .r_joints = 16.0F,
        .r_root = 8.0F,
        .s_max = 4.0F,
        .n_t = 1,
        .n_q = 2,
        .n_s = 2,
        .t_format = (uint8_t)((layout & 1U) ? NT_SKELETAL_FORMAT_FP16 : NT_SKELETAL_FORMAT_FP32),
        .q_format = (uint8_t)((layout & 2U) ? NT_SKELETAL_FORMAT_FP16 : NT_SKELETAL_FORMAT_FP32),
        .s_format = (uint8_t)((layout & 4U) ? NT_SKELETAL_FORMAT_FP16 : NT_SKELETAL_FORMAT_FP32),
    };
}

static uint32_t fixture_wire(uint8_t *payload, uint32_t layout) {
    const NtAnmHeader header = fixture_header(layout);
    memcpy(payload, &header, sizeof(header));
    memcpy(payload + 48, k_base, sizeof(k_base));
    uint8_t *at = payload + 208;
    for (uint32_t frame = 0; frame < SAMPLES; ++frame) {
        for (uint32_t lane = 0; lane < STRIDE; ++lane) {
            uint32_t bit = 4U;
            if (lane < 3U) {
                bit = 1U;
            } else if (lane < 11U) {
                bit = 2U;
            }
            if ((layout & bit) != 0U) {
                write_u16(at, k_half[frame][lane]);
                at += 2;
            } else {
                memcpy(at, &k_blocks[frame][lane], sizeof(float));
                at += 4;
            }
        }
    }
    for (uint32_t i = 0; i < 5U; ++i) {
        write_u16(at, k_tables[i]);
        at += 2;
    }
    return (uint32_t)(at - payload);
}

static nt_resource_t publish_clip(uint32_t runtime_handle) {
    const nt_hash32_t pack_id = nt_hash32_str("decode/virtual");
    const nt_hash64_t resource_id = nt_hash64_str("decode/clip");
    const nt_resource_t handle = nt_resource_request(resource_id, NT_ASSET_CLIP);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(pack_id, resource_id, NT_ASSET_CLIP, runtime_handle));
    nt_resource_step();
    TEST_ASSERT_TRUE(nt_resource_is_ready(handle));
    return handle;
}

void setUp(void) {
    nt_resource_init(NULL);
    nt_skeletal_assets_init(1);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_create_pack(nt_hash32_str("decode/virtual"), 0));
}

void tearDown(void) {
    nt_resource_shutdown();
    nt_skeletal_assets_shutdown();
    nt_assert_handler = NULL;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void check_blocks(const nt_skeletal_clip_t *clip, uint32_t layout) {
    TEST_ASSERT_EQUAL_MEMORY(k_base, clip->base, sizeof(k_base));
    TEST_ASSERT_EQUAL_UINT16(1, clip->n_t);
    TEST_ASSERT_EQUAL_UINT16(2, clip->n_q);
    TEST_ASSERT_EQUAL_UINT16(2, clip->n_s);
    TEST_ASSERT_EQUAL_UINT16(JOINTS, clip->joint_count);
    TEST_ASSERT_EQUAL_UINT32(SAMPLES, clip->sample_count);
    TEST_ASSERT_TRUE(clip->duration == (double)0.7F);
    TEST_ASSERT_TRUE(clip->rig_compat_id.value == UINT64_C(0x0123456789ABCDEF));
    TEST_ASSERT_TRUE(clip->r_joints == 16.0F && clip->r_root == 8.0F && clip->s_max == 4.0F);
    TEST_ASSERT_EQUAL_MEMORY(k_tables, clip->t_joint, 2);
    TEST_ASSERT_EQUAL_MEMORY(k_tables + 1, clip->q_joint, 4);
    TEST_ASSERT_EQUAL_MEMORY(k_tables + 3, clip->s_joint, 4);
    for (uint32_t frame = 0; frame < SAMPLES; ++frame) {
        const float *block = clip->blocks + ((size_t)frame * STRIDE);
        const float *t = (layout & 1U) ? k_widened[frame] : k_blocks[frame];
        const float *s = (layout & 4U) ? k_widened[frame] : k_blocks[frame];
        TEST_ASSERT_EQUAL_MEMORY(t, block, 3U * sizeof(float));
        TEST_ASSERT_EQUAL_MEMORY(s + 11, block + 11, 6U * sizeof(float));
        if ((layout & 2U) == 0U) {
            TEST_ASSERT_EQUAL_MEMORY(k_blocks[frame] + 3, block + 3, 8U * sizeof(float));
        } else {
            for (uint32_t row = 0; row < 2U; ++row) {
                const float *q = block + 3U + (4U * (size_t)row);
                const float *raw = k_widened[frame] + 3U + (4U * (size_t)row);
                double norm2 = 0.0;
                double actual_norm2 = 0.0;
                for (uint32_t c = 0; c < 4U; ++c) {
                    norm2 += (double)raw[c] * (double)raw[c];
                    actual_norm2 += (double)q[c] * (double)q[c];
                }
                TEST_ASSERT_TRUE(fabs(actual_norm2 - 1.0) < 1e-6);
                for (uint32_t c = 0; c < 4U; ++c) {
                    TEST_ASSERT_TRUE(fabs((double)q[c] - ((double)raw[c] / sqrt(norm2))) < 2e-7);
                }
            }
        }
    }
}

void test_all_layouts_decode_and_activate(void) {
    for (uint32_t layout = 0; layout < 8U; ++layout) {
        _Alignas(4) uint8_t payload[MAX_WIRE_BYTES];
        const uint32_t size = fixture_wire(payload, layout);
        const uint32_t expected_size = 218U + (3U * (((layout & 1U) ? 6U : 12U) + ((layout & 2U) ? 16U : 32U) + ((layout & 4U) ? 12U : 24U)));
        const NtAnmHeader header = fixture_header(layout);
        TEST_ASSERT_EQUAL_UINT32(expected_size, size);
        TEST_ASSERT_TRUE(nt_anm_size(&header) == expected_size);
        TEST_ASSERT_TRUE(nt_anm_decoded_size(&header) == STORAGE_BYTES);
        uint8_t *storage = (uint8_t *)malloc(STORAGE_BYTES + 4U);
        TEST_ASSERT_NOT_NULL(storage);
        memset(storage, 0xCD, STORAGE_BYTES + 4U);
        nt_skeletal_clip_t direct;
        nt_skeletal_clip_decode(payload, storage, STORAGE_BYTES + 4U, &direct);
        check_blocks(&direct, layout);
        const uint8_t canary[4] = {0xCD, 0xCD, 0xCD, 0xCD};
        TEST_ASSERT_EQUAL_MEMORY(canary, storage + STORAGE_BYTES, sizeof(canary));

        const uint32_t id = nt_skeletal_assets_activate_clip(payload, size);
        TEST_ASSERT_NOT_EQUAL_UINT32(0, id);
        const nt_skeletal_clip_t *activated = nt_skeletal_assets_clip(publish_clip(id));
        memset(payload, 0xA5, sizeof(payload));
        check_blocks(activated, layout);
        TEST_ASSERT_EQUAL_MEMORY(direct.blocks, activated->blocks, (size_t)SAMPLES * STRIDE * sizeof(float));
        for (uint32_t frame = 0; frame < SAMPLES; ++frame) {
            nt_skeletal_trs_t pose[JOINTS];
            nt_skeletal_sample(activated, (double)frame * activated->duration / 2.0, pose);
            const float *block = activated->blocks + ((size_t)frame * STRIDE);
            TEST_ASSERT_EQUAL_MEMORY(block, pose[3].t, 3U * sizeof(float));
            TEST_ASSERT_EQUAL_MEMORY(block + 3, pose[2].q, 4U * sizeof(float));
            TEST_ASSERT_EQUAL_MEMORY(block + 7, pose[0].q, 4U * sizeof(float));
            TEST_ASSERT_EQUAL_MEMORY(block + 11, pose[1].s, 3U * sizeof(float));
            TEST_ASSERT_EQUAL_MEMORY(block + 14, pose[3].s, 3U * sizeof(float));
            TEST_ASSERT_EQUAL_MEMORY(k_base[1].t, pose[1].t, 3U * sizeof(float));
        }
        nt_skeletal_trs_t a[JOINTS];
        nt_skeletal_trs_t b[JOINTS];
        nt_skeletal_sample(&direct, direct.duration / 4.0, a);
        nt_skeletal_sample(activated, activated->duration / 4.0, b);
        TEST_ASSERT_EQUAL_MEMORY(a, b, sizeof(a));
        TEST_ASSERT_TRUE(fabsf(b[3].t[0] - ((direct.blocks[0] + direct.blocks[STRIDE]) * 0.5F)) < 1e-6F);
        nt_resource_unregister(nt_hash32_str("decode/virtual"), nt_hash64_str("decode/clip"));
        nt_skeletal_assets_deactivate_clip(id);
        free(storage);
    }
}

void test_static_clip_preserves_base_and_has_no_blocks(void) {
    _Alignas(4) uint8_t payload[48 + sizeof(k_base)];
    NtAnmHeader header = fixture_header(7);
    header.sample_count = 1;
    header.n_t = 0;
    header.n_q = 0;
    header.n_s = 0;
    memcpy(payload, &header, sizeof(header));
    memcpy(payload + 48, k_base, sizeof(k_base));
    void *storage = malloc(sizeof(k_base));
    TEST_ASSERT_NOT_NULL(storage);
    nt_skeletal_clip_t clip;
    nt_skeletal_clip_decode(payload, storage, sizeof(k_base), &clip);
    TEST_ASSERT_NULL(clip.blocks);
    TEST_ASSERT_TRUE(nt_anm_size(&header) == sizeof(payload));
    TEST_ASSERT_TRUE(nt_anm_decoded_size(&header) == sizeof(k_base));
    nt_skeletal_trs_t pose[JOINTS];
    nt_skeletal_sample(&clip, clip.duration, pose);
    TEST_ASSERT_EQUAL_MEMORY(k_base, pose, sizeof(pose));
    free(storage);
}

#if NT_ASSERT_MODE == NT_ASSERT_FULL
void test_decoder_rejects_small_capacity_before_writing(void) {
    _Alignas(4) uint8_t payload[MAX_WIRE_BYTES];
    (void)fixture_wire(payload, 7);
    uint8_t *storage = (uint8_t *)malloc(STORAGE_BYTES);
    TEST_ASSERT_NOT_NULL(storage);
    memset(storage, 0xCD, STORAGE_BYTES);
    nt_skeletal_clip_t clip;
    NT_TEST_EXPECT_ASSERT(nt_skeletal_clip_decode(payload, storage, STORAGE_BYTES - 1U, &clip));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "storage_size"));
    for (uint32_t i = 0; i < STORAGE_BYTES; ++i) {
        TEST_ASSERT_EQUAL_HEX8(0xCD, storage[i]);
    }
    nt_skeletal_clip_decode(payload, storage, STORAGE_BYTES, &clip);
    check_blocks(&clip, 7);
    free(storage);
}

void test_decoder_rejects_unaligned_input_and_storage(void) {
    _Alignas(4) uint8_t payload[MAX_WIRE_BYTES + 4];
    _Alignas(4) uint8_t shifted[MAX_WIRE_BYTES + 4];
    const uint32_t size = fixture_wire(payload, 0);
    memcpy(shifted + 1, payload, size);
    uint8_t *storage = (uint8_t *)malloc(STORAGE_BYTES + 4U);
    TEST_ASSERT_NOT_NULL(storage);
    nt_skeletal_clip_t clip;
    NT_TEST_EXPECT_ASSERT(nt_skeletal_clip_decode(shifted + 1, storage, STORAGE_BYTES, &clip));
    NT_TEST_EXPECT_ASSERT(nt_skeletal_clip_decode(payload, storage + 1, STORAGE_BYTES, &clip));
    NT_TEST_EXPECT_ASSERT(nt_skeletal_assets_activate_clip(shifted + 1, size));
    const uint32_t id = nt_skeletal_assets_activate_clip(payload, size);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, id);
    nt_skeletal_assets_deactivate_clip(id);
    free(storage);
}

void test_invalid_tags_and_joint_indices_do_not_take_a_slot(void) {
    for (uint32_t tag = 42; tag < 45; ++tag) {
        _Alignas(4) uint8_t payload[MAX_WIRE_BYTES];
        const uint32_t size = fixture_wire(payload, 7);
        payload[tag] = 255;
        _Alignas(4) uint8_t storage[STORAGE_BYTES];
        uint8_t untouched[STORAGE_BYTES];
        memset(storage, 0xCD, sizeof(storage));
        memset(untouched, 0xCD, sizeof(untouched));
        nt_skeletal_clip_t clip;
        NT_TEST_EXPECT_ASSERT(nt_skeletal_clip_decode(payload, storage, sizeof(storage), &clip));
        TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "unsupported clip format"));
        TEST_ASSERT_EQUAL_MEMORY(untouched, storage, sizeof(storage));
        NT_TEST_EXPECT_ASSERT(nt_skeletal_assets_activate_clip(payload, size));
        TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "unsupported clip format"));
        (void)fixture_wire(payload, 7);
        const uint32_t id = nt_skeletal_assets_activate_clip(payload, size);
        TEST_ASSERT_NOT_EQUAL_UINT32(0, id);
        nt_skeletal_assets_deactivate_clip(id);
    }
    _Alignas(4) uint8_t payload[MAX_WIRE_BYTES];
    const uint32_t size = fixture_wire(payload, 7);
    write_u16(payload + size - 2U, JOINTS);
    NT_TEST_EXPECT_ASSERT(nt_skeletal_assets_activate_clip(payload, size));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "joint"));
    (void)fixture_wire(payload, 7);
    const uint32_t id = nt_skeletal_assets_activate_clip(payload, size);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, id);
    nt_skeletal_assets_deactivate_clip(id);
}

void test_absent_group_rejects_invalid_format_before_slot_allocation(void) {
    _Alignas(4) uint8_t payload[MAX_WIRE_BYTES];
    const uint32_t full_size = fixture_wire(payload, 7);
    /* Remove each frame's S rows and their joint table, keeping T/Q sampled. */
    for (size_t frame = 0; frame < SAMPLES; ++frame) {
        memmove(payload + 208U + (22U * frame), payload + 208U + (34U * frame), 22U);
    }
    memmove(payload + 274U, payload + full_size - 10U, 6U);
    NtAnmHeader header = fixture_header(7);
    header.n_s = 0;
    header.s_format = 255;
    memcpy(payload, &header, sizeof(header));
    NT_TEST_EXPECT_ASSERT(nt_skeletal_assets_activate_clip(payload, 280U));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "unsupported clip format"));
    header.s_format = NT_SKELETAL_FORMAT_FP16;
    memcpy(payload, &header, sizeof(header));
    const uint32_t id = nt_skeletal_assets_activate_clip(payload, 280U);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, id);
    nt_skeletal_assets_deactivate_clip(id);
}

void test_expanded_size_limit_precedes_array_access_and_slot_allocation(void) {
    _Alignas(4) NtAnmHeader header = fixture_header(1);
    header.joint_count = 1;
    header.sample_count = 400000000U;
    header.duration = 1.0F;
    header.n_q = 0;
    header.n_s = 0;
    TEST_ASSERT_TRUE(nt_anm_size(&header) == UINT64_C(2400000090));
    TEST_ASSERT_TRUE(nt_anm_decoded_size(&header) == UINT64_C(4800000042));
    /* Only the header is backed: the format-limit check must precede any
     * array access even when the declared wire size itself fits uint32. */
    uint32_t storage = UINT32_C(0xCDCDCDCD);
    nt_skeletal_clip_t clip;
    NT_TEST_EXPECT_ASSERT(nt_skeletal_clip_decode((const uint8_t *)&header, &storage, UINT32_MAX, &clip));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "UINT32_MAX"));
    TEST_ASSERT_EQUAL_HEX32(UINT32_C(0xCDCDCDCD), storage);
    NT_TEST_EXPECT_ASSERT(nt_skeletal_assets_activate_clip((const uint8_t *)&header, UINT32_C(2400000090)));
    TEST_ASSERT_NOT_NULL(strstr(nt_test_assert_last_expr, "UINT32_MAX"));
    _Alignas(4) uint8_t payload[MAX_WIRE_BYTES];
    const uint32_t size = fixture_wire(payload, 7);
    const uint32_t id = nt_skeletal_assets_activate_clip(payload, size);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, id);
    nt_skeletal_assets_deactivate_clip(id);
}
#endif

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_all_layouts_decode_and_activate);
    RUN_TEST(test_static_clip_preserves_base_and_has_no_blocks);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_decoder_rejects_small_capacity_before_writing);
    RUN_TEST(test_decoder_rejects_unaligned_input_and_storage);
    RUN_TEST(test_invalid_tags_and_joint_indices_do_not_take_a_slot);
    RUN_TEST(test_absent_group_rejects_invalid_format_before_slot_allocation);
    RUN_TEST(test_expanded_size_limit_precedes_array_access_and_slot_allocation);
#endif
    return UNITY_END();
}
