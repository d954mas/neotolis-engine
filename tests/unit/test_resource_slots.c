#include <stdint.h>

#include "nt_pack_format.h"
#include "renderers/nt_sprite_renderer.h"
#include "resource/nt_resource.h"
#include "test_helpers/nt_assert_trap.h"
#include "unity.h"

static uint32_t s_published[2];
static uint32_t s_published_count;

void setUp(void) {
    const nt_resource_desc_t desc = {0};
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_init(&desc));
    s_published_count = 0;
}

void tearDown(void) { nt_resource_shutdown(); }

static void record_publication(const uint8_t *data, uint32_t size, nt_resource_t handle, uint32_t runtime_handle, void *user_data) {
    (void)data;
    (void)size;
    (void)runtime_handle;
    (void)user_data;
    TEST_ASSERT_LESS_THAN_UINT32(2, s_published_count);
    s_published[s_published_count++] = handle.id;
}

static void test_slots_cross_16_bit_boundary_and_survive_unmount(void) {
    const nt_hash32_t pack = {1};
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_create_pack(pack, 0));
    for (uint32_t i = 1; i <= NT_RESOURCE_MAX_SLOTS; i++) {
        nt_resource_t handle = nt_resource_request((nt_hash64_t){i}, NT_ASSET_MESH);
        TEST_ASSERT_EQUAL_UINT32(i, handle.id);
    }

    const nt_resource_t low = nt_resource_find((nt_hash64_t){1});
    const nt_resource_t high = nt_resource_find((nt_hash64_t){NT_RESOURCE_MAX_SLOTS});
    TEST_ASSERT_EQUAL_UINT32(65535, nt_resource_find((nt_hash64_t){65535}).id);
#if NT_RESOURCE_MAX_SLOTS > 65535
    TEST_ASSERT_EQUAL_UINT32(65536, nt_resource_find((nt_hash64_t){65536}).id);
#endif
    TEST_ASSERT_EQUAL_UINT32(NT_RESOURCE_MAX_SLOTS, high.id);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(pack, (nt_hash64_t){1}, NT_ASSET_MESH, 101));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register(pack, (nt_hash64_t){NT_RESOURCE_MAX_SLOTS}, NT_ASSET_MESH, 202));
    nt_resource_set_post_resolve_callback(NT_ASSET_MESH, record_publication);
    nt_resource_step();
    TEST_ASSERT_TRUE(nt_resource_is_ready(high));
    TEST_ASSERT_EQUAL_UINT32(101, nt_resource_get(low));
    TEST_ASSERT_EQUAL_UINT32(202, nt_resource_get(high));
    TEST_ASSERT_EQUAL_UINT64(NT_RESOURCE_MAX_SLOTS, nt_resource_source_of(NT_ASSET_MESH, 202));
    TEST_ASSERT_EQUAL_UINT32(2, s_published_count);
    TEST_ASSERT_EQUAL_UINT32(low.id, s_published[0]);
    TEST_ASSERT_EQUAL_UINT32(high.id, s_published[1]);
    NT_TEST_EXPECT_ASSERT(nt_resource_request((nt_hash64_t){NT_RESOURCE_MAX_SLOTS + 1U}, NT_ASSET_MESH));

    nt_resource_unmount(pack);
    nt_resource_step();
    TEST_ASSERT_FALSE(nt_resource_is_ready(low));
    TEST_ASSERT_FALSE(nt_resource_is_ready(high));
    TEST_ASSERT_EQUAL_UINT32(high.id, nt_resource_request((nt_hash64_t){NT_RESOURCE_MAX_SLOTS}, NT_ASSET_MESH).id);
    NT_TEST_EXPECT_ASSERT(nt_resource_request((nt_hash64_t){NT_RESOURCE_MAX_SLOTS + 1U}, NT_ASSET_MESH));
}

