#include "test_helpers/ui_atlas.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef NT_ATLAS_TEST_ACCESS
#define NT_ATLAS_TEST_ACCESS 1
#endif
#include "atlas/nt_atlas.h"
#include "core/nt_assert.h"
#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "nt_atlas_format.h"
#include "nt_crc32.h"
#include "nt_pack_format.h"
#include "resource/nt_resource.h"

/* Synthetic atlas blob layout (matches engine/atlas/nt_atlas_format.h):
 *
 *   NtAtlasHeader     (28 bytes)
 *   page resource id  (8 bytes)
 *   NtAtlasRegion[2]  (region 0 = 4-vert white quad, region 1 = 6-vert hull)
 *   NtAtlasVertex[10]
 *   uint16[18]        (6 indices for white + 12 for hull)
 *
 * Mounts the blob as a real virtual pack so nt_resource_is_ready returns
 * true and nt_atlas_get_region resolves through the regular activator
 * path. Page 0 is a separate high-priority virtual pack with a 1x1 white
 * texture so the sprite renderer's nt_resource_get(page_resource) returns
 * a non-zero texture id. */

#define UI_ATLAS_REGION_COUNT 2u
#define UI_ATLAS_PAGE_COUNT 1u
#define UI_ATLAS_VERTEX_COUNT 10u
#define UI_ATLAS_INDEX_COUNT 18u

#define UI_ATLAS_HEADER_SIZE 28u
#define UI_ATLAS_PAGE_IDS_SIZE (UI_ATLAS_PAGE_COUNT * 8u)
#define UI_ATLAS_REGIONS_SIZE (UI_ATLAS_REGION_COUNT * 40u)
#define UI_ATLAS_VERTICES_SIZE (UI_ATLAS_VERTEX_COUNT * 8u)
#define UI_ATLAS_INDICES_SIZE (UI_ATLAS_INDEX_COUNT * 2u)

#define UI_ATLAS_VERTEX_OFFSET (UI_ATLAS_HEADER_SIZE + UI_ATLAS_PAGE_IDS_SIZE + UI_ATLAS_REGIONS_SIZE)
#define UI_ATLAS_INDEX_OFFSET (UI_ATLAS_VERTEX_OFFSET + UI_ATLAS_VERTICES_SIZE)
#define UI_ATLAS_BLOB_SIZE (UI_ATLAS_INDEX_OFFSET + UI_ATLAS_INDICES_SIZE)

/* Pack name suffix counter so multiple coexisting instances (and
 * sequential create/destroy cycles) don't collide on tombstoned
 * resource-table slots. Monotonic across the whole test binary. */
static uint32_t s_helper_counter;

static const uint8_t s_white_pixel[4] = {255, 255, 255, 255};

