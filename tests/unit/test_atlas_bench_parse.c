/* System headers before Unity to avoid noreturn / __declspec conflict on MSVC */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include "nt_atlas_format.h"
#include "nt_pack_format.h"
#include "ntpack_parse.h"
#include "unity.h"
/* clang-format on */

/* Build the same byte layout consumed by the runtime atlas loader. */

typedef struct {
    const NtAtlasRegion *regions;
    uint16_t region_count;
    const NtAtlasVertex *vertices;
    uint32_t total_vertex_count;
    const uint16_t *indices;
    uint32_t total_index_count;
    const uint64_t *page_ids;
    uint16_t page_count;
} mock_atlas_spec_t;

static uint32_t build_mock_atlas_blob(uint8_t *out, uint32_t cap, const mock_atlas_spec_t *spec) {
    const uint32_t page_bytes = (uint32_t)spec->page_count * (uint32_t)sizeof(uint64_t);
    const uint32_t region_bytes = (uint32_t)spec->region_count * (uint32_t)sizeof(NtAtlasRegion);
    const uint32_t vertex_bytes = spec->total_vertex_count * (uint32_t)sizeof(NtAtlasVertex);
    const uint32_t index_bytes = spec->total_index_count * (uint32_t)sizeof(uint16_t);

    const uint32_t vertex_offset = (uint32_t)sizeof(NtAtlasHeader) + page_bytes + region_bytes;
    const uint32_t index_offset = vertex_offset + vertex_bytes;
    const uint32_t total = index_offset + index_bytes;

    TEST_ASSERT_MESSAGE(total <= cap, "mock blob buffer too small");

    memset(out, 0, total);

    NtAtlasHeader *hdr = (NtAtlasHeader *)out;
    hdr->magic = NT_ATLAS_MAGIC;
    hdr->version = NT_ATLAS_VERSION;
    hdr->region_count = spec->region_count;
    hdr->page_count = spec->page_count;
    hdr->_pad = 0;
    hdr->vertex_offset = vertex_offset;
    hdr->total_vertex_count = spec->total_vertex_count;
    hdr->index_offset = index_offset;
    hdr->total_index_count = spec->total_index_count;

    if (page_bytes > 0) {
        memcpy(out + sizeof(NtAtlasHeader), spec->page_ids, page_bytes);
    }
    if (region_bytes > 0) {
        memcpy(out + sizeof(NtAtlasHeader) + page_bytes, spec->regions, region_bytes);
    }
    if (vertex_bytes > 0) {
        memcpy(out + vertex_offset, spec->vertices, vertex_bytes);
    }
    if (index_bytes > 0) {
        memcpy(out + index_offset, spec->indices, index_bytes);
    }

    return total;
}

void setUp(void) {}
void tearDown(void) {}

/* ---- Fixtures ---- */

/* 3 regions with vertex_counts {3, 5, 8} laid out contiguously. */
static uint32_t build_counts_blob(uint8_t *buf, uint32_t cap) {
    NtAtlasVertex verts[16];
    for (uint16_t i = 0; i < 16; i++) {
        verts[i].local_x = (int16_t)(i * 4);
        verts[i].local_y = (int16_t)(i * 5);
        verts[i].atlas_u = (uint16_t)(i * 1000);
        verts[i].atlas_v = (uint16_t)(i * 700);
    }

    NtAtlasRegion regions[3];
    memset(regions, 0, sizeof(regions));
    const uint8_t counts[3] = {3, 5, 8};
    uint32_t start = 0;
    for (uint16_t r = 0; r < 3; r++) {
        regions[r].vertex_start = start;
        regions[r].vertex_count = counts[r];
        regions[r].page_index = 0;
        start += counts[r];
    }

    mock_atlas_spec_t spec = {
        .regions = regions,
        .region_count = 3,
        .vertices = verts,
        .total_vertex_count = 16,
        .indices = NULL,
        .total_index_count = 0,
        .page_ids = NULL,
        .page_count = 0,
    };
    return build_mock_atlas_blob(buf, cap, &spec);
}

