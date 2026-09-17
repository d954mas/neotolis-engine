#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include "nt_builder_internal.h"
#include "unity.h"

static bool s_fail_allocation;
static jmp_buf s_assert_jmp;
static const char *s_assert_expr;

static void *test_calloc(size_t count, size_t size) { return s_fail_allocation ? NULL : calloc(count, size); }

/* Inject allocation failure without changing the builder's allocator. */
#define calloc test_calloc
// NOLINTNEXTLINE(bugprone-suspicious-include)
#include "nt_builder_codegen.c"
#undef calloc

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

void tearDown(void) { nt_build_assert_handler = NULL; }

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
    RUN_TEST(test_skeletal_kinds_emit_prefixed_identifiers_in_group_order);
    return UNITY_END();
}
