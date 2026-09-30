#include "graphics/nt_gfx.h"
#include "test_helpers/nt_gfx_fake.h"
#include "unity.h"

void setUp(void) {
    nt_gfx_desc_t desc = nt_gfx_desc_defaults();
    desc.max_shaders = 4;
    desc.max_programs = 2;
    desc.max_pipelines = 1;
    nt_gfx_init(&desc);
}

void tearDown(void) {
    nt_gfx_shutdown();
    nt_gfx_fake_reset();
}

static void test_pending_pipeline_is_rejected_without_finishing(void) {
    for (uint32_t pending = 0; pending < 2; pending++) {
        nt_gfx_fake_set_links_pending(pending != 0);
        nt_program_t program = nt_gfx_fake_make_program(NULL, 0);
        const uint32_t finishes = nt_gfx_fake_program_finish_count();
        const uint32_t pipelines = nt_gfx_fake_pipeline_create_count();

        TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program}).id);
        TEST_ASSERT_EQUAL_UINT32(finishes, nt_gfx_fake_program_finish_count());
        TEST_ASSERT_EQUAL_UINT32(pipelines, nt_gfx_fake_pipeline_create_count());
        TEST_ASSERT_EQUAL_INT(NT_GFX_PROGRAM_READY, nt_gfx_program_wait(program));
        const uint32_t after_wait = nt_gfx_fake_program_finish_count();
        TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program}).id);
        TEST_ASSERT_EQUAL_UINT32(after_wait, nt_gfx_fake_program_finish_count());
        nt_gfx_destroy_program(program);
    }
}

static void test_invalid_and_stale_programs_never_create_pipelines(void) {
    TEST_ASSERT_EQUAL_INT(NT_GFX_PROGRAM_UNAVAILABLE, nt_gfx_program_wait(NT_PROGRAM_INVALID));
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = NT_PROGRAM_INVALID}).id);
    nt_program_t stale = nt_gfx_fake_make_program(NULL, 0);
    nt_gfx_destroy_program(stale);
    nt_program_t fresh = nt_gfx_fake_make_program(NULL, 0);
    TEST_ASSERT_NOT_EQUAL_UINT32(stale.id, fresh.id);
    TEST_ASSERT_EQUAL_INT(NT_GFX_PROGRAM_UNAVAILABLE, nt_gfx_program_wait(stale));
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = stale}).id);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_program_finish_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_pipeline_create_count());
    TEST_ASSERT_EQUAL_INT(NT_GFX_PROGRAM_READY, nt_gfx_program_wait(fresh));
    TEST_ASSERT_NOT_EQUAL_UINT32(0, nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = fresh}).id);
}

static void test_failed_wait_leaves_a_terminal_program(void) {
    nt_program_t program = nt_gfx_fake_make_program(NULL, 0);
    nt_gfx_fake_fail_next_link();
    TEST_ASSERT_EQUAL_INT(NT_GFX_PROGRAM_UNAVAILABLE, nt_gfx_program_wait(program));
    const uint32_t finishes = nt_gfx_fake_program_finish_count();
    TEST_ASSERT_EQUAL_INT(NT_GFX_PROGRAM_UNAVAILABLE, nt_gfx_program_wait(program));
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program}).id);
    TEST_ASSERT_EQUAL_UINT32(finishes, nt_gfx_fake_program_finish_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_pipeline_create_count());
}

static void test_wait_loss_does_not_advance_a_frame_or_revive_the_program(void) {
    nt_program_t program = nt_gfx_fake_make_program(NULL, 0);
    nt_gfx_fake_set_context_lost(true);
    TEST_ASSERT_FALSE(g_nt_gfx.context_lost);
    const uint32_t finishes_before = nt_gfx_fake_program_finish_count();
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program}).id);
    TEST_ASSERT_EQUAL_UINT32(finishes_before, nt_gfx_fake_program_finish_count());
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_pipeline_create_count());
    const uint64_t frame = g_nt_gfx.counters.frame_sequence;
    TEST_ASSERT_EQUAL_INT(NT_GFX_PROGRAM_UNAVAILABLE, nt_gfx_program_wait(program));
    TEST_ASSERT_EQUAL_UINT64(frame, g_nt_gfx.counters.frame_sequence);
    nt_gfx_begin_frame();
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program}).id);
    nt_gfx_fake_set_context_lost(false);
    nt_gfx_begin_frame();
    TEST_ASSERT_EQUAL_INT(NT_GFX_PROGRAM_UNAVAILABLE, nt_gfx_program_wait(program));
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_make_pipeline(&(nt_pipeline_desc_t){.program = program}).id);
    TEST_ASSERT_EQUAL_UINT32(0, nt_gfx_fake_pipeline_create_count());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_pending_pipeline_is_rejected_without_finishing);
    RUN_TEST(test_invalid_and_stale_programs_never_create_pipelines);
    RUN_TEST(test_failed_wait_leaves_a_terminal_program);
    RUN_TEST(test_wait_loss_does_not_advance_a_frame_or_revive_the_program);
    return UNITY_END();
}
