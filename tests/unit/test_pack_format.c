#include "nt_blob_format.h"
#include "nt_crc32.h"
#include "nt_font_format.h"
#include "nt_mesh_format.h"
#include "nt_pack_format.h"
#include "nt_shader_format.h"
#include "nt_texture_format.h"
#include "unity.h"

#include <stddef.h>
#include <string.h>

void setUp(void) {}

void tearDown(void) {}

/* --- Pack header struct size tests --- */

void test_pack_header_size(void) { TEST_ASSERT_EQUAL_UINT(32, sizeof(NtPackHeader)); }

void test_asset_entry_size(void) { TEST_ASSERT_EQUAL_UINT(24, sizeof(NtAssetEntry)); }

/* --- Pack header constants --- */

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

void test_pack_version(void) { TEST_ASSERT_EQUAL_UINT(2, NT_PACK_VERSION); }

void test_pack_align(void) {
    TEST_ASSERT_EQUAL_UINT(4, NT_PACK_ASSET_ALIGN);
    TEST_ASSERT_EQUAL_UINT(8, NT_PACK_DATA_ALIGN);
}

/* --- Alignment macro --- */

void test_align_up_macro(void) {
    /* 4-byte alignment */
    TEST_ASSERT_EQUAL_UINT32(0, NT_PACK_ALIGN_UP(0, 4));
    TEST_ASSERT_EQUAL_UINT32(4, NT_PACK_ALIGN_UP(1, 4));
    TEST_ASSERT_EQUAL_UINT32(4, NT_PACK_ALIGN_UP(4, 4));
    TEST_ASSERT_EQUAL_UINT32(8, NT_PACK_ALIGN_UP(5, 4));
    TEST_ASSERT_EQUAL_UINT32(100, NT_PACK_ALIGN_UP(100, 4));
    /* 8-byte alignment */
    TEST_ASSERT_EQUAL_UINT32(0, NT_PACK_ALIGN_UP(0, 8));
    TEST_ASSERT_EQUAL_UINT32(8, NT_PACK_ALIGN_UP(1, 8));
    TEST_ASSERT_EQUAL_UINT32(8, NT_PACK_ALIGN_UP(8, 8));
    TEST_ASSERT_EQUAL_UINT32(16, NT_PACK_ALIGN_UP(9, 8));
    TEST_ASSERT_EQUAL_UINT32(24, NT_PACK_ALIGN_UP(24, 8));
}

/* --- Pack header field offset tests --- */

void test_pack_header_field_offsets(void) {
    TEST_ASSERT_EQUAL_UINT(0, offsetof(NtPackHeader, magic));
    TEST_ASSERT_EQUAL_UINT(4, offsetof(NtPackHeader, meta_count));
    TEST_ASSERT_EQUAL_UINT(8, offsetof(NtPackHeader, version));
    TEST_ASSERT_EQUAL_UINT(10, offsetof(NtPackHeader, asset_count));
    TEST_ASSERT_EQUAL_UINT(12, offsetof(NtPackHeader, header_size));
    TEST_ASSERT_EQUAL_UINT(16, offsetof(NtPackHeader, total_size));
    TEST_ASSERT_EQUAL_UINT(20, offsetof(NtPackHeader, checksum));
    TEST_ASSERT_EQUAL_UINT(24, offsetof(NtPackHeader, meta_offset));
}

void test_asset_entry_field_offsets(void) {
    TEST_ASSERT_EQUAL_UINT(0, offsetof(NtAssetEntry, resource_id));
    TEST_ASSERT_EQUAL_UINT(8, offsetof(NtAssetEntry, offset));
    TEST_ASSERT_EQUAL_UINT(12, offsetof(NtAssetEntry, size));
    TEST_ASSERT_EQUAL_UINT(16, offsetof(NtAssetEntry, format_version));
    TEST_ASSERT_EQUAL_UINT(18, offsetof(NtAssetEntry, asset_type));
    TEST_ASSERT_EQUAL_UINT(19, offsetof(NtAssetEntry, _pad));
    TEST_ASSERT_EQUAL_UINT(20, offsetof(NtAssetEntry, meta_offset));
}

