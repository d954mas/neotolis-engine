#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include "nt_builder_internal.h"
#include "unity.h"

static bool s_fail_allocation;
static jmp_buf s_assert_jmp;
static const char *s_assert_expr;

static void *s_allocations[8];
static size_t s_allocation_count;

static void *test_calloc(size_t count, size_t size) {
    if (s_fail_allocation) {
        return NULL;
    }
    void *ptr = calloc(count, size);
    if (ptr != NULL) {
        TEST_ASSERT_TRUE(s_allocation_count < sizeof(s_allocations) / sizeof(s_allocations[0]));
        s_allocations[s_allocation_count++] = ptr;
    }
    return ptr;
}

static void test_free(void *ptr) {
    for (size_t i = 0; i < s_allocation_count; ++i) {
        if (s_allocations[i] == ptr) {
            s_allocations[i] = s_allocations[--s_allocation_count];
            break;
        }
    }
    free(ptr);
}

/* Inject allocation failure without changing the builder's allocator. */
#define calloc test_calloc
#define free test_free
// NOLINTNEXTLINE(bugprone-suspicious-include)
#include "nt_builder_codegen.c"
#undef calloc
#undef free

static void catch_assert(const char *expr, const char *file, int line) {
    (void)file;
    (void)line;
    s_assert_expr = expr;
    longjmp(s_assert_jmp, 1);
}

void setUp(void) {
    s_fail_allocation = false;
    s_assert_expr = NULL;
    nt_build_assert_handler = catch_assert;
}

static const char *const s_merge_paths[] = {"codegen_merge_a.tmp", "codegen_merge_b.tmp"};
static const char s_merge_output[] = "codegen_merge_output.tmp";

void tearDown(void) {
    nt_build_assert_handler = NULL;
    while (s_allocation_count > 0U) {
        free(s_allocations[--s_allocation_count]);
    }
    for (size_t i = 0; i < sizeof(s_merge_paths) / sizeof(s_merge_paths[0]); ++i) {
        (void)remove(s_merge_paths[i]);
    }
    (void)remove(s_merge_output);
}

static const CodegenEntry s_entries[] = {
    {.path = "icons/a-b", .resource_id = 1, .kind = NT_BUILD_ASSET_BLOB},
    {.path = "icons/a_b", .resource_id = 2, .kind = NT_BUILD_ASSET_BLOB},
};

static void test_collision_check_asserts_on_allocation_failure(void) {
    s_fail_allocation = true;
    if (setjmp(s_assert_jmp) == 0) {
        check_codegen_collisions(s_entries, 2);
        TEST_FAIL_MESSAGE("Expected collision-check allocation failure to assert");
    }
    TEST_ASSERT_NOT_NULL(strstr(s_assert_expr, "alloc failed"));
}

static void test_collision_check_rejects_matching_identifiers(void) {
    if (setjmp(s_assert_jmp) == 0) {
        check_codegen_collisions(s_entries, 2);
        TEST_FAIL_MESSAGE("Expected conflicting identifiers to assert");
    }
    TEST_ASSERT_NOT_NULL(strstr(s_assert_expr, "identifier collision"));
}

static void test_collision_check_accepts_distinct_identifiers(void) {
    const CodegenEntry entries[] = {
        {.path = "icons/a", .resource_id = 1, .kind = NT_BUILD_ASSET_BLOB},
        {.path = "icons/b", .resource_id = 2, .kind = NT_BUILD_ASSET_BLOB},
    };
    if (setjmp(s_assert_jmp) != 0) {
        TEST_FAIL_MESSAGE("Distinct identifiers must not assert");
    }
    check_codegen_collisions(entries, 2);
}

static void test_merge_rejects_identifier_collision(void) {
    const char *const headers[] = {
        "#define ASSET_BLOB_ICONS_A_B ((nt_hash64_t){0x0000000000000001ULL}) /* icons/a-b */\n",
        "#define ASSET_BLOB_ICONS_A_B ((nt_hash64_t){0x0000000000000002ULL}) /* icons/a_b */\n",
    };
    for (size_t i = 0; i < sizeof(s_merge_paths) / sizeof(s_merge_paths[0]); ++i) {
        FILE *f = fopen(s_merge_paths[i], "wb");
        TEST_ASSERT_NOT_NULL(f);
        const size_t len = strlen(headers[i]);
        TEST_ASSERT_EQUAL_size_t(len, fwrite(headers[i], 1U, len, f));
        TEST_ASSERT_EQUAL_INT(0, fclose(f));
    }
    if (setjmp(s_assert_jmp) == 0) {
        nt_builder_merge_headers(s_merge_paths, 2U, s_merge_output);
        TEST_FAIL_MESSAGE("Expected conflicting merged identifiers to assert");
    }
    TEST_ASSERT_NOT_NULL(strstr(s_assert_expr, "identifier collision"));
}

