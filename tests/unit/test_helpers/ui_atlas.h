#ifndef NT_TEST_HELPER_UI_ATLAS_H
#define NT_TEST_HELPER_UI_ATLAS_H

#include <stdint.h>

#include "resource/nt_resource.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Produces a fake "UI atlas" resource handle that nt_resource_is_ready returns
 * true for and that has a 1x1 white region at index 0 (4 verts) and a
 * 6-vertex polygon region at index 1 (for polygon-hull preservation tests).
 *
 * Built via NT_ATLAS_TEST_ACCESS nt_atlas_test_drive_resolve — bypasses pack
 * loading. (Revision Issue 4 committed path.)
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
