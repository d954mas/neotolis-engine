#include "nt_crc32.h"
#include "nt_mesh_format.h"
#include "nt_pack_format.h"
#include "nt_shader_format.h"
#include "nt_texture_format.h"
#include "unity.h"

#include <stddef.h>
#include <string.h>

void setUp(void) {}

void tearDown(void) {}

/* --- Struct size tests --- */

void test_pack_header_size(void) { TEST_ASSERT_EQUAL_UINT(24, sizeof(NtPackHeader)); }

void test_asset_entry_size(void) { TEST_ASSERT_EQUAL_UINT(16, sizeof(NtAssetEntry)); }

/* --- Constants --- */

void test_pack_magic_value(void) {
    TEST_ASSERT_EQUAL_HEX32(0x4B41504E, NT_PACK_MAGIC);

    /* Verify the bytes spell "NPAK" in little-endian */
    uint32_t magic = NT_PACK_MAGIC;
    const uint8_t *bytes = (const uint8_t *)&magic;
    TEST_ASSERT_EQUAL_UINT8('N', bytes[0]);
    TEST_ASSERT_EQUAL_UINT8('P', bytes[1]);
    TEST_ASSERT_EQUAL_UINT8('A', bytes[2]);
    TEST_ASSERT_EQUAL_UINT8('K', bytes[3]);
}

void test_pack_version(void) { TEST_ASSERT_EQUAL_UINT(1, NT_PACK_VERSION); }

void test_pack_align(void) { TEST_ASSERT_EQUAL_UINT(4, NT_PACK_ALIGN); }

/* --- Alignment macro --- */

void test_align_up_macro(void) {
    TEST_ASSERT_EQUAL_UINT32(0, NT_PACK_ALIGN_UP(0));
    TEST_ASSERT_EQUAL_UINT32(4, NT_PACK_ALIGN_UP(1));
    TEST_ASSERT_EQUAL_UINT32(4, NT_PACK_ALIGN_UP(2));
    TEST_ASSERT_EQUAL_UINT32(4, NT_PACK_ALIGN_UP(3));
    TEST_ASSERT_EQUAL_UINT32(4, NT_PACK_ALIGN_UP(4));
    TEST_ASSERT_EQUAL_UINT32(8, NT_PACK_ALIGN_UP(5));
    TEST_ASSERT_EQUAL_UINT32(16, NT_PACK_ALIGN_UP(13));
    TEST_ASSERT_EQUAL_UINT32(100, NT_PACK_ALIGN_UP(100));
}

/* --- Field offset tests --- */

void test_pack_header_field_offsets(void) {
    TEST_ASSERT_EQUAL_UINT(0, offsetof(NtPackHeader, magic));
    TEST_ASSERT_EQUAL_UINT(4, offsetof(NtPackHeader, version));
    TEST_ASSERT_EQUAL_UINT(6, offsetof(NtPackHeader, pack_id));
    TEST_ASSERT_EQUAL_UINT(10, offsetof(NtPackHeader, asset_count));
    TEST_ASSERT_EQUAL_UINT(12, offsetof(NtPackHeader, header_size));
    TEST_ASSERT_EQUAL_UINT(16, offsetof(NtPackHeader, total_size));
    TEST_ASSERT_EQUAL_UINT(20, offsetof(NtPackHeader, checksum));
}

void test_asset_entry_field_offsets(void) {
    TEST_ASSERT_EQUAL_UINT(0, offsetof(NtAssetEntry, resource_id));
    TEST_ASSERT_EQUAL_UINT(4, offsetof(NtAssetEntry, asset_type));
    TEST_ASSERT_EQUAL_UINT(5, offsetof(NtAssetEntry, format_version));
    TEST_ASSERT_EQUAL_UINT(7, offsetof(NtAssetEntry, _pad));
    TEST_ASSERT_EQUAL_UINT(8, offsetof(NtAssetEntry, offset));
    TEST_ASSERT_EQUAL_UINT(12, offsetof(NtAssetEntry, size));
}

/* --- Asset type enum --- */

void test_asset_type_enum_values(void) {
    TEST_ASSERT_EQUAL_UINT(1, NT_ASSET_MESH);
    TEST_ASSERT_EQUAL_UINT(2, NT_ASSET_TEXTURE);
    TEST_ASSERT_EQUAL_UINT(3, NT_ASSET_SHADER);
}

/* --- CRC32 tests --- */

void test_crc32_empty_input(void) { TEST_ASSERT_EQUAL_HEX32(0, nt_crc32(NULL, 0)); }

void test_crc32_ieee_check_value(void) {
    /* Standard IEEE 802.3 check value: CRC32("123456789") == 0xCBF43926 */
    const uint8_t data[] = "123456789";
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926, nt_crc32(data, 9));
}

void test_crc32_all_zeros(void) {
    /* CRC32 of 4 zero bytes = 0x2144DF1C */
    const uint8_t zeros[4] = {0, 0, 0, 0};
    TEST_ASSERT_EQUAL_HEX32(0x2144DF1C, nt_crc32(zeros, 4));
}

void test_crc32_single_byte(void) {
    /* CRC32 of single byte 0xFF = 0xFF000000 */
    const uint8_t byte = 0xFF;
    TEST_ASSERT_EQUAL_HEX32(0xFF000000, nt_crc32(&byte, 1));
}

void test_crc32_deterministic(void) {
    /* Same input produces same output on repeated calls */
    const uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint32_t first = nt_crc32(data, 4);
    uint32_t second = nt_crc32(data, 4);
    TEST_ASSERT_EQUAL_HEX32(first, second);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_pack_header_size);
    RUN_TEST(test_asset_entry_size);
    RUN_TEST(test_pack_magic_value);
    RUN_TEST(test_pack_version);
    RUN_TEST(test_pack_align);
    RUN_TEST(test_align_up_macro);
    RUN_TEST(test_pack_header_field_offsets);
    RUN_TEST(test_asset_entry_field_offsets);
    RUN_TEST(test_asset_type_enum_values);
    RUN_TEST(test_crc32_empty_input);
    RUN_TEST(test_crc32_ieee_check_value);
    RUN_TEST(test_crc32_all_zeros);
    RUN_TEST(test_crc32_single_byte);
    RUN_TEST(test_crc32_deterministic);
    return UNITY_END();
}
