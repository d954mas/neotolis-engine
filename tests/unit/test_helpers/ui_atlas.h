#ifndef NT_TEST_HELPER_UI_ATLAS_H
#define NT_TEST_HELPER_UI_ATLAS_H

#include <stdint.h>

#include "graphics/nt_gfx.h"
#include "hash/nt_hash.h"
#include "resource/nt_resource.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Mounts a virtual pack with a synthetic atlas blob and parses it
 * through the full atlas activator, yielding a real READY resource
 * handle with a 1x1 white region at index 0 (4 verts) and a 6-vertex
 * polygon region at index 1 (for polygon-hull preservation tests).
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

    /* Implementation-private. Do not access directly. */
    void *_pack_blob;
    uint32_t _pack_total;
    nt_hash32_t _atlas_pack_id;
    nt_hash32_t _page_pack_id;
    nt_texture_t _page_tex;
} minimal_ui_atlas_t;

minimal_ui_atlas_t minimal_ui_atlas_create(void);
void minimal_ui_atlas_destroy(minimal_ui_atlas_t *atlas);

#ifdef __cplusplus
}
#endif

#endif /* NT_TEST_HELPER_UI_ATLAS_H */
