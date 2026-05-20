#ifndef NT_TEST_HELPER_UI_ATLAS_H
#define NT_TEST_HELPER_UI_ATLAS_H

#include <stdint.h>

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
 * calling create. Lifetime: valid until destroy. */
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
