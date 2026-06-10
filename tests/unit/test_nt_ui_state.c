/* Generic per-id state pool tests (#190).
 *
 * Direct-mapped, linear-probe, NO-eviction pool over ctx->state_pool[]. Pointer
 * is stable within a frame and across re-acquires until clear/clear_all; first
 * touch zeroes the payload. Re-acquire with a mismatched size and pool overflow
 * are NT_ASSERT death-tests (FULL-gated). A real ctx comes from the shared UI
 * fixture; no Clay frame is needed (the pool is plain BSS in the context). */

#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_test_arena.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_state.h"
#include "unity.h"

alignas(NT_UI_ARENA_ALIGN) static uint8_t s_arena[NT_UI_TEST_ARENA_SIZE];
static ui_walker_fixture_t s_fx;

typedef struct {
    float offset_x, offset_y;
    float velocity_x, velocity_y;
} scroll_state_t;

void setUp(void) {
    nt_test_assert_install();
    ui_walker_fixture_init(&s_fx, s_arena, sizeof s_arena, UI_WALKER_FX_BIND_ALL);
}

void tearDown(void) { ui_walker_fixture_shutdown(&s_fx); }

/* ---- Test 1: get-or-create returns a non-NULL ZEROED payload on first touch. ---- */
static void test_state_create_returns_zeroed(void) {
    const uint32_t id = 0x1111U;
    uint8_t *p = (uint8_t *)nt_ui_state(s_fx.ctx, id, 16U);
    TEST_ASSERT_NOT_NULL(p);
    for (uint32_t i = 0; i < 16U; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0U, p[i]); /* zero is a valid initial state */
    }
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_state_used_slots(s_fx.ctx));
    TEST_ASSERT_EQUAL_UINT32(16U, nt_ui_state_used_bytes(s_fx.ctx));
}

/* ---- Test 2: re-acquire same id+size returns the SAME pointer, mutation intact. ---- */
static void test_state_reacquire_same_pointer(void) {
    const uint32_t id = 0x2222U;
    scroll_state_t *a = (scroll_state_t *)nt_ui_state(s_fx.ctx, id, sizeof(scroll_state_t));
    TEST_ASSERT_NOT_NULL(a);
    a->offset_x = 42.0F;
    a->velocity_y = -7.0F;

    scroll_state_t *b = (scroll_state_t *)nt_ui_state(s_fx.ctx, id, sizeof(scroll_state_t));
    TEST_ASSERT_EQUAL_PTR(a, b);            /* same cell, stable pointer */
    TEST_ASSERT_TRUE(b->offset_x == 42.0F); /* mutation survives re-acquire */
    TEST_ASSERT_TRUE(b->velocity_y == -7.0F);
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_state_used_slots(s_fx.ctx)); /* no second cell */
}

/* ---- Test 3: find returns NULL for an id never created (no create, no assert). ---- */
static void test_state_find_absent_null(void) {
    TEST_ASSERT_NULL(nt_ui_state_find(s_fx.ctx, 0x9999U));
    (void)nt_ui_state(s_fx.ctx, 0x3333U, 8U);
    TEST_ASSERT_NOT_NULL(nt_ui_state_find(s_fx.ctx, 0x3333U)); /* present after create */
    TEST_ASSERT_NULL(nt_ui_state_find(s_fx.ctx, 0x4444U));     /* still absent */
    TEST_ASSERT_NULL(nt_ui_state_find(s_fx.ctx, 0U));          /* id 0 never present */
}

/* ---- Test 4: clear frees the cell; a later nt_ui_state re-creates it zeroed. ---- */
static void test_state_clear_then_recreate_zeroed(void) {
    const uint32_t id = 0x5555U;
    uint32_t *p = (uint32_t *)nt_ui_state(s_fx.ctx, id, sizeof(uint32_t));
    *p = 0xDEADBEEFU;
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_state_used_slots(s_fx.ctx));

    nt_ui_state_clear(s_fx.ctx, id);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_state_used_slots(s_fx.ctx));
    TEST_ASSERT_NULL(nt_ui_state_find(s_fx.ctx, id));

    uint32_t *q = (uint32_t *)nt_ui_state(s_fx.ctx, id, sizeof(uint32_t));
    TEST_ASSERT_NOT_NULL(q);
    TEST_ASSERT_EQUAL_UINT32(0U, *q); /* re-created cell is zeroed */

    nt_ui_state_clear(s_fx.ctx, 0U);                                /* id 0 is a no-op */
    nt_ui_state_clear(s_fx.ctx, 0x6666U);                           /* clearing an absent id is a no-op */
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_state_used_slots(s_fx.ctx)); /* only `q` lives */
}

