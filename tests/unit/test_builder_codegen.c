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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_collision_check_asserts_on_allocation_failure);
    RUN_TEST(test_collision_check_rejects_matching_identifiers);
    RUN_TEST(test_collision_check_accepts_distinct_identifiers);
    return UNITY_END();
}