static void test_slot_map_collision_wraps_without_aliasing_names(void) {
    const uint64_t buckets = (uint64_t)NT_RESOURCE_MAX_SLOTS * 2U;
    for (uint32_t i = 0; i < 3; i++) {
        nt_hash64_t rid = {buckets - 1U + (i * buckets)};
        TEST_ASSERT_EQUAL_UINT32(i + 1U, nt_resource_request(rid, NT_ASSET_MESH).id);
    }
    for (uint32_t i = 0; i < 3; i++) {
        nt_hash64_t rid = {buckets - 1U + (i * buckets)};
        TEST_ASSERT_EQUAL_UINT32(i + 1U, nt_resource_find(rid).id);
        TEST_ASSERT_EQUAL_UINT32(i + 1U, nt_resource_request(rid, NT_ASSET_MESH).id);
    }
    TEST_ASSERT_EQUAL_UINT32(0, nt_resource_find((nt_hash64_t){buckets - 1U + (3U * buckets)}).id);
}

static void test_zero_resource_id_is_rejected_before_allocation(void) {
    NT_TEST_EXPECT_ASSERT(nt_resource_request((nt_hash64_t){0}, NT_ASSET_MESH));
    TEST_ASSERT_EQUAL_UINT32(1, nt_resource_request((nt_hash64_t){1}, NT_ASSET_MESH).id);
}

static void test_shutdown_without_requests(void) { nt_resource_step(); }

static void request_dependent_page(const uint8_t *data, uint32_t size, nt_resource_t handle, uint32_t runtime_handle, void *user_data) {
    record_publication(data, size, handle, runtime_handle, user_data);
    if (handle.id == 1) {
        TEST_ASSERT_EQUAL_UINT32(NT_RESOURCE_MAX_SLOTS, nt_resource_request((nt_hash64_t){NT_RESOURCE_MAX_SLOTS}, NT_ASSET_MESH).id);
    }
}

static void test_post_resolve_allocates_last_slot(void) {
    for (uint32_t i = 1; i < NT_RESOURCE_MAX_SLOTS; i++) {
        nt_resource_request((nt_hash64_t){i}, NT_ASSET_MESH);
    }
    nt_resource_set_post_resolve_callback(NT_ASSET_MESH, request_dependent_page);
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_create_pack((nt_hash32_t){1}, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register((nt_hash32_t){1}, (nt_hash64_t){1}, NT_ASSET_MESH, 101));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register((nt_hash32_t){1}, (nt_hash64_t){NT_RESOURCE_MAX_SLOTS}, NT_ASSET_MESH, 202));
    nt_resource_step();
    TEST_ASSERT_EQUAL_UINT32(2, s_published_count);
    TEST_ASSERT_EQUAL_UINT32(NT_RESOURCE_MAX_SLOTS, s_published[1]);
    TEST_ASSERT_EQUAL_UINT32(202, nt_resource_get(nt_resource_find((nt_hash64_t){NT_RESOURCE_MAX_SLOTS})));
}

#if NT_RESOURCE_MAX_SLOTS > 65536
static void test_sprite_key_uses_texture_not_large_resource_index(void) {
    for (uint32_t i = 1; i <= NT_RESOURCE_MAX_SLOTS; i++) {
        nt_resource_request((nt_hash64_t){i}, NT_ASSET_TEXTURE);
    }
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_create_pack((nt_hash32_t){1}, 0));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register((nt_hash32_t){1}, (nt_hash64_t){1}, NT_ASSET_TEXTURE, 0x10001));
    TEST_ASSERT_EQUAL(NT_OK, nt_resource_register((nt_hash32_t){1}, (nt_hash64_t){65537}, NT_ASSET_TEXTURE, 0x10002));
    nt_resource_step();
    const nt_material_t material = {.id = 0x10002};
    TEST_ASSERT_EQUAL_HEX32(0x20001, nt_sprite_renderer_batch_key(material, nt_resource_find((nt_hash64_t){1})));
    TEST_ASSERT_EQUAL_HEX32(0x20002, nt_sprite_renderer_batch_key(material, nt_resource_find((nt_hash64_t){65537})));
}
#endif

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_slots_cross_16_bit_boundary_and_survive_unmount);
    RUN_TEST(test_slot_map_collision_wraps_without_aliasing_names);
    RUN_TEST(test_zero_resource_id_is_rejected_before_allocation);
    RUN_TEST(test_shutdown_without_requests);
    RUN_TEST(test_post_resolve_allocates_last_slot);
#if NT_RESOURCE_MAX_SLOTS > 65536
    RUN_TEST(test_sprite_key_uses_texture_not_large_resource_index);
#endif
    return UNITY_END();
}
