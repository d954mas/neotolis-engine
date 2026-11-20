#include <string.h>

#include "blob/nt_blob.h"
#include "test_helpers/nt_assert_trap.h"
#include "unity.h"

void setUp(void) {
    nt_test_assert_install();
    nt_blob_init(2);
}

void tearDown(void) { nt_blob_shutdown(); }

static void test_owned_bytes_and_stale_handle(void) {
    uint8_t input[] = {1, 2, 3};
    nt_blob_t blob = nt_blob_create(input, sizeof input);
    memset(input, 0, sizeof input);
    nt_blob_view_t view = nt_blob_read(blob);
    TEST_ASSERT_EQUAL_UINT32(3, view.size);
    TEST_ASSERT_EQUAL_UINT8(1, view.data[0]);
    nt_blob_destroy(blob);
    nt_blob_t next = nt_blob_create(NULL, 0);
    TEST_ASSERT_NOT_EQUAL_UINT32(blob.id, next.id);
    NT_TEST_EXPECT_ASSERT((void)nt_blob_read(blob));
    NT_TEST_EXPECT_ASSERT(nt_blob_destroy(blob));
    view = nt_blob_read(next);
    TEST_ASSERT_NULL(view.data);
    TEST_ASSERT_EQUAL_UINT32(0, view.size);
    nt_blob_destroy(next);
    nt_blob_destroy((nt_blob_t){0});
}

static void test_capacity_asserts(void) {
    (void)nt_blob_create(NULL, 0);
    (void)nt_blob_create(NULL, 0);
    NT_TEST_EXPECT_ASSERT((void)nt_blob_create(NULL, 0));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_owned_bytes_and_stale_handle);
    RUN_TEST(test_capacity_asserts);
    return UNITY_END();
}
