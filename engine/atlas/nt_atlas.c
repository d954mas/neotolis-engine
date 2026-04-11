/* STUB — real implementation in 48-01 Task 2 */

#include "atlas/nt_atlas.h"

#include <stdlib.h>
#include <string.h>

#include "core/nt_assert.h"
#include "log/nt_log.h"
#include "nt_atlas_format.h"
#include "nt_pack_format.h"
#include "resource/nt_resource.h"

/* ---- Module state ---- */

static struct {
    bool initialized;
} s_atlas;

/* ---- Public API (stub bodies — Task 2 replaces these) ---- */

nt_result_t nt_atlas_init(void) {
    /* Task 2 will install the real activator + resolve callbacks.
     * For Task 1 the module only needs to compile and link cleanly. */
    NT_ASSERT(!s_atlas.initialized && "nt_atlas_init called twice");
    s_atlas.initialized = true;
    return NT_OK;
}

uint32_t nt_atlas_find_region(nt_resource_t atlas, uint64_t name_hash) {
    (void)atlas;
    (void)name_hash;
    NT_ASSERT(0 && "nt_atlas_find_region not implemented — Task 2");
    return NT_ATLAS_INVALID_REGION;
}

const nt_texture_region_t *nt_atlas_get_region(nt_resource_t atlas, uint32_t index) {
    (void)atlas;
    (void)index;
    NT_ASSERT(0 && "nt_atlas_get_region not implemented — Task 2");
    return NULL;
}

nt_resource_t nt_atlas_get_page_resource(nt_resource_t atlas, uint8_t page_index) {
    (void)atlas;
    (void)page_index;
    NT_ASSERT(0 && "nt_atlas_get_page_resource not implemented — Task 2");
    return NT_RESOURCE_INVALID;
}
