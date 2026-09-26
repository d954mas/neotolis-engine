#include <string.h>

#ifdef NT_DEVAPI_ENABLED
#undef NT_DEVAPI_ENABLED
#endif
#define main nt_skeletal_showcase_main
// NOLINTNEXTLINE(bugprone-suspicious-include): exercise the showcase's real game-owned controller.
#include "../../examples/skeletal_showcase/main.c"
#undef main

#include "test_helpers/nt_assert_trap.h"
#include "unity.h"

static nt_skeletal_trs_t s_test_rest[3];
static uint16_t s_test_parent[3];
static uint16_t s_test_subtree_end[3];
static uint32_t s_test_joint_ids[3];
static nt_skeletal_skeleton_t s_test_skeleton;
static nt_skeletal_clip_t s_test_clips[MIX_CLIP_COUNT];

void setUp(void) {
    nt_test_assert_install();
    memset(&s_mixing_scene, 0, sizeof s_mixing_scene);
    for (uint32_t j = 0; j < 3; ++j) {
        s_test_rest[j].q[3] = 1.0F;
        s_test_rest[j].s[0] = s_test_rest[j].s[1] = s_test_rest[j].s[2] = 1.0F;
        s_test_parent[j] = j == 0 ? NT_SKELETAL_NO_PARENT : (uint16_t)(j - 1U);
        s_test_subtree_end[j] = 3;
        s_test_joint_ids[j] = nt_hash32_str(j == 1 ? "spine" : "joint").value;
    }
    s_test_skeleton = (nt_skeletal_skeleton_t){.parent = s_test_parent, .subtree_end = s_test_subtree_end, .joint_id = s_test_joint_ids, .rest = s_test_rest, .joint_count = 3};
    s_mixing_scene.skel = &s_test_skeleton;
    for (uint32_t i = 0; i < MIX_CLIP_COUNT; ++i) {
        s_test_clips[i] = (nt_skeletal_clip_t){.duration = 1.0, .base = s_test_rest, .r_root = (float)i, .sample_count = 1, .joint_count = 3};
        s_mixing_scene.clips[i] = &s_test_clips[i];
    }
    s_mixing_scene.transition_duration = 1.0F;
    s_mixing_scene.pending_target = -1;
}

void tearDown(void) {}

static void test_blend_space_uses_adjacent_normalized_weights(void) {
    float gains[3];
    mixing_blend_gains(0.25F, true, gains);
    TEST_ASSERT_TRUE(fabsf(gains[0] - 0.5F) < 1e-6F);
    TEST_ASSERT_TRUE(fabsf(gains[1] - 0.5F) < 1e-6F);
    TEST_ASSERT_TRUE(fabsf(gains[2]) < 1e-6F);
    mixing_blend_gains(0.25F, false, gains);
    TEST_ASSERT_TRUE(fabsf(gains[0]) < 1e-6F);
    TEST_ASSERT_TRUE(fabsf(gains[1] - 0.75F) < 1e-6F);
    TEST_ASSERT_TRUE(fabsf(gains[2] - 0.25F) < 1e-6F);
}

static void test_zero_gain_track_stays_occupied_and_advances(void) {
    mixing_assign_slot(0, MIX_CLIP_IDLE, true);
    mixing_assign_slot(1, MIX_CLIP_WALK, true);
    s_mixing_scene.slots[0].gain = 1.0F;
    s_mixing_scene.slots[1].gain = 0.0F;
    mixing_advance_tracks(0.25);
    TEST_ASSERT_TRUE(fabs(s_mixing_scene.slots[0].track.time - 0.25) < 1e-12);
    TEST_ASSERT_TRUE(fabs(s_mixing_scene.slots[1].track.time - 0.25) < 1e-12);
    TEST_ASSERT_BITS_HIGH(NT_SKELETAL_TRACK_OCCUPIED, s_mixing_scene.slots[1].track.flags);
}

static void test_assigning_an_occupied_slot_asserts(void) {
    mixing_assign_slot(0, MIX_CLIP_IDLE, true);
    NT_TEST_EXPECT_ASSERT(mixing_assign_slot(0, MIX_CLIP_RUN, true));
}