/* ---- Test 5: clear_all empties the pool (used_slots == 0). ---- */
static void test_state_clear_all_empties(void) {
    (void)nt_ui_state(s_fx.ctx, 0x10U, 8U);
    (void)nt_ui_state(s_fx.ctx, 0x20U, 16U);
    (void)nt_ui_state(s_fx.ctx, 0x30U, 4U);
    TEST_ASSERT_EQUAL_UINT32(3U, nt_ui_state_used_slots(s_fx.ctx));
    TEST_ASSERT_EQUAL_UINT32(28U, nt_ui_state_used_bytes(s_fx.ctx));

    nt_ui_state_clear_all(s_fx.ctx);
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_state_used_slots(s_fx.ctx));
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_state_used_bytes(s_fx.ctx));
    TEST_ASSERT_NULL(nt_ui_state_find(s_fx.ctx, 0x10U));
}

/* ---- Test 6: open addressing coexists two ids sharing one base bucket. ---- */
static void test_state_open_address_coexists(void) {
    const uint32_t id1 = 5U;
    const uint32_t id2 = 5U + NT_UI_STATE_SLOTS; /* same base bucket as id1 */
    TEST_ASSERT_EQUAL_UINT32(id1 & (NT_UI_STATE_SLOTS - 1U), id2 & (NT_UI_STATE_SLOTS - 1U));

    uint32_t *a = (uint32_t *)nt_ui_state(s_fx.ctx, id1, sizeof(uint32_t));
    uint32_t *b = (uint32_t *)nt_ui_state(s_fx.ctx, id2, sizeof(uint32_t));
    TEST_ASSERT_TRUE_MESSAGE(a != b, "linear probe must land id2 in a different slot than id1");
    *a = 1U;
    *b = 2U;
    TEST_ASSERT_EQUAL_UINT32(1U, *(uint32_t *)nt_ui_state(s_fx.ctx, id1, sizeof(uint32_t)));
    TEST_ASSERT_EQUAL_UINT32(2U, *(uint32_t *)nt_ui_state(s_fx.ctx, id2, sizeof(uint32_t)));
}

/* ---- Test 7: size == 0 is legal (cell with no payload still occupies a slot). ---- */
static void test_state_zero_size_payload(void) {
    void *p = nt_ui_state(s_fx.ctx, 0x7777U, 0U);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT32(1U, nt_ui_state_used_slots(s_fx.ctx));
    TEST_ASSERT_EQUAL_UINT32(0U, nt_ui_state_used_bytes(s_fx.ctx));
}

/* ---- Death tests (NT_ASSERT_FULL only) ---- */
#if NT_ASSERT_MODE == NT_ASSERT_FULL

/* Re-acquire an id with a different size -> assert (D-59-08 size-collision guard). */
static void test_assert_size_mismatch(void) {
    const uint32_t id = 0xAAAAU;
    (void)nt_ui_state(s_fx.ctx, id, 16U);
    NT_TEST_EXPECT_ASSERT((void)nt_ui_state(s_fx.ctx, id, 32U));
}

/* id 0 is reserved (empty-slot sentinel) -> assert. */
static void test_assert_id_zero(void) { NT_TEST_EXPECT_ASSERT((void)nt_ui_state(s_fx.ctx, 0U, 8U)); }

/* size > payload max -> assert (game must store its own pointer, D-59-09). */
static void test_assert_oversize(void) { NT_TEST_EXPECT_ASSERT((void)nt_ui_state(s_fx.ctx, 0xBBBBU, NT_UI_STATE_PAYLOAD_MAX + 1U)); }

/* Fill the probe chain for one base bucket, then one more create -> overflow assert
 * (no LRU eviction, D-59-07). PROBE_MAX+1 ids that all hash to the same base. */
static void test_assert_probe_overflow(void) {
    const uint32_t base = 3U;
    for (uint32_t k = 0; k < NT_UI_STATE_PROBE_MAX; ++k) {
        (void)nt_ui_state(s_fx.ctx, base + (k * NT_UI_STATE_SLOTS), 8U);
    }
    const uint32_t one_too_many = base + (NT_UI_STATE_PROBE_MAX * NT_UI_STATE_SLOTS);
    NT_TEST_EXPECT_ASSERT((void)nt_ui_state(s_fx.ctx, one_too_many, 8U));
}

#endif /* NT_ASSERT_MODE == NT_ASSERT_FULL */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_state_create_returns_zeroed);
    RUN_TEST(test_state_reacquire_same_pointer);
    RUN_TEST(test_state_find_absent_null);
    RUN_TEST(test_state_clear_then_recreate_zeroed);
    RUN_TEST(test_state_clear_all_empties);
    RUN_TEST(test_state_open_address_coexists);
    RUN_TEST(test_state_zero_size_payload);
#if NT_ASSERT_MODE == NT_ASSERT_FULL
    RUN_TEST(test_assert_size_mismatch);
    RUN_TEST(test_assert_id_zero);
    RUN_TEST(test_assert_oversize);
    RUN_TEST(test_assert_probe_overflow);
#endif
    return UNITY_END();
}
