#ifndef NT_TEST_HELPER_UI_ATLAS_H
#define NT_TEST_HELPER_UI_ATLAS_H

#include <stdint.h>

#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "resource/nt_resource.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Raw atlas V bounds of packed sub-region (index 2), used by image/slice9 tests. */
#define MINIMAL_UI_ATLAS_PACKED_V0_RAW 0x8000U
#define MINIMAL_UI_ATLAS_PACKED_V1_RAW 0xC000U

/* Mounts a virtual pack with a synthetic atlas blob and parses it
 * through the full atlas activator, yielding a real READY resource
 * handle with a 1x1 white region at index 0 (4 verts), a 6-vertex
 * polygon region at index 1 (for polygon-hull preservation tests), and
 * a 4-vert PACKED sub-region at index 2 whose atlas UV does NOT span
 * [0,1] (for packed-image and source-coordinate tests).
 *
 * Caller must have init'd nt_hash, nt_gfx, nt_resource, nt_atlas before
 * calling create. Lifetime: valid until destroy.
 *
 * Multiple instances coexist (each owns its own pack ids + blob), so
 * tests that need two atlases get two with no interaction. */
typedef struct {
    nt_resource_t handle;
    uint32_t white_region_idx;   /* always 0 */
    uint32_t polygon_region_idx; /* always 1 */
    uint32_t packed_region_idx;  /* always 2 — non-[0,1] UV sub-region */

    /* Implementation-private. Do not access directly. */
    void *_pack_blob;
    uint32_t _pack_total;
    nt_hash32_t _atlas_pack_id;
    nt_hash32_t _page_pack_id;
    nt_texture_t _page_tex;
} minimal_ui_atlas_t;

minimal_ui_atlas_t minimal_ui_atlas_create(void);
/* Same atlas with another intrinsic scale; region positions are not rescaled. */
minimal_ui_atlas_t minimal_ui_atlas_create_ipu(float inverse_pixels_per_unit);
void minimal_ui_atlas_destroy(minimal_ui_atlas_t *atlas);

#ifdef __cplusplus
}
#endif

#endif /* NT_TEST_HELPER_UI_ATLAS_H */
