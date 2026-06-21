/* SOLVER PROTOTYPE SPIKE (plan-03 gate): mixed-run baseline + forced wrap on
 * asymmetric-height data, deterministic via nt_font_test_set_metrics, no GL.
 * Bodies filled in Task 3; Task 1 lands the scaffold + a linkage smoke. */

#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "atlas/nt_atlas.h"
#include "clay.h"
#include "core/nt_assert.h"
#include "font/nt_font.h"
#include "memory/nt_mem_scratch.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_rich_text.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

void setUp(void) {
    nt_test_assert_install();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
    nt_mem_scratch_reset();
    s_fx.ctx->pending_rich = NULL;
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

/* Task 1 scaffold smoke: the spike entry runs on an empty-ish run-list. */
static void test_rich_wrap_smoke(void) {
    nt_ui_rich_style_t base = nt_ui_rich_style_defaults();
    base.font_id[0] = s_fx.stub_font;
    nt_ui_rich_begin(s_fx.ctx, &base);
    nt_ui_rich_text_n(s_fx.ctx, "x", 1);
    nt_ui_rich_end(s_fx.ctx);
    nt_ui_rich_test_solve(s_fx.ctx, 1000.0F);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(1U, nt_ui_rich_test_line_count(s_fx.ctx));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_rich_wrap_smoke);
    return UNITY_END();
}
