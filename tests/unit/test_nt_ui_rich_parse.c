/* Rich-text markup parser + tagset vocabulary. No GL: the parser writes a frame-scratch
 * run-list (read back via the build probes); the tagset is a plain owned struct.
 * Task 1 covers the tagset register/lookup; Tasks 2-3 add parser + malformed death tests. */

#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "clay.h"
#include "core/nt_assert.h"
#include "font/nt_font.h"
#include "hash/nt_hash.h"
#include "memory/nt_mem_scratch.h"
#include "resource/nt_resource.h"
#include "test_helpers/nt_assert_trap.h"
#include "test_helpers/ui_walker_fixture.h"
#include "ui/nt_ui.h"
#include "ui/nt_ui_internal.h"
#include "ui/nt_ui_rich_tagset.h"
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

static uint64_t h(const char *s) { return nt_hash64_str(s).value; }

/* (1) register_font then lookup by name returns the family; an unknown name misses. */
static void test_tagset_font_register_lookup(void) {
    nt_ui_rich_tagset_t ts;
    nt_ui_rich_tagset_init(&ts);

    const nt_font_t fam[4] = {{.id = 10}, {.id = 11}, {.id = 12}, {.id = 13}};
    nt_ui_rich_tagset_register_font(&ts, "heading", fam);

    nt_font_t out[4];
    TEST_ASSERT_TRUE(nt_ui_rich_tagset_lookup_font(&ts, h("heading"), out));
    TEST_ASSERT_EQUAL_UINT32(10U, out[0].id);
    TEST_ASSERT_EQUAL_UINT32(13U, out[3].id);

    nt_font_t miss[4];
    TEST_ASSERT_FALSE(nt_ui_rich_tagset_lookup_font(&ts, h("body"), miss));
}

/* (2) register_color / register_atlas / register_effect resolve by name. */
static void test_tagset_color_atlas_effect(void) {
    nt_ui_rich_tagset_t ts;
    nt_ui_rich_tagset_init(&ts);

    nt_ui_rich_tagset_register_color(&ts, "gold", 0xFF00D7FFU);
    nt_ui_rich_tagset_register_atlas(&ts, "icons", (nt_resource_t){.id = 42U});
    nt_ui_rich_tagset_register_effect(&ts, "wave", 3U);

    uint32_t color = 0;
    TEST_ASSERT_TRUE(nt_ui_rich_tagset_lookup_color(&ts, h("gold"), &color));
    TEST_ASSERT_EQUAL_HEX32(0xFF00D7FFU, color);

    nt_resource_t atlas = {0};
    TEST_ASSERT_TRUE(nt_ui_rich_tagset_lookup_atlas(&ts, h("icons"), &atlas));
    TEST_ASSERT_EQUAL_UINT32(42U, atlas.id);

    uint8_t fx = 0;
    TEST_ASSERT_TRUE(nt_ui_rich_tagset_lookup_effect(&ts, h("wave"), &fx));
    TEST_ASSERT_EQUAL_UINT8(3U, fx);
}

/* (3) re-registering a name overrides it in place (no duplicate entry). */
static void test_tagset_override_in_place(void) {
    nt_ui_rich_tagset_t ts;
    nt_ui_rich_tagset_init(&ts);

    nt_ui_rich_tagset_register_color(&ts, "accent", 0xFF112233U);
    nt_ui_rich_tagset_register_color(&ts, "accent", 0xFFAABBCCU);
    TEST_ASSERT_EQUAL_UINT32(1U, ts.color_count);

    uint32_t color = 0;
    TEST_ASSERT_TRUE(nt_ui_rich_tagset_lookup_color(&ts, h("accent"), &color));
    TEST_ASSERT_EQUAL_HEX32(0xFFAABBCCU, color);
}

/* (4) reset empties every table; lookups then miss. */
static void test_tagset_reset(void) {
    nt_ui_rich_tagset_t ts;
    nt_ui_rich_tagset_init(&ts);
    nt_ui_rich_tagset_register_color(&ts, "gold", 0xFF00D7FFU);
    nt_ui_rich_tagset_reset(&ts);

    uint32_t color = 0;
    TEST_ASSERT_EQUAL_UINT32(0U, ts.color_count);
    TEST_ASSERT_FALSE(nt_ui_rich_tagset_lookup_color(&ts, h("gold"), &color));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_tagset_font_register_lookup);
    RUN_TEST(test_tagset_color_atlas_effect);
    RUN_TEST(test_tagset_override_in_place);
    RUN_TEST(test_tagset_reset);
    return UNITY_END();
}