static void ui_atlas_build_inner_blob(uint8_t *out_blob, uint32_t suffix) {
    memset(out_blob, 0, UI_ATLAS_BLOB_SIZE);

    /* ---- Header ---- */
    NtAtlasHeader hdr = {
        .magic = NT_ATLAS_MAGIC,
        .version = NT_ATLAS_VERSION,
        .region_count = (uint16_t)UI_ATLAS_REGION_COUNT,
        .page_count = (uint16_t)UI_ATLAS_PAGE_COUNT,
        ._pad = 0,
        .vertex_offset = UI_ATLAS_VERTEX_OFFSET,
        .total_vertex_count = UI_ATLAS_VERTEX_COUNT,
        .index_offset = UI_ATLAS_INDEX_OFFSET,
        .total_index_count = UI_ATLAS_INDEX_COUNT,
    };
    memcpy(out_blob + 0, &hdr, sizeof hdr);

    /* ---- Page resource id (must match the page-pack registration below) ---- */
    char page_name[64];
    (void)snprintf(page_name, sizeof page_name, "ui_atlas_page_%u", suffix);
    uint64_t page_rid = nt_hash64_str(page_name).value;
    memcpy(out_blob + UI_ATLAS_HEADER_SIZE, &page_rid, sizeof page_rid);

    /* ---- Region 0: 1x1 white quad ---- */
    NtAtlasRegion region0 = {
        .name_hash = 0x57484954454E4555ULL, /* "WHITEENU" */
        .source_w = 1,
        .source_h = 1,
        .trim_offset_x = 0,
        .trim_offset_y = 0,
        .origin_x = 0.0F,
        .origin_y = 0.0F,
        .vertex_start = 0,
        .index_start = 0,
        .vertex_count = 4,
        .page_index = 0,
        .transform = 0,
        .index_count = 6,
        .flags = NT_ATLAS_REGION_FLAG_QUAD_012023,
        ._reserved = {0, 0, 0},
    };
    memcpy(out_blob + (size_t)UI_ATLAS_HEADER_SIZE + (size_t)UI_ATLAS_PAGE_IDS_SIZE, &region0, sizeof region0);

    /* ---- Region 1: 6-vertex polygon hull (12 indices = 4 fan triangles) ---- */
    NtAtlasRegion region1 = {
        .name_hash = 0x504F4C59474F4E32ULL, /* "POLYGON2" */
        .source_w = 16,
        .source_h = 16,
        .trim_offset_x = 0,
        .trim_offset_y = 0,
        .origin_x = 0.5F,
        .origin_y = 0.5F,
        .vertex_start = 4,
        .index_start = 6,
        .vertex_count = 6,
        .page_index = 0,
        .transform = 0,
        .index_count = 12,
        .flags = 0,
        ._reserved = {0, 0, 0},
    };
    memcpy(out_blob + (size_t)UI_ATLAS_HEADER_SIZE + (size_t)UI_ATLAS_PAGE_IDS_SIZE + 40U, &region1, sizeof region1);

    /* ---- Vertices: 4 white + 6 polygon-hull ---- */
    NtAtlasVertex verts[UI_ATLAS_VERTEX_COUNT] = {
        /* white quad (trim-local 0..1, atlas UV 0..0xFFFF) */
        {0, 0, 0, 0},
        {1, 0, 0xFFFF, 0},
        {1, 1, 0xFFFF, 0xFFFF},
        {0, 1, 0, 0xFFFF},
        /* polygon hull (hexagon-ish, 6 unique verts) */
        {0, 8, 0, 0x8000},
        {8, 0, 0x8000, 0},
        {16, 8, 0xFFFF, 0x8000},
        {16, 16, 0xFFFF, 0xFFFF},
        {8, 16, 0x8000, 0xFFFF},
        {0, 8, 0, 0x8000},
    };
    memcpy(out_blob + UI_ATLAS_VERTEX_OFFSET, verts, sizeof verts);

    /* ---- Indices ---- */
    uint16_t indices[UI_ATLAS_INDEX_COUNT] = {
        /* white: 0,1,2 + 0,2,3 */
        0,
        1,
        2,
        0,
        2,
        3,
        /* polygon-hull fan: 0,1,2 / 0,2,3 / 0,3,4 / 0,4,5 */
        0,
        1,
        2,
        0,
        2,
        3,
        0,
        3,
        4,
        0,
        4,
        5,
    };
    memcpy(out_blob + UI_ATLAS_INDEX_OFFSET, indices, sizeof indices);
}

/* Wrap the inner atlas blob in a real NEOPAK envelope so nt_resource_parse_pack
 * accepts it. The resulting pack contains exactly one NT_ASSET_ATLAS entry. */
