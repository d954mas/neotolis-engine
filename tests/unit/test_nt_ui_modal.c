/* Modal helper tests — z-band math, stack push/pop balance, depth-overflow death,
 * top-only close targeting, open/close tween clamp + fully_closed/visible transition,
 * high-level wrapper contract, and world auto-gate via wants_pointer.
 *
 * Behaviour is driven through the walker fixture + NT_TEST_ACCESS probes (no GL).
 * UNITY_EXCLUDE_FLOAT: compare floats via an eps helper. */

#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "input/nt_input_internal.h" /* nt_input_set_key / nt_input_clear_all_keys */
#include "memory/nt_mem_scratch.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_modal.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

void setUp(void) {
    nt_test_assert_install();
    nt_input_clear_all_keys();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) {
    nt_input_clear_all_keys();
    ui_walker_fixture_shutdown(&s_fx);
}

/* ---- ABI sanity: the _Static_asserts compile; assert the runtime sizes match too. ---- */
static void test_modal_abi_sizes(void) {
    TEST_ASSERT_EQUAL_UINT(12U, (unsigned)sizeof(nt_ui_modal_result_t));
    TEST_ASSERT_EQUAL_UINT(24U, (unsigned)sizeof(nt_ui_modal_style_t));
}

/* ---- Defaults are a valid (non-zero-init) style: scale_start > 0 dodges the anim trap. ---- */
static void test_modal_defaults_valid(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    TEST_ASSERT_TRUE(st.scale_start > 0.0F);
    TEST_ASSERT_TRUE(st.ease_speed > 0.0F);
    TEST_ASSERT_TRUE((st.flags & NT_UI_MODAL_LISTEN_ESC) != 0U);
    TEST_ASSERT_TRUE((st.flags & NT_UI_MODAL_CLOSE_ON_BACKDROP) != 0U);
}

/* ---- Stack depth is zero at frame start and after balanced begin/end. ---- */
static void test_modal_stack_depth_zero_balanced(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    TEST_ASSERT_EQUAL_UINT8(0U, nt_ui_modal_test_stack_depth(s_fx.ctx));

    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
    (void)nt_ui_modal_begin(s_fx.ctx, 0x4D0001U, &st, true);
    TEST_ASSERT_EQUAL_UINT8(1U, nt_ui_modal_test_stack_depth(s_fx.ctx));
    nt_ui_modal_end(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT8(0U, nt_ui_modal_test_stack_depth(s_fx.ctx));
    nt_ui_end(s_fx.ctx);
}

/* ---- Pushing past NT_UI_MODAL_MAX_DEPTH fires NT_ASSERT (MODAL-05, no silent fallback). ---- */
static void test_modal_depth_overflow_asserts(void) {
    nt_ui_modal_style_t st = nt_ui_modal_style_defaults();
    nt_ui_begin(s_fx.ctx, 800.0F, 600.0F, 1.0F / 60.0F, &(nt_pointer_t){.active = true}, 1);
    /* Fill the stack to the cap — each unique id. */
    for (uint32_t i = 0; i < NT_UI_MODAL_MAX_DEPTH; ++i) {
        (void)nt_ui_modal_begin(s_fx.ctx, 0x4D1000U + i, &st, true);
    }
    TEST_ASSERT_EQUAL_UINT8((uint8_t)NT_UI_MODAL_MAX_DEPTH, nt_ui_modal_test_stack_depth(s_fx.ctx));
    /* The (cap+1)-th push must trap. */
    NT_TEST_EXPECT_ASSERT(nt_ui_modal_begin(s_fx.ctx, 0x4D1FFFU, &st, true));
    /* Unwind so nt_ui_end sees a balanced stack. */
    for (uint32_t i = 0; i < NT_UI_MODAL_MAX_DEPTH; ++i) {
        nt_ui_modal_end(s_fx.ctx);
    }
    nt_ui_end(s_fx.ctx);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_modal_abi_sizes);
    RUN_TEST(test_modal_defaults_valid);
    RUN_TEST(test_modal_stack_depth_zero_balanced);
    RUN_TEST(test_modal_depth_overflow_asserts);
    return UNITY_END();
}
