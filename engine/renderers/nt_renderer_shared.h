#ifndef NT_RENDERER_SHARED_H
#define NT_RENDERER_SHARED_H

#include "core/nt_assert.h"
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

/* Scans for key, validating only the entry that matched -- pool generations are
 * 16-bit, so a program handle does eventually recur and a matching key is not on
 * its own proof of a live entry. A dead match reads as a miss, and the miss path
 * reaps every corpse, so the scan itself stays pure comparisons.
 * Returns an invalid handle on a miss. */
static inline nt_pipeline_t nt_renderer_pipeline_cache_find(const nt_renderer_pipeline_entry_t *entries, uint16_t count, uint64_t key) {
    for (uint16_t i = 0; i < count; i++) {
        if (entries[i].key == key) {
            return nt_gfx_pipeline_valid(entries[i].pipeline) ? entries[i].pipeline : (nt_pipeline_t){0};
        }
    }
    return (nt_pipeline_t){0};
}

/* Miss path. Drops entries whose pipeline died with its program -- otherwise each
 * corpse pins a cache slot until the renderer resets -- then builds and stores.
 * The reap loop advances only on a keep: a swap-remove has to re-test the index
 * it just overwrote.
 *
 * A full cache is a configuration bug, not a runtime state, so it traps here for
 * every renderer instead of three copies disagreeing about the bound. An invalid
 * pipeline means a lost context or a failed backend allocation; caching it would
 * pin the failure for the session, so it is returned unstored and retried. */
static inline nt_pipeline_t nt_renderer_pipeline_cache_insert(nt_renderer_pipeline_entry_t *entries, uint16_t *count, uint16_t cap, uint64_t key, const nt_pipeline_desc_t *desc, bool *warned) {
    for (uint16_t i = 0; i < *count;) {
        if (!nt_gfx_pipeline_valid(entries[i].pipeline)) {
            entries[i] = entries[--(*count)];
            continue;
        }
        i++;
    }
    /* Logged as well as asserted: the trap now reports this shared header, so the
     * label is the only thing left that says which renderer overflowed -- and a
     * TRAP release build has no assert message at all. */
    if (*count >= cap) {
        NT_LOG_ERROR("pipeline cache exhausted building '%s' -- raise that renderer's cap", (desc->label != NULL) ? desc->label : "(unlabeled)");
    }
    NT_ASSERT(*count < cap && "pipeline cache exhausted -- raise this renderer's cap (desc.max_pipelines or NT_*_RENDERER_MAX_PIPELINES)");
    nt_pipeline_t pip = nt_gfx_make_pipeline(desc);
    if (pip.id == 0) {
        return pip;
    }
    entries[*count].key = key;
    entries[*count].pipeline = pip;
    (*count)++;
    *warned = false;
    return pip;
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