static uint8_t *ui_atlas_build_pack_blob(uint64_t atlas_rid, uint32_t suffix, uint32_t *out_total) {
    uint8_t inner[UI_ATLAS_BLOB_SIZE];
    ui_atlas_build_inner_blob(inner, suffix);

    const uint32_t raw_header = (uint32_t)(sizeof(NtPackHeader) + sizeof(NtAssetEntry));
    const uint32_t header_size = (raw_header + (NT_PACK_DATA_ALIGN - 1U)) & ~(uint32_t)(NT_PACK_DATA_ALIGN - 1U);
    const uint32_t atlas_offset = header_size;
    const uint32_t aligned_atlas = (UI_ATLAS_BLOB_SIZE + (NT_PACK_ASSET_ALIGN - 1U)) & ~(uint32_t)(NT_PACK_ASSET_ALIGN - 1U);
    const uint32_t total_size = atlas_offset + aligned_atlas;

    uint8_t *pack_blob = (uint8_t *)calloc(1, total_size);
    NT_ASSERT(pack_blob != NULL);

    NtPackHeader *ph = (NtPackHeader *)pack_blob;
    ph->magic = NT_PACK_MAGIC;
    ph->version = NT_PACK_VERSION;
    ph->asset_count = 1;
    ph->header_size = header_size;
    ph->total_size = total_size;
    ph->meta_offset = 0;
    ph->meta_count = 0;

    NtAssetEntry *entry = (NtAssetEntry *)(pack_blob + sizeof(NtPackHeader));
    entry->resource_id = atlas_rid;
    entry->asset_type = NT_ASSET_ATLAS;
    entry->format_version = NT_ATLAS_VERSION;
    entry->offset = atlas_offset;
    entry->size = UI_ATLAS_BLOB_SIZE;
    entry->meta_offset = 0;
    entry->_pad = 0;

    memcpy(pack_blob + atlas_offset, inner, UI_ATLAS_BLOB_SIZE);
    ph->checksum = nt_crc32(pack_blob + header_size, total_size - header_size);

    *out_total = total_size;
    return pack_blob;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
minimal_ui_atlas_t minimal_ui_atlas_create(void) {
    minimal_ui_atlas_t out = {0};
    const uint32_t suffix = ++s_helper_counter;

    /* Page pack at high priority so the atlas activator's post-resolve
     * nt_resource_request for the page id finds a registered slot. */
    char page_pack[64];
    char page_name[64];
    (void)snprintf(page_pack, sizeof page_pack, "ui_atlas_page_pack_%u", suffix);
    (void)snprintf(page_name, sizeof page_name, "ui_atlas_page_%u", suffix);
    out._page_pack_id = nt_hash32_str(page_pack);
    NT_ASSERT(nt_resource_create_pack(out._page_pack_id, 100) == NT_OK);
    out._page_tex = nt_gfx_make_texture(&(nt_texture_desc_t){.width = 1, .height = 1, .data = s_white_pixel, .label = "ui_atlas_white_page"});
    NT_ASSERT(out._page_tex.id != 0);
    NT_ASSERT(nt_resource_register(out._page_pack_id, nt_hash64_str(page_name), NT_ASSET_TEXTURE, out._page_tex.id) == NT_OK);

    char atlas_pack[64];
    char atlas_name[64];
    (void)snprintf(atlas_pack, sizeof atlas_pack, "ui_atlas_pack_%u", suffix);
    (void)snprintf(atlas_name, sizeof atlas_name, "ui_atlas_%u", suffix);
    out._atlas_pack_id = nt_hash32_str(atlas_pack);
    nt_hash64_t atlas_rid = nt_hash64_str(atlas_name);

    NT_ASSERT(nt_resource_mount(out._atlas_pack_id, 0) == NT_OK);
    out._pack_blob = ui_atlas_build_pack_blob(atlas_rid.value, suffix, &out._pack_total);
    NT_ASSERT(nt_resource_parse_pack(out._atlas_pack_id, out._pack_blob, out._pack_total) == NT_OK);

    nt_resource_t atlas = nt_resource_request(atlas_rid, NT_ASSET_ATLAS);
    NT_ASSERT(atlas.id != 0);
    /* Two steps: first runs on_resolve + on_post_resolve (which queues a
     * page-texture request); second publishes the page slot so
     * nt_resource_is_ready(atlas) returns true. */
    nt_resource_step();
    nt_resource_step();
    NT_ASSERT(nt_resource_is_ready(atlas));

    out.handle = atlas;
    out.white_region_idx = 0;
    out.polygon_region_idx = 1;
    return out;
}

void minimal_ui_atlas_destroy(minimal_ui_atlas_t *atlas) {
    if (atlas == NULL) {
        return;
    }
    if (atlas->_atlas_pack_id.value != 0) {
        nt_resource_unmount(atlas->_atlas_pack_id);
    }
    if (atlas->_page_pack_id.value != 0) {
        /* Unmount the page pack BEFORE destroying the texture so the
         * resource system doesn't dangle on the runtime handle. */
        nt_resource_unmount(atlas->_page_pack_id);
    }
    if (atlas->_page_tex.id != 0) {
        nt_gfx_destroy_texture(atlas->_page_tex);
    }
    free(atlas->_pack_blob);
    *atlas = (minimal_ui_atlas_t){0};
}