/* --- Asset type enum --- */

void test_asset_type_enum_values(void) {
    TEST_ASSERT_EQUAL_UINT(1, NT_ASSET_MESH);
    TEST_ASSERT_EQUAL_UINT(2, NT_ASSET_TEXTURE);
    TEST_ASSERT_EQUAL_UINT(3, NT_ASSET_SHADER_CODE);
    TEST_ASSERT_EQUAL_UINT(4, NT_ASSET_BLOB);
    TEST_ASSERT_EQUAL_UINT(5, NT_ASSET_FONT);
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

/* --- Stream descriptor tests --- */

void test_stream_desc_size(void) { TEST_ASSERT_EQUAL_UINT(8, sizeof(NtStreamDesc)); }

void test_stream_desc_field_offsets(void) {
    TEST_ASSERT_EQUAL_UINT(0, offsetof(NtStreamDesc, name_hash));
    TEST_ASSERT_EQUAL_UINT(4, offsetof(NtStreamDesc, type));
    TEST_ASSERT_EQUAL_UINT(5, offsetof(NtStreamDesc, count));
    TEST_ASSERT_EQUAL_UINT(6, offsetof(NtStreamDesc, normalized));
}

void test_stream_type_sizes(void) {
    TEST_ASSERT_EQUAL_UINT32(1, nt_stream_type_size(NT_STREAM_UINT8));
    TEST_ASSERT_EQUAL_UINT32(1, nt_stream_type_size(NT_STREAM_INT8));
    TEST_ASSERT_EQUAL_UINT32(2, nt_stream_type_size(NT_STREAM_UINT16));
    TEST_ASSERT_EQUAL_UINT32(2, nt_stream_type_size(NT_STREAM_INT16));
    TEST_ASSERT_EQUAL_UINT32(2, nt_stream_type_size(NT_STREAM_FLOAT16));
    TEST_ASSERT_EQUAL_UINT32(4, nt_stream_type_size(NT_STREAM_FLOAT32));
    TEST_ASSERT_EQUAL_UINT32(0, nt_stream_type_size(99)); /* unknown type */
}

void test_stream_stride_calculation(void) {
    /* Typical mesh: position(float32x3) + normal(float32x3) + uv(float32x2) */
    NtStreamDesc streams[3] = {
        {0xAABBCCDD, NT_STREAM_FLOAT32, 3, 0, 0},
        {0x11223344, NT_STREAM_FLOAT32, 3, 0, 0},
        {0x55667788, NT_STREAM_FLOAT32, 2, 0, 0},
    };
    uint32_t stride = 0;
    for (int i = 0; i < 3; i++) {
        stride += nt_stream_type_size(streams[i].type) * streams[i].count;
    }
    TEST_ASSERT_EQUAL_UINT32(32, stride); /* 12 + 12 + 8 */
}

void test_stream_stride_mixed_precision(void) {
    /* Compact mesh: position(float32x3) + normal(float16x3) + uv(uint16x2) */
    NtStreamDesc streams[3] = {
        {0xAABBCCDD, NT_STREAM_FLOAT32, 3, 0, 0},
        {0x11223344, NT_STREAM_FLOAT16, 3, 0, 0},
        {0x55667788, NT_STREAM_UINT16, 2, 0, 0},
    };
    uint32_t stride = 0;
    for (int i = 0; i < 3; i++) {
        stride += nt_stream_type_size(streams[i].type) * streams[i].count;
    }
    TEST_ASSERT_EQUAL_UINT32(22, stride); /* 12 + 6 + 4 */
}

/* --- Mesh header tests --- */

void test_mesh_header_size(void) { TEST_ASSERT_EQUAL_UINT(48, sizeof(NtMeshAssetHeader)); }

void test_mesh_magic_value(void) {
    TEST_ASSERT_EQUAL_HEX32(0x4853454D, NT_MESH_MAGIC);

    /* Verify the bytes spell "MESH" in little-endian */
    uint32_t magic = NT_MESH_MAGIC;
    const uint8_t *b = (const uint8_t *)&magic;
    TEST_ASSERT_EQUAL_UINT8('M', b[0]);
    TEST_ASSERT_EQUAL_UINT8('E', b[1]);
    TEST_ASSERT_EQUAL_UINT8('S', b[2]);
    TEST_ASSERT_EQUAL_UINT8('H', b[3]);
}

void test_mesh_header_field_offsets(void) {
    TEST_ASSERT_EQUAL_UINT(0, offsetof(NtMeshAssetHeader, magic));
    TEST_ASSERT_EQUAL_UINT(4, offsetof(NtMeshAssetHeader, version));
    TEST_ASSERT_EQUAL_UINT(6, offsetof(NtMeshAssetHeader, stream_count));
    TEST_ASSERT_EQUAL_UINT(7, offsetof(NtMeshAssetHeader, index_type));
    TEST_ASSERT_EQUAL_UINT(8, offsetof(NtMeshAssetHeader, vertex_count));
    TEST_ASSERT_EQUAL_UINT(12, offsetof(NtMeshAssetHeader, index_count));
    TEST_ASSERT_EQUAL_UINT(16, offsetof(NtMeshAssetHeader, vertex_data_size));
    TEST_ASSERT_EQUAL_UINT(20, offsetof(NtMeshAssetHeader, index_data_size));
    TEST_ASSERT_EQUAL_UINT(24, offsetof(NtMeshAssetHeader, aabb_min));
    TEST_ASSERT_EQUAL_UINT(36, offsetof(NtMeshAssetHeader, aabb_max));
}

/* --- Texture header tests --- */

void test_texture_header_size(void) { TEST_ASSERT_EQUAL_UINT(24, sizeof(NtTextureAssetHeaderV2)); }

void test_texture_magic_value(void) {
    TEST_ASSERT_EQUAL_HEX32(0x58455454, NT_TEXTURE_MAGIC);

    /* Verify the bytes spell "TTEX" in little-endian */
    uint32_t magic = NT_TEXTURE_MAGIC;
    const uint8_t *b = (const uint8_t *)&magic;
    TEST_ASSERT_EQUAL_UINT8('T', b[0]);
    TEST_ASSERT_EQUAL_UINT8('T', b[1]);
    TEST_ASSERT_EQUAL_UINT8('E', b[2]);
    TEST_ASSERT_EQUAL_UINT8('X', b[3]);
}

void test_texture_header_field_offsets(void) {
    TEST_ASSERT_EQUAL_UINT(0, offsetof(NtTextureAssetHeaderV2, magic));
    TEST_ASSERT_EQUAL_UINT(4, offsetof(NtTextureAssetHeaderV2, version));
    TEST_ASSERT_EQUAL_UINT(6, offsetof(NtTextureAssetHeaderV2, format));
    TEST_ASSERT_EQUAL_UINT(8, offsetof(NtTextureAssetHeaderV2, width));
    TEST_ASSERT_EQUAL_UINT(12, offsetof(NtTextureAssetHeaderV2, height));
    TEST_ASSERT_EQUAL_UINT(16, offsetof(NtTextureAssetHeaderV2, mip_count));
    TEST_ASSERT_EQUAL_UINT(18, offsetof(NtTextureAssetHeaderV2, compression));
    TEST_ASSERT_EQUAL_UINT(19, offsetof(NtTextureAssetHeaderV2, _pad));
    TEST_ASSERT_EQUAL_UINT(20, offsetof(NtTextureAssetHeaderV2, data_size));
}

void test_texture_format_enum(void) { TEST_ASSERT_EQUAL_UINT(1, NT_TEXTURE_FORMAT_RGBA8); }

/* --- Metadata struct tests --- */

void test_NtMetaEntryHeader_size(void) { TEST_ASSERT_EQUAL_UINT(20, sizeof(NtMetaEntryHeader)); }

void test_NtMetaEntryHeader_field_offsets(void) {
    TEST_ASSERT_EQUAL_UINT(0, offsetof(NtMetaEntryHeader, resource_id));
    TEST_ASSERT_EQUAL_UINT(8, offsetof(NtMetaEntryHeader, kind));
    TEST_ASSERT_EQUAL_UINT(16, offsetof(NtMetaEntryHeader, size));
}

void test_NtPackHeader_meta_count_offset(void) {
    /* meta_count is at byte offset 4 */
    TEST_ASSERT_EQUAL_UINT(4, offsetof(NtPackHeader, meta_count));
    NtPackHeader h;
    memset(&h, 0, sizeof(h));
    h.meta_count = 42;
    uint8_t *bytes = (uint8_t *)&h;
    uint32_t val;
    memcpy(&val, bytes + 4, sizeof(val));
    TEST_ASSERT_EQUAL_UINT32(42, val);
}

void test_NtAssetEntry_meta_offset_offset(void) {
    /* meta_offset is at byte offset 20 */
    TEST_ASSERT_EQUAL_UINT(20, offsetof(NtAssetEntry, meta_offset));
    NtAssetEntry e;
    memset(&e, 0, sizeof(e));
    e.meta_offset = 1234;
    uint8_t *bytes = (uint8_t *)&e;
    uint32_t val;
    memcpy(&val, bytes + 20, sizeof(val));
    TEST_ASSERT_EQUAL_UINT32(1234, val);
}

/* --- Shader code header tests --- */

void test_shader_code_header_size(void) { TEST_ASSERT_EQUAL_UINT(12, sizeof(NtShaderCodeHeader)); }

void test_shader_code_magic_value(void) {
    TEST_ASSERT_EQUAL_HEX32(0x43444853, NT_SHADER_CODE_MAGIC);

    uint32_t magic = NT_SHADER_CODE_MAGIC;
    const uint8_t *b = (const uint8_t *)&magic;
    TEST_ASSERT_EQUAL_UINT8('S', b[0]);
    TEST_ASSERT_EQUAL_UINT8('H', b[1]);
    TEST_ASSERT_EQUAL_UINT8('D', b[2]);
    TEST_ASSERT_EQUAL_UINT8('C', b[3]);
}

void test_shader_code_header_field_offsets(void) {
    TEST_ASSERT_EQUAL_UINT(0, offsetof(NtShaderCodeHeader, magic));
    TEST_ASSERT_EQUAL_UINT(4, offsetof(NtShaderCodeHeader, version));
    TEST_ASSERT_EQUAL_UINT(6, offsetof(NtShaderCodeHeader, stage));
    TEST_ASSERT_EQUAL_UINT(8, offsetof(NtShaderCodeHeader, code_size));
}

void test_shader_stage_enum(void) {
    TEST_ASSERT_EQUAL_UINT(0, NT_SHADER_STAGE_VERTEX);
    TEST_ASSERT_EQUAL_UINT(1, NT_SHADER_STAGE_FRAGMENT);
}

/* --- Blob format tests --- */

void test_blob_header_size(void) { TEST_ASSERT_EQUAL_UINT(8, sizeof(NtBlobAssetHeader)); }

void test_blob_magic_value(void) {
    TEST_ASSERT_EQUAL_HEX32(0x424F4C42, NT_BLOB_MAGIC);
    uint32_t magic = NT_BLOB_MAGIC;
    const uint8_t *b = (const uint8_t *)&magic;
    TEST_ASSERT_EQUAL_UINT8('B', b[0]);
    TEST_ASSERT_EQUAL_UINT8('L', b[1]);
    TEST_ASSERT_EQUAL_UINT8('O', b[2]);
    TEST_ASSERT_EQUAL_UINT8('B', b[3]);
}

/* --- Font format tests --- */

void test_font_asset_header_size(void) { TEST_ASSERT_EQUAL_UINT(16, sizeof(NtFontAssetHeader)); }

void test_font_magic_value(void) {
    TEST_ASSERT_EQUAL_HEX32(0x544E4F46, NT_FONT_MAGIC);
    uint32_t magic = NT_FONT_MAGIC;
    const uint8_t *b = (const uint8_t *)&magic;
    TEST_ASSERT_EQUAL_UINT8('F', b[0]);
    TEST_ASSERT_EQUAL_UINT8('O', b[1]);
    TEST_ASSERT_EQUAL_UINT8('N', b[2]);
    TEST_ASSERT_EQUAL_UINT8('T', b[3]);
}

void test_font_version(void) { TEST_ASSERT_EQUAL_UINT(1, NT_FONT_VERSION); }

void test_font_asset_header_field_offsets(void) {
    TEST_ASSERT_EQUAL_UINT(0, offsetof(NtFontAssetHeader, magic));
    TEST_ASSERT_EQUAL_UINT(4, offsetof(NtFontAssetHeader, version));
    TEST_ASSERT_EQUAL_UINT(6, offsetof(NtFontAssetHeader, glyph_count));
    TEST_ASSERT_EQUAL_UINT(8, offsetof(NtFontAssetHeader, units_per_em));
    TEST_ASSERT_EQUAL_UINT(10, offsetof(NtFontAssetHeader, ascent));
    TEST_ASSERT_EQUAL_UINT(12, offsetof(NtFontAssetHeader, descent));
    TEST_ASSERT_EQUAL_UINT(14, offsetof(NtFontAssetHeader, line_gap));
}

void test_font_glyph_entry_size(void) { TEST_ASSERT_EQUAL_UINT(24, sizeof(NtFontGlyphEntry)); }

void test_font_glyph_entry_field_offsets(void) {
    TEST_ASSERT_EQUAL_UINT(0, offsetof(NtFontGlyphEntry, codepoint));
    TEST_ASSERT_EQUAL_UINT(4, offsetof(NtFontGlyphEntry, data_offset));
    TEST_ASSERT_EQUAL_UINT(8, offsetof(NtFontGlyphEntry, advance));
    TEST_ASSERT_EQUAL_UINT(10, offsetof(NtFontGlyphEntry, bbox_x0));
    TEST_ASSERT_EQUAL_UINT(12, offsetof(NtFontGlyphEntry, bbox_y0));
    TEST_ASSERT_EQUAL_UINT(14, offsetof(NtFontGlyphEntry, bbox_x1));
    TEST_ASSERT_EQUAL_UINT(16, offsetof(NtFontGlyphEntry, bbox_y1));
    TEST_ASSERT_EQUAL_UINT(18, offsetof(NtFontGlyphEntry, curve_count));
    TEST_ASSERT_EQUAL_UINT(20, offsetof(NtFontGlyphEntry, kern_count));
    TEST_ASSERT_EQUAL_UINT(21, offsetof(NtFontGlyphEntry, band_count));
    TEST_ASSERT_EQUAL_UINT(22, offsetof(NtFontGlyphEntry, index_count));
}

void test_font_kern_entry_size(void) { TEST_ASSERT_EQUAL_UINT(8, sizeof(NtFontKernEntry)); }

void test_font_kern_entry_field_offsets(void) {
    TEST_ASSERT_EQUAL_UINT(0, offsetof(NtFontKernEntry, right_codepoint));
    TEST_ASSERT_EQUAL_UINT(4, offsetof(NtFontKernEntry, value));
    TEST_ASSERT_EQUAL_UINT(6, offsetof(NtFontKernEntry, _pad));
}

void test_font_curve_size(void) { TEST_ASSERT_EQUAL_UINT(12, sizeof(NtFontCurve)); }

void test_font_curve_field_offsets(void) {
    TEST_ASSERT_EQUAL_UINT(0, offsetof(NtFontCurve, p0x));
    TEST_ASSERT_EQUAL_UINT(2, offsetof(NtFontCurve, p0y));
    TEST_ASSERT_EQUAL_UINT(4, offsetof(NtFontCurve, p1x));
    TEST_ASSERT_EQUAL_UINT(6, offsetof(NtFontCurve, p1y));
    TEST_ASSERT_EQUAL_UINT(8, offsetof(NtFontCurve, p2x));
    TEST_ASSERT_EQUAL_UINT(10, offsetof(NtFontCurve, p2y));
}

void test_font_band_size(void) { TEST_ASSERT_EQUAL_UINT(4, sizeof(NtFontBand)); }

void test_font_band_field_offsets(void) {
    TEST_ASSERT_EQUAL_UINT(0, offsetof(NtFontBand, curve_start));
    TEST_ASSERT_EQUAL_UINT(2, offsetof(NtFontBand, curve_count));
}

void test_font_glyph_array_offset(void) {
    /* Glyph entries start immediately after header (D-11) */
    TEST_ASSERT_EQUAL_UINT(16, sizeof(NtFontAssetHeader));
    /* 16 is 4-byte aligned, safe for NtFontGlyphEntry array access */
    TEST_ASSERT_EQUAL_UINT(0, sizeof(NtFontAssetHeader) % 4);
}

int main(void) {
    UNITY_BEGIN();

    /* Pack format tests (Plan 01) */
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

    /* Stream descriptor tests */
    RUN_TEST(test_stream_desc_size);
    RUN_TEST(test_stream_desc_field_offsets);
    RUN_TEST(test_stream_type_sizes);
    RUN_TEST(test_stream_stride_calculation);
    RUN_TEST(test_stream_stride_mixed_precision);

    /* Mesh header tests */
    RUN_TEST(test_mesh_header_size);
    RUN_TEST(test_mesh_magic_value);
    RUN_TEST(test_mesh_header_field_offsets);

    /* Texture header tests (Plan 02) */
    RUN_TEST(test_texture_header_size);
    RUN_TEST(test_texture_magic_value);
    RUN_TEST(test_texture_header_field_offsets);
    RUN_TEST(test_texture_format_enum);

    /* Metadata struct tests */
    RUN_TEST(test_NtMetaEntryHeader_size);
    RUN_TEST(test_NtMetaEntryHeader_field_offsets);
    RUN_TEST(test_NtPackHeader_meta_count_offset);
    RUN_TEST(test_NtAssetEntry_meta_offset_offset);

    /* Shader code header tests */
    RUN_TEST(test_shader_code_header_size);
    RUN_TEST(test_shader_code_magic_value);
    RUN_TEST(test_shader_code_header_field_offsets);
    RUN_TEST(test_shader_stage_enum);

    /* Blob format tests */
    RUN_TEST(test_blob_header_size);
    RUN_TEST(test_blob_magic_value);

    /* Font format tests */
    RUN_TEST(test_font_asset_header_size);
    RUN_TEST(test_font_magic_value);
    RUN_TEST(test_font_version);
    RUN_TEST(test_font_asset_header_field_offsets);
    RUN_TEST(test_font_glyph_entry_size);
    RUN_TEST(test_font_glyph_entry_field_offsets);
    RUN_TEST(test_font_kern_entry_size);
    RUN_TEST(test_font_kern_entry_field_offsets);
    RUN_TEST(test_font_curve_size);
    RUN_TEST(test_font_curve_field_offsets);
    RUN_TEST(test_font_band_size);
    RUN_TEST(test_font_band_field_offsets);
    RUN_TEST(test_font_glyph_array_offset);

    return UNITY_END();
}