/* One region, a non-degenerate unit-ish quad in atlas-UV corners. */
static uint32_t build_quad_blob(uint8_t *buf, uint32_t cap) {
    NtAtlasVertex verts[4];
    /* CCW quad corners, all distinct atlas_u/atlas_v. */
    const uint16_t us[4] = {1000, 5000, 5000, 1000};
    const uint16_t vs[4] = {2000, 2000, 6000, 6000};
    for (uint16_t i = 0; i < 4; i++) {
        verts[i].local_x = (int16_t)(i * 3);
        verts[i].local_y = (int16_t)(i * 3);
        verts[i].atlas_u = us[i];
        verts[i].atlas_v = vs[i];
    }

    NtAtlasRegion region;
    memset(&region, 0, sizeof(region));
    region.vertex_start = 0;
    region.vertex_count = 4;
    region.page_index = 0;

    mock_atlas_spec_t spec = {
        .regions = &region,
        .region_count = 1,
        .vertices = verts,
        .total_vertex_count = 4,
        .indices = NULL,
        .total_index_count = 0,
        .page_ids = NULL,
        .page_count = 0,
    };
    return build_mock_atlas_blob(buf, cap, &spec);
}

/* ---- Tests ---- */

static void region_and_vertex_counts(void) {
    uint8_t buf[1024];
    uint32_t size = build_counts_blob(buf, sizeof(buf));

    nt_bench_atlas_metrics_t m;
    int rc = nt_bench_parse_atlas_blob(buf, size, &m);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_UINT16(3, m.region_count);
    TEST_ASSERT_EQUAL_UINT32(16, m.total_vertex_count);
    TEST_ASSERT_EQUAL_UINT32(8, m.hull_vert_max);
    TEST_ASSERT_EQUAL_UINT32(3, m.hull_vert_min);
    TEST_ASSERT_EQUAL_UINT32(16, m.hull_vert_total);
}

static void polygon_area_positive(void) {
    uint8_t buf[512];
    uint32_t size = build_quad_blob(buf, sizeof(buf));

    nt_bench_atlas_metrics_t m;
    int rc = nt_bench_parse_atlas_blob(buf, size, &m);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_TRUE(m.poly_area_uv > 0.0);
}

static void reject_bad_magic(void) {
    uint8_t buf[512];
    uint32_t size = build_quad_blob(buf, sizeof(buf));
    NtAtlasHeader *hdr = (NtAtlasHeader *)buf;
    hdr->magic = 0xDEADBEEF;

    nt_bench_atlas_metrics_t m;
    int rc = nt_bench_parse_atlas_blob(buf, size, &m);
    TEST_ASSERT_TRUE(rc < 0);
}

static void reject_bad_version(void) {
    uint8_t buf[512];
    uint32_t size = build_quad_blob(buf, sizeof(buf));
    NtAtlasHeader *hdr = (NtAtlasHeader *)buf;
    hdr->version = (uint16_t)(NT_ATLAS_VERSION + 1);

    nt_bench_atlas_metrics_t m;
    int rc = nt_bench_parse_atlas_blob(buf, size, &m);
    TEST_ASSERT_TRUE(rc < 0);
}

static void reject_oob_vertex_offset(void) {
    uint8_t buf[512];
    uint32_t size = build_quad_blob(buf, sizeof(buf));
    NtAtlasHeader *hdr = (NtAtlasHeader *)buf;
    hdr->vertex_offset = size + 64; /* past end of buffer */

    nt_bench_atlas_metrics_t m;
    int rc = nt_bench_parse_atlas_blob(buf, size, &m);
    TEST_ASSERT_TRUE(rc < 0);
}

