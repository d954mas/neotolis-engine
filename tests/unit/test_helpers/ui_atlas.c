#include "test_helpers/ui_atlas.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define NT_ATLAS_TEST_ACCESS
#include "atlas/nt_atlas.h"
#include "nt_atlas_format.h"

/* ---- Synthetic atlas blob layout (matches engine/atlas/nt_atlas_format.h) ----
 *
 *  NtAtlasHeader (28 bytes)
 *  uint64_t texture_resource_ids[1]    -> page 0 (placeholder hash)
 *  NtAtlasRegion regions[2]            -> region 0: white 4-vert; region 1: hull 6-vert
 *  NtAtlasVertex vertices[10]          -> 4 (white) + 6 (polygon hull)
 *  uint16_t      indices[18]           -> 6 (white: two triangles) + 12 (hull: 4 triangles)
 *
 * Offsets are computed at build time so the blob is contiguous BSS data.
 */

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
#define UI_ATLAS_TOTAL_SIZE (UI_ATLAS_INDEX_OFFSET + UI_ATLAS_INDICES_SIZE)

/* Synthetic blob lives in BSS — built lazily on first create. */
static uint8_t s_blob[UI_ATLAS_TOTAL_SIZE];
static int s_blob_built = 0;

/* Cached parsed data (from nt_atlas_test_drive_resolve). Plan 02 Task 2.3
 * finalizes resource-handle wiring so nt_resource_is_ready returns true.
 * Wave 0 ships the blob constructor + committed test-access call; the public
 * API shape is locked. */
static void *s_atlas_user_data = NULL;

static void ui_atlas_build_blob(void) {
    if (s_blob_built) {
        return;
    }
    memset(s_blob, 0, sizeof s_blob);

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
    memcpy(s_blob + 0, &hdr, sizeof hdr);

    /* ---- Page resource id (placeholder; runtime uses its own slot lookup) ---- */
    uint64_t page_id = 0xC0FFEE00ULL;
    memcpy(s_blob + UI_ATLAS_HEADER_SIZE, &page_id, sizeof page_id);

    /* ---- Region 0: 1x1 white quad, 4 verts, 6 indices ---- */
    NtAtlasRegion region0 = {
        .name_hash = 0x57484954454E4555ULL, /* arbitrary hash for "white" */
        .source_w = 1,
        .source_h = 1,
        .trim_offset_x = 0,
        .trim_offset_y = 0,
        .origin_x = 0.0f,
        .origin_y = 0.0f,
        .vertex_start = 0,
        .index_start = 0,
        .vertex_count = 4,
        .page_index = 0,
        .transform = 0,
        .index_count = 6,
        .flags = 0,
        ._reserved = {0, 0, 0},
    };
    memcpy(s_blob + UI_ATLAS_HEADER_SIZE + UI_ATLAS_PAGE_IDS_SIZE, &region0, sizeof region0);

    /* ---- Region 1: 6-vertex polygon hull (12 indices = 4 fan triangles) ---- */
    NtAtlasRegion region1 = {
        .name_hash = 0x504F4C59474F4E32ULL, /* arbitrary hash for "polygon2" */
        .source_w = 16,
        .source_h = 16,
        .trim_offset_x = 0,
        .trim_offset_y = 0,
        .origin_x = 0.5f,
        .origin_y = 0.5f,
        .vertex_start = 4,
        .index_start = 6,
        .vertex_count = 6,
        .page_index = 0,
        .transform = 0,
        .index_count = 12,
        .flags = 0,
        ._reserved = {0, 0, 0},
    };
    memcpy(s_blob + UI_ATLAS_HEADER_SIZE + UI_ATLAS_PAGE_IDS_SIZE + 40, &region1, sizeof region1);

    /* ---- Vertices: 4 white (corners of 1x1) + 6 polygon-hull ---- */
    NtAtlasVertex verts[UI_ATLAS_VERTEX_COUNT] = {
        /* white quad (trim-local space 0..1, atlas UV 0..0xFFFF) */
        {0, 0, 0, 0},
        {1, 0, 0xFFFF, 0},
        {1, 1, 0xFFFF, 0xFFFF},
        {0, 1, 0, 0xFFFF},
        /* polygon hull (6 verts, hexagon-ish) */
        {0, 8, 0, 0x8000},
        {8, 0, 0x8000, 0},
        {16, 8, 0xFFFF, 0x8000},
        {16, 8, 0xFFFF, 0x8000},
        {8, 16, 0x8000, 0xFFFF},
        {0, 8, 0, 0x8000},
    };
    memcpy(s_blob + UI_ATLAS_VERTEX_OFFSET, verts, sizeof verts);

    /* ---- Indices: white (0..3 fan -> 2 tris) + polygon (fan over 4 tris) ---- */
    uint16_t indices[UI_ATLAS_INDEX_COUNT] = {
        /* white: 0,1,2 + 0,2,3 */
        0,
        1,
        2,
        0,
        2,
        3,
        /* polygon-hull fan (vertices local 0..5): 0,1,2 0,2,3 0,3,4 0,4,5 */
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
    memcpy(s_blob + UI_ATLAS_INDEX_OFFSET, indices, sizeof indices);

    s_blob_built = 1;
}

minimal_ui_atlas_t minimal_ui_atlas_create(void) {
    ui_atlas_build_blob();

    /* Drive the atlas activator's on_resolve callback directly (Revision Issue 4
     * committed path — nt_atlas_test_drive_resolve from NT_ATLAS_TEST_ACCESS).
     * This parses the synthetic blob into an nt_atlas_data_t* without standing
     * up the resource system. Plan 02 Task 2.3 finalizes the nt_resource_t
     * wiring so nt_resource_is_ready(handle) returns true; until then the
     * handle is INVALID and callers must use nt_atlas_test_* accessors. */
    nt_atlas_test_drive_resolve(s_blob, (uint32_t)sizeof s_blob, &s_atlas_user_data);

    minimal_ui_atlas_t result = {
        .handle = NT_RESOURCE_INVALID, /* TODO Plan 02 Task 2.3 */
        .white_region_idx = 0,
        .polygon_region_idx = 1,
    };
    return result;
}

void minimal_ui_atlas_destroy(minimal_ui_atlas_t *atlas) {
    if (atlas == NULL) {
        return;
    }
    if (s_atlas_user_data != NULL) {
        nt_atlas_test_drive_cleanup(s_atlas_user_data);
        s_atlas_user_data = NULL;
    }
    atlas->handle = NT_RESOURCE_INVALID;
    atlas->white_region_idx = 0;
    atlas->polygon_region_idx = 0;
}