/* A 116-char BLOB path makes a 127-char identifier: valid, and longer than other kinds' would be. */
static void test_merge_keeps_longest_valid_identifier(void) {
    char path[117];
    memset(path, 'a', 116U);
    path[116] = '\0';
    char line[400];
    (void)snprintf(line, sizeof line, "#define ASSET_BLOB_%.116s ((nt_hash64_t){0x0000000000000001ULL}) /* %s */\n",
                   "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", path);
    FILE *f = fopen(s_merge_paths[0], "wb");
    TEST_ASSERT_NOT_NULL(f);
    TEST_ASSERT_EQUAL_size_t(strlen(line), fwrite(line, 1U, strlen(line), f));
    TEST_ASSERT_EQUAL_INT(0, fclose(f));
    if (setjmp(s_assert_jmp) != 0) {
        TEST_FAIL_MESSAGE(s_assert_expr);
    }
    nt_builder_merge_headers(s_merge_paths, 1U, s_merge_output);
    f = fopen(s_merge_output, "rb");
    TEST_ASSERT_NOT_NULL(f);
    char merged[4096] = {0};
    (void)fread(merged, 1U, sizeof(merged) - 1U, f);
    TEST_ASSERT_EQUAL_INT(0, fclose(f));
    TEST_ASSERT_NOT_NULL(strstr(merged, line));
}

/* Atlas regions hash only their name, so hud/icon and world/icon share a hash yet are distinct defines. */
static void test_merge_keeps_regions_sharing_a_hash(void) {
    const char *const headers[] = {
        "#define ASSET_ATLAS_REGION_HUD_ICON ((nt_hash64_t){0x0000000000000007ULL}) /* hud/icon */\n",
        "#define ASSET_ATLAS_REGION_WORLD_ICON ((nt_hash64_t){0x0000000000000007ULL}) /* world/icon */\n",
    };
    for (size_t i = 0; i < sizeof(s_merge_paths) / sizeof(s_merge_paths[0]); ++i) {
        FILE *f = fopen(s_merge_paths[i], "wb");
        TEST_ASSERT_NOT_NULL(f);
        const size_t len = strlen(headers[i]);
        TEST_ASSERT_EQUAL_size_t(len, fwrite(headers[i], 1U, len, f));
        TEST_ASSERT_EQUAL_INT(0, fclose(f));
    }
    nt_builder_merge_headers(s_merge_paths, 2U, s_merge_output);
    FILE *f = fopen(s_merge_output, "rb");
    TEST_ASSERT_NOT_NULL(f);
    char merged[4096] = {0};
    (void)fread(merged, 1U, sizeof(merged) - 1U, f);
    TEST_ASSERT_EQUAL_INT(0, fclose(f));
    TEST_ASSERT_NOT_NULL(strstr(merged, headers[0]));
    TEST_ASSERT_NOT_NULL(strstr(merged, headers[1]));
}

/* An atlas whose path starts with "region" shares the ATLAS_REGION_ prefix; the merge must keep its name. */
static void test_merge_keeps_atlas_named_region(void) {
    const char *const headers[] = {
        "#define ASSET_ATLAS_REGION_MAP ((nt_hash64_t){0x0000000000000001ULL}) /* region/map */\n",
        "#define ASSET_ATLAS_REGION_HUD_ICON ((nt_hash64_t){0x0000000000000002ULL}) /* hud/icon */\n",
    };
    for (size_t i = 0; i < sizeof(s_merge_paths) / sizeof(s_merge_paths[0]); ++i) {
        FILE *f = fopen(s_merge_paths[i], "wb");
        TEST_ASSERT_NOT_NULL(f);
        const size_t len = strlen(headers[i]);
        TEST_ASSERT_EQUAL_size_t(len, fwrite(headers[i], 1U, len, f));
        TEST_ASSERT_EQUAL_INT(0, fclose(f));
    }
    nt_builder_merge_headers(s_merge_paths, 2U, s_merge_output);
    FILE *f = fopen(s_merge_output, "rb");
    TEST_ASSERT_NOT_NULL(f);
    char merged[4096] = {0};
    (void)fread(merged, 1U, sizeof(merged) - 1U, f);
    TEST_ASSERT_EQUAL_INT(0, fclose(f));
    TEST_ASSERT_NOT_NULL(strstr(merged, "#define ASSET_ATLAS_REGION_MAP "));
    TEST_ASSERT_NOT_NULL(strstr(merged, "#define ASSET_ATLAS_REGION_HUD_ICON "));
    TEST_ASSERT_NULL(strstr(merged, "REGION_REGION"));
}

