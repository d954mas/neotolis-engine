#ifndef NT_TEST_HELPER_UI_ATLAS_H
#define NT_TEST_HELPER_UI_ATLAS_H

#include <stdint.h>

#include "resource/nt_resource.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Produces a real UI atlas resource handle that nt_resource_is_ready returns
 * true for and that has a 1x1 white region at index 0 (4 verts) and a
 * 6-vertex polygon region at index 1 (for polygon-hull preservation tests).
 *
 * Plan 04 Task 2.3 finalization (Path (a) committed): mounts a virtual pack
 * containing a synthetic atlas blob and parses it through the full atlas
 * activator (on_resolve + on_post_resolve). Also registers a 1x1 white
 * texture as page 0 so emit_region can resolve a real page texture handle.
 *
 * Prerequisites (caller must call BEFORE minimal_ui_atlas_create):
 *   - nt_hash_init
 *   - nt_gfx_init (any backend; stub OK)
 *   - nt_resource_init
 *   - nt_atlas_init
 *
 * Lifetime: valid until minimal_ui_atlas_destroy is called. */
typedef struct {
    nt_resource_t handle;
    uint32_t white_region_idx;   /* always 0 */
    uint32_t polygon_region_idx; /* always 1 */
} minimal_ui_atlas_t;

minimal_ui_atlas_t minimal_ui_atlas_create(void);
void minimal_ui_atlas_destroy(minimal_ui_atlas_t *atlas);

#ifdef __cplusplus
}
#endif

#endif /* NT_TEST_HELPER_UI_ATLAS_H */