static void test_interruption_captures_exact_signal_before_reuse(void) {
    mixing_assign_slot(0, MIX_CLIP_IDLE, true);
    mixing_assign_slot(1, MIX_CLIP_RUN, true);
    nt_skeletal_trs_t signal[3];
    memcpy(signal, s_test_rest, sizeof signal);
    signal[0].t[0] = 7.25F;
    signal[1].q[2] = 0.25F;
    signal[1].q[3] = sqrtf(1.0F - (0.25F * 0.25F));
    s_mixing_scene.pending_target = MIX_CLIP_JUMP;
    mixing_process_interruption(signal);
    TEST_ASSERT_EQUAL_MEMORY(signal, s_mixing_scene.snapshot, sizeof signal);
    TEST_ASSERT_EQUAL_UINT32(0, s_mixing_scene.slots[0].track.flags);
    TEST_ASSERT_EQUAL_INT(MIX_CLIP_JUMP, s_mixing_scene.slots[1].clip);
    TEST_ASSERT_TRUE(s_mixing_scene.using_snapshot);
    TEST_ASSERT_TRUE(fabsf(s_mixing_scene.transition_elapsed) < 1e-6F);

    nt_skeletal_trs_t top[3];
    nt_skeletal_trs_t out[3];
    memcpy(top, s_test_rest, sizeof top);
    top[0].t[0] = -4.0F;
    nt_skeletal_override(s_mixing_scene.snapshot, top, NULL, 0.0F, 3, out);
    TEST_ASSERT_EQUAL_MEMORY(signal, out, sizeof signal);
}

static void test_repeated_interruptions_reuse_one_snapshot(void) {
    mixing_assign_slot(0, MIX_CLIP_IDLE, true);
    nt_skeletal_trs_t signal[3];
    memcpy(signal, s_test_rest, sizeof signal);
    nt_skeletal_trs_t *const snapshot_address = s_mixing_scene.snapshot;
    for (uint32_t i = 0; i < 64; ++i) {
        signal[0].t[0] = (float)i;
        s_mixing_scene.pending_target = (i & 1U) != 0U ? MIX_CLIP_RUN : MIX_CLIP_JUMP;
        mixing_process_interruption(signal);
    }
    TEST_ASSERT_TRUE(snapshot_address == s_mixing_scene.snapshot);
    TEST_ASSERT_EQUAL_UINT32(64, s_mixing_scene.handoff_count);
    TEST_ASSERT_TRUE(fabsf(s_mixing_scene.snapshot[0].t[0] - 63.0F) < 1e-6F);
}

static void test_masks_and_composed_bound_come_from_the_loaded_rig(void) {
    mixing_init_factors();
    TEST_ASSERT_TRUE(fabsf(s_mixing_scene.upper[0]) < 1e-6F);
    TEST_ASSERT_TRUE(fabsf(s_mixing_scene.lower[0] - 1.0F) < 1e-6F);
    TEST_ASSERT_TRUE(fabsf(s_mixing_scene.weighted[0] - 0.25F) < 1e-6F);
    TEST_ASSERT_TRUE(fabsf(s_mixing_scene.upper[1] - 1.0F) < 1e-6F);
    TEST_ASSERT_TRUE(fabsf(s_mixing_scene.lower[1]) < 1e-6F);
    TEST_ASSERT_TRUE(fabsf(s_mixing_scene.weighted[1] - 3.0F) < 1e-6F);
    TEST_ASSERT_TRUE(fabsf(s_mixing_scene.root_radius - 4.0F) < 1e-6F);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_blend_space_uses_adjacent_normalized_weights);
    RUN_TEST(test_zero_gain_track_stays_occupied_and_advances);
    RUN_TEST(test_assigning_an_occupied_slot_asserts);
    RUN_TEST(test_interruption_captures_exact_signal_before_reuse);
    RUN_TEST(test_repeated_interruptions_reuse_one_snapshot);
    RUN_TEST(test_masks_and_composed_bound_come_from_the_loaded_rig);
    return UNITY_END();
}