static void reject_missing_page_texture(void) {
    const char *path = "test_atlas_bench_missing_texture.ntpack";
    uint8_t atlas_blob[sizeof(NtAtlasHeader) + sizeof(uint64_t)] = {0};
    NtAtlasHeader *atlas = (NtAtlasHeader *)atlas_blob;
    atlas->magic = NT_ATLAS_MAGIC;
    atlas->version = NT_ATLAS_VERSION;
    atlas->page_count = 1;

    const uint64_t page_id = 0x12345678ULL;
    memcpy(atlas_blob + sizeof(NtAtlasHeader), &page_id, sizeof(page_id));

    NtPackHeader header = {0};
    header.magic = NT_PACK_MAGIC;
    header.version = NT_PACK_VERSION;
    header.asset_count = 1;
    header.header_size = sizeof(NtPackHeader) + sizeof(NtAssetEntry);
    header.total_size = header.header_size + sizeof(atlas_blob);

    NtAssetEntry entry = {0};
    entry.resource_id = 1;
    entry.offset = header.header_size;
    entry.size = sizeof(atlas_blob);
    entry.format_version = NT_ATLAS_VERSION;
    entry.asset_type = NT_ASSET_ATLAS;

    FILE *file = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL_size_t(sizeof(header), fwrite(&header, 1, sizeof(header), file));
    TEST_ASSERT_EQUAL_size_t(sizeof(entry), fwrite(&entry, 1, sizeof(entry), file));
    TEST_ASSERT_EQUAL_size_t(sizeof(atlas_blob), fwrite(atlas_blob, 1, sizeof(atlas_blob), file));
    TEST_ASSERT_EQUAL_INT(0, fclose(file));

    nt_bench_atlas_metrics_t m;
    int rc = nt_bench_parse_ntpack(path, &m);
    (void)remove(path);
    TEST_ASSERT_TRUE(rc < 0);
}

static void selected_geometry_is_owned_and_y_down(void) {
    const char *path = "test_atlas_bench_selected.ntpack";
    const NtAtlasVertex vertices[4] = {
        {.local_x = 0, .local_y = 4},
        {.local_x = 0, .local_y = 0},
        {.local_x = 4, .local_y = 0},
        {.local_x = 4, .local_y = 4},
    };
    const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    NtAtlasRegion region = {0};
    region.source_w = 6;
    region.source_h = 7;
    region.trim_offset_x = 1;
    region.trim_offset_y = 2;
    region.vertex_count = 4;
    region.index_count = 6;
    uint8_t atlas_blob[512];
    const mock_atlas_spec_t spec = {
        .regions = &region,
        .region_count = 1,
        .vertices = vertices,
        .total_vertex_count = 4,
        .indices = indices,
        .total_index_count = 6,
    };
    const uint32_t atlas_size = build_mock_atlas_blob(atlas_blob, sizeof(atlas_blob), &spec);
    NtPackHeader header = {.magic = NT_PACK_MAGIC, .version = NT_PACK_VERSION, .asset_count = 1};
    header.header_size = sizeof(NtPackHeader) + sizeof(NtAssetEntry);
    header.total_size = header.header_size + atlas_size;
    NtAssetEntry entry = {.offset = header.header_size, .size = atlas_size, .format_version = NT_ATLAS_VERSION, .asset_type = NT_ASSET_ATLAS};
    FILE *file = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL_size_t(sizeof(header), fwrite(&header, 1, sizeof(header), file));
    TEST_ASSERT_EQUAL_size_t(sizeof(entry), fwrite(&entry, 1, sizeof(entry), file));
    TEST_ASSERT_EQUAL_size_t(atlas_size, fwrite(atlas_blob, 1, atlas_size, file));
    TEST_ASSERT_EQUAL_INT(0, fclose(file));

    nt_bench_selected_geometry_t geometry = {0};
    TEST_ASSERT_EQUAL_INT(0, nt_bench_parse_selected_geometry(path, 0, 4, 4, &geometry));
    TEST_ASSERT_EQUAL_UINT32(4, geometry.vertex_count);
    TEST_ASSERT_EQUAL_UINT32(6, geometry.triangle_index_count);
    TEST_ASSERT_EQUAL_INT32(0, geometry.polygon[0].y);
    TEST_ASSERT_EQUAL_INT32(4, geometry.polygon[1].y);
    TEST_ASSERT_EQUAL_UINT16(2, geometry.triangle_indices[1]);
    TEST_ASSERT_EQUAL_UINT16(1, geometry.triangle_indices[2]);
    TEST_ASSERT_EQUAL_UINT16(6, geometry.source_w);
    TEST_ASSERT_EQUAL_INT16(2, geometry.trim_offset_y);
    nt_bench_selected_geometry_destroy(&geometry);
    TEST_ASSERT_NULL(geometry.polygon);
    TEST_ASSERT_NULL(geometry.triangle_indices);
    (void)remove(path);
}

