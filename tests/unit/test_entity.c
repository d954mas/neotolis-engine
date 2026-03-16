#include "entity/nt_entity.h"
#include "unity.h"

/* ---- Mock component storage ---- */

static bool s_mock_has_result = false;
static bool s_mock_on_destroy_called = false;
static nt_entity_t s_mock_destroyed_entity;

static bool mock_has(nt_entity_t entity) {
    (void)entity;
    return s_mock_has_result;
}

static void mock_on_destroy(nt_entity_t entity) {
    s_mock_on_destroy_called = true;
    s_mock_destroyed_entity = entity;
}

/* ---- setUp / tearDown ---- */

void setUp(void) {
    nt_entity_init(&(nt_entity_desc_t){.max_entities = 8});
    s_mock_has_result = false;
    s_mock_on_destroy_called = false;
    s_mock_destroyed_entity = NT_ENTITY_INVALID;
}

void tearDown(void) { nt_entity_shutdown(); }

/* ---- Tests ---- */

void test_entity_create_returns_valid(void) {
    nt_entity_t e = nt_entity_create();
    TEST_ASSERT_NOT_EQUAL_UINT32(0, e.id);
}

void test_entity_create_unique_handles(void) {
    nt_entity_t a = nt_entity_create();
    nt_entity_t b = nt_entity_create();
    TEST_ASSERT_NOT_EQUAL_UINT32(a.id, b.id);
}

void test_entity_is_alive_valid(void) {
    nt_entity_t e = nt_entity_create();
    TEST_ASSERT_TRUE(nt_entity_is_alive(e));
}

void test_entity_is_alive_invalid(void) { TEST_ASSERT_FALSE(nt_entity_is_alive(NT_ENTITY_INVALID)); }

void test_entity_destroy_invalidates(void) {
    nt_entity_t e = nt_entity_create();
    nt_entity_destroy(e);
    TEST_ASSERT_FALSE(nt_entity_is_alive(e));
}

void test_entity_realloc_new_generation(void) {
    nt_entity_t first = nt_entity_create();
    uint16_t first_index = nt_entity_index(first);
    nt_entity_destroy(first);

    nt_entity_t second = nt_entity_create();
    uint16_t second_index = nt_entity_index(second);

    /* Same slot index reused */
    TEST_ASSERT_EQUAL_UINT16(first_index, second_index);
    /* Different handle id (new generation) */
    TEST_ASSERT_NOT_EQUAL_UINT32(first.id, second.id);
}

void test_entity_pool_full(void) {
    /* Create max_entities (8) entities */
    for (int i = 0; i < 8; i++) {
        nt_entity_t e = nt_entity_create();
        TEST_ASSERT_NOT_EQUAL_UINT32(0, e.id);
    }
    /* 9th creation should fail */
    nt_entity_t overflow = nt_entity_create();
    TEST_ASSERT_EQUAL_UINT32(0, overflow.id);
}

void test_entity_pool_reuse_after_destroy(void) {
    /* Fill pool */
    nt_entity_t entities[8];
    for (int i = 0; i < 8; i++) {
        entities[i] = nt_entity_create();
    }
    /* Pool full */
    TEST_ASSERT_EQUAL_UINT32(0, nt_entity_create().id);

    /* Destroy one -> next create should succeed */
    nt_entity_destroy(entities[0]);
    nt_entity_t reused = nt_entity_create();
    TEST_ASSERT_NOT_EQUAL_UINT32(0, reused.id);
}

void test_entity_enabled_default(void) {
    nt_entity_t e = nt_entity_create();
    TEST_ASSERT_TRUE(nt_entity_is_enabled(e));
}

void test_entity_set_enabled_false(void) {
    nt_entity_t e = nt_entity_create();
    nt_entity_set_enabled(e, false);
    TEST_ASSERT_FALSE(nt_entity_is_enabled(e));
}

void test_entity_set_enabled_true(void) {
    nt_entity_t e = nt_entity_create();
    nt_entity_set_enabled(e, false);
    nt_entity_set_enabled(e, true);
    TEST_ASSERT_TRUE(nt_entity_is_enabled(e));
}

void test_entity_index_extraction(void) {
    nt_entity_t e = nt_entity_create();
    uint16_t index = nt_entity_index(e);
    /* First allocation should get index >= 1 (slot 0 is reserved) */
    TEST_ASSERT_GREATER_OR_EQUAL_UINT16(1, index);
    /* Index should be the lower 16 bits */
    TEST_ASSERT_EQUAL_UINT16((uint16_t)(e.id & 0xFFFF), index);
}

void test_entity_generation_extraction(void) {
    nt_entity_t e = nt_entity_create();
    uint16_t gen = nt_entity_generation(e);
    /* First allocation has generation 1 */
    TEST_ASSERT_EQUAL_UINT16(1, gen);
    /* Generation should be the upper 16 bits */
    TEST_ASSERT_EQUAL_UINT16((uint16_t)(e.id >> 16), gen);
}

void test_entity_slot_zero_reserved(void) {
    nt_entity_t e = nt_entity_create();
    uint16_t index = nt_entity_index(e);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT16(1, index);
}

void test_entity_register_storage_and_destroy_cleanup(void) {
    nt_entity_register_storage(&(nt_comp_storage_reg_t){
        .name = "mock",
        .has = mock_has,
        .on_destroy = mock_on_destroy,
    });

    nt_entity_t e = nt_entity_create();
    s_mock_has_result = true;
    nt_entity_destroy(e);

    TEST_ASSERT_TRUE(s_mock_on_destroy_called);
    TEST_ASSERT_EQUAL_UINT32(e.id, s_mock_destroyed_entity.id);
}

/* ---- Main ---- */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_entity_create_returns_valid);
    RUN_TEST(test_entity_create_unique_handles);
    RUN_TEST(test_entity_is_alive_valid);
    RUN_TEST(test_entity_is_alive_invalid);
    RUN_TEST(test_entity_destroy_invalidates);
    RUN_TEST(test_entity_realloc_new_generation);
    RUN_TEST(test_entity_pool_full);
    RUN_TEST(test_entity_pool_reuse_after_destroy);
    RUN_TEST(test_entity_enabled_default);
    RUN_TEST(test_entity_set_enabled_false);
    RUN_TEST(test_entity_set_enabled_true);
    RUN_TEST(test_entity_index_extraction);
    RUN_TEST(test_entity_generation_extraction);
    RUN_TEST(test_entity_slot_zero_reserved);
    RUN_TEST(test_entity_register_storage_and_destroy_cleanup);
    return UNITY_END();
}
