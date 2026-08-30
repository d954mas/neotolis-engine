#ifndef NT_RENDERER_SHARED_H
#define NT_RENDERER_SHARED_H

#include "graphics/nt_gfx.h"
#include "log/nt_log.h"
#include "material/nt_material.h"

#include <stdbool.h>
#include <stdint.h>

/* Internal to the renderers -- not a public header, not installed. */

/* The 64-bit key IS the cache identity: descriptors are never compared on a
 * hit, so every nt_pipeline_desc_t field the owning renderer varies has to be
 * folded into it. Every key folds the program handle, which is what lets a dead
 * entry be dropped rather than matched. */
typedef struct {
    uint64_t key;
    nt_pipeline_t pipeline;
} nt_renderer_pipeline_entry_t;

/* Scans for key, swap-removing entries whose pipeline died with its program on
 * the way -- otherwise each corpse pins a cache slot until the renderer resets.
 * Returns an invalid handle on a miss. The loop advances only on a keep: a
 * swap-remove has to re-test the index it just overwrote. */
static inline nt_pipeline_t nt_renderer_pipeline_cache_find(nt_renderer_pipeline_entry_t *entries, uint16_t *count, uint64_t key) {
    for (uint16_t i = 0; i < *count;) {
        if (!nt_gfx_pipeline_valid(entries[i].pipeline)) {
            entries[i] = entries[--(*count)];
            continue;
        }
        if (entries[i].key == key) {
            return entries[i].pipeline;
        }
        i++;
    }
    return (nt_pipeline_t){0};
}

/* One-shot: a game that never assigns a program would otherwise get a black
 * screen and no explanation. The caller owns the flag and clears it when it
 * builds a pipeline, i.e. when something became drawable again. */
static inline void nt_renderer_warn_program_not_ready(bool *warned, const nt_material_info_t *mat_info) {
    if (*warned) {
        return;
    }
    NT_LOG_WARN("skipping '%s': its program is not ready -- assign one with nt_material_set_program, and after a context loss invalidate NT_ASSET_SHADER_CODE so the stages come back",
                (mat_info != NULL && mat_info->label != NULL) ? mat_info->label : "(unlabeled)");
    *warned = true;
}

#endif /* NT_RENDERER_SHARED_H */