/* Declared out of type and path order, so both the group order and the
 * within-group path sort have to do real work. */
static const CodegenEntry s_skeletal_entries[] = {
    {.path = "clips/hero_walk.nanm", .resource_id = 0x31, .kind = NT_BUILD_ASSET_CLIP},
    {.path = "rigs/hero.nskn", .resource_id = 0x20, .kind = NT_BUILD_ASSET_SKIN_BINDING},
    {.path = "clips/hero_run.nanm", .resource_id = 0x30, .kind = NT_BUILD_ASSET_CLIP},
    {.path = "rigs/hero.nskl", .resource_id = 0x10, .kind = NT_BUILD_ASSET_SKELETON},
};

#define CODEGEN_TMP_PATH "codegen_skeletal_defines.tmp"

static char *write_skeletal_defines(void) {
    FILE *f = fopen(CODEGEN_TMP_PATH, "wb");
    TEST_ASSERT_NOT_NULL(f);
    write_sorted_defines(f, s_skeletal_entries, (uint32_t)(sizeof(s_skeletal_entries) / sizeof(s_skeletal_entries[0])));
    (void)fclose(f);

    f = fopen(CODEGEN_TMP_PATH, "rb");
    TEST_ASSERT_NOT_NULL(f);
    (void)fseek(f, 0, SEEK_END);
    const long len = ftell(f);
    (void)fseek(f, 0, SEEK_SET);
    TEST_ASSERT_TRUE(len > 0);
    char *text = (char *)calloc((size_t)len + 1U, 1);
    TEST_ASSERT_NOT_NULL(text);
    TEST_ASSERT_EQUAL_size_t((size_t)len, fread(text, 1, (size_t)len, f));
    (void)fclose(f);
    (void)remove(CODEGEN_TMP_PATH);
    return text;
}

/* The three skeletal kinds each get their own identifier prefix and their own
 * group, emitted after the older kinds and in enum order. */
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void test_skeletal_kinds_emit_prefixed_identifiers_in_group_order(void) {
    char *text = write_skeletal_defines();

    const char *skeleton = strstr(text, "#define ASSET_SKELETON_RIGS_HERO_NSKL ");
    const char *binding = strstr(text, "#define ASSET_SKIN_BINDING_RIGS_HERO_NSKN ");
    const char *run = strstr(text, "#define ASSET_CLIP_CLIPS_HERO_RUN_NANM ");
    const char *walk = strstr(text, "#define ASSET_CLIP_CLIPS_HERO_WALK_NANM ");
    TEST_ASSERT_NOT_NULL(skeleton);
    TEST_ASSERT_NOT_NULL(binding);
    TEST_ASSERT_NOT_NULL(run);
    TEST_ASSERT_NOT_NULL(walk);
    TEST_ASSERT_TRUE_MESSAGE(skeleton < binding && binding < run, "groups follow SKELETON, SKIN_BINDING, CLIP");
    TEST_ASSERT_TRUE_MESSAGE(run < walk, "entries inside a group are sorted by path");

    TEST_ASSERT_NOT_NULL(strstr(text, "/* --- SKELETON --- */"));
    TEST_ASSERT_NOT_NULL(strstr(text, "/* --- SKIN_BINDING --- */"));
    TEST_ASSERT_NOT_NULL(strstr(text, "/* --- CLIP --- */"));

    free(text);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_collision_check_asserts_on_allocation_failure);
    RUN_TEST(test_collision_check_rejects_matching_identifiers);
    RUN_TEST(test_collision_check_accepts_distinct_identifiers);
    RUN_TEST(test_merge_rejects_identifier_collision);
    RUN_TEST(test_merge_keeps_atlas_named_region);
    RUN_TEST(test_merge_keeps_regions_sharing_a_hash);
    RUN_TEST(test_merge_keeps_longest_valid_identifier);
    RUN_TEST(test_skeletal_kinds_emit_prefixed_identifiers_in_group_order);
    return UNITY_END();
}
