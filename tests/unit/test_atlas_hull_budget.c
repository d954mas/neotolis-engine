/* Golden hull vertex totals across the public area budgets on real sprite art: a geometry change
 * in the builder shows up here as a numeric diff, never as a silent drift. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

/* clang-format off */
#include "nt_builder.h"
#include "test_helpers/atlas_dedup_fixture.h"
#include "unity.h"
/* clang-format on */

#define TMP_DIR "build/tests/tmp"
#define SPRITE_COUNT 12U
#define BUDGET_COUNT 6U

static const char *const SPRITES[SPRITE_COUNT] = {
    "assets/sprites/bigatlas/resource_apple.png",     "assets/sprites/bigatlas/arrow_clockwise.png",   "assets/sprites/bigatlas/arrow_right_curve.png",  "assets/sprites/bigatlas/award.png",
    "assets/sprites/bigatlas/book_open.png",          "assets/sprites/bigatlas/cards_stack_cross.png", "assets/sprites/bigatlas/hand_token_open.png",    "assets/sprites/bigatlas/d12.png",
    "assets/sprites/bigatlas/suit_hearts_broken.png", "assets/sprites/bigatlas/emote_anger.png",       "assets/sprites/bigatlas/emote_heartBroken_.png", "assets/sprites/bigatlas/flip_empty.png",
};
static const float BUDGETS[BUDGET_COUNT] = {0.0F, 2.0F, 5.0F, 10.0F, 15.0F, 25.0F};
static const uint32_t EXPECTED_TOTALS[BUDGET_COUNT] = {87, 83, 79, 67, 57, 51};

void setUp(void) {}
void tearDown(void) {}

static uint32_t hull_vertex_total(float budget) {
    const char *path = TMP_DIR "/hull_budget.ntpack";
    (void)MKDIR("build");
    (void)MKDIR("build/tests");
    (void)MKDIR(TMP_DIR);
    NtBuilderContext *ctx = nt_builder_start_pack(path);
    TEST_ASSERT_NOT_NULL(ctx);
    nt_builder_set_threads(ctx, 1);
    nt_atlas_opts_t opts = nt_atlas_opts_defaults();
    opts.max_added_area_percent = budget;
    NtAtlasBuild *atlas = nt_atlas_begin(ctx, "hull_budget", &opts);
    for (uint32_t i = 0; i < SPRITE_COUNT; i++) {
        nt_atlas_add(atlas, SPRITES[i], NULL);
    }
    TEST_ASSERT_EQUAL_INT(NT_BUILD_OK, nt_atlas_commit(atlas));
    TEST_ASSERT_EQUAL_INT(NT_BUILD_OK, nt_builder_finish_pack(ctx));
    nt_builder_free_pack(ctx);

    FILE *f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL(f);
    (void)fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    (void)fseek(f, 0, SEEK_SET);
    uint8_t *bytes = (uint8_t *)malloc((size_t)size);
    TEST_ASSERT_NOT_NULL(bytes);
    TEST_ASSERT_EQUAL_size_t((size_t)size, fread(bytes, 1, (size_t)size, f));
    (void)fclose(f);

    nt_atlas_dedup_region_t regions[SPRITE_COUNT];
    uint32_t count = 0;
    TEST_ASSERT_TRUE(atlas_dedup_collect_regions(bytes, (size_t)size, regions, SPRITE_COUNT, &count));
    free(bytes);
    TEST_ASSERT_EQUAL_UINT32(SPRITE_COUNT, count);
    uint32_t total = 0;
    for (uint32_t i = 0; i < count; i++) {
        total += regions[i].vertex_count;
    }
    return total;
}

void test_hull_vertex_totals_per_budget(void) {
    uint32_t totals[BUDGET_COUNT];
    for (uint32_t i = 0; i < BUDGET_COUNT; i++) {
        totals[i] = hull_vertex_total(BUDGETS[i]);
    }
    TEST_ASSERT_EQUAL_UINT32_ARRAY(EXPECTED_TOTALS, totals, BUDGET_COUNT);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_hull_vertex_totals_per_budget);
    return UNITY_END();
}