static void selected_geometry_rejects_corrupt_index_window(void) {
    const char *path = "test_atlas_bench_selected_corrupt.ntpack";
    uint8_t blob[512];
    const NtAtlasVertex vertices[3] = {0};
    NtAtlasRegion region = {.vertex_count = 3, .index_count = 3, .index_start = UINT32_MAX};
    const mock_atlas_spec_t spec = {.regions = &region, .region_count = 1, .vertices = vertices, .total_vertex_count = 3};
    const uint32_t size = build_mock_atlas_blob(blob, sizeof(blob), &spec);
    NtPackHeader header = {.magic = NT_PACK_MAGIC, .version = NT_PACK_VERSION, .asset_count = 1};
    header.header_size = sizeof(NtPackHeader) + sizeof(NtAssetEntry);
    header.total_size = header.header_size + size;
    NtAssetEntry entry = {.offset = header.header_size, .size = size, .format_version = NT_ATLAS_VERSION, .asset_type = NT_ASSET_ATLAS};
    FILE *file = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL_size_t(sizeof(header), fwrite(&header, 1, sizeof(header), file));
    TEST_ASSERT_EQUAL_size_t(sizeof(entry), fwrite(&entry, 1, sizeof(entry), file));
    TEST_ASSERT_EQUAL_size_t(size, fwrite(blob, 1, size, file));
    TEST_ASSERT_EQUAL_INT(0, fclose(file));
    nt_bench_selected_geometry_t geometry = {0};
    TEST_ASSERT_TRUE(nt_bench_parse_selected_geometry(path, 0, 4, 4, &geometry) < 0);
    TEST_ASSERT_NULL(geometry.polygon);
    TEST_ASSERT_NULL(geometry.triangle_indices);
    (void)remove(path);
}

static void selected_geometry_rejects_overlapping_sections(void) {
    const char *path = "test_atlas_bench_selected_overlap.ntpack";
    uint8_t blob[512];
    const NtAtlasVertex vertices[3] = {0};
    const uint16_t indices[3] = {0, 1, 2};
    NtAtlasRegion region = {.vertex_count = 3, .index_count = 3};
    const mock_atlas_spec_t spec = {
        .regions = &region,
        .region_count = 1,
        .vertices = vertices,
        .total_vertex_count = 3,
        .indices = indices,
        .total_index_count = 3,
    };
    const uint32_t size = build_mock_atlas_blob(blob, sizeof(blob), &spec);
    NtAtlasHeader *atlas = (NtAtlasHeader *)blob;
    atlas->index_offset = atlas->vertex_offset;
    NtPackHeader header = {.magic = NT_PACK_MAGIC, .version = NT_PACK_VERSION, .asset_count = 1};
    header.header_size = sizeof(NtPackHeader) + sizeof(NtAssetEntry);
    header.total_size = header.header_size + size;
    NtAssetEntry entry = {.offset = header.header_size, .size = size, .format_version = NT_ATLAS_VERSION, .asset_type = NT_ASSET_ATLAS};
    FILE *file = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL_size_t(sizeof(header), fwrite(&header, 1, sizeof(header), file));
    TEST_ASSERT_EQUAL_size_t(sizeof(entry), fwrite(&entry, 1, sizeof(entry), file));
    TEST_ASSERT_EQUAL_size_t(size, fwrite(blob, 1, size, file));
    TEST_ASSERT_EQUAL_INT(0, fclose(file));
    nt_bench_selected_geometry_t geometry = {0};
    TEST_ASSERT_TRUE(nt_bench_parse_selected_geometry(path, 0, 4, 4, &geometry) < 0);
    TEST_ASSERT_NULL(geometry.polygon);
    TEST_ASSERT_NULL(geometry.triangle_indices);
    (void)remove(path);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(region_and_vertex_counts);
    RUN_TEST(polygon_area_positive);
    RUN_TEST(reject_bad_magic);
    RUN_TEST(reject_bad_version);
    RUN_TEST(reject_oob_vertex_offset);
    RUN_TEST(reject_missing_page_texture);
    RUN_TEST(selected_geometry_is_owned_and_y_down);
    RUN_TEST(selected_geometry_rejects_corrupt_index_window);
    RUN_TEST(selected_geometry_rejects_overlapping_sections);
    return UNITY_END();
}
