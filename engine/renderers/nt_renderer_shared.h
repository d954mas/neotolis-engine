#ifndef NT_RENDERER_SHARED_H
#define NT_RENDERER_SHARED_H

#include "log/nt_log.h"
#include "material/nt_material.h"

#include <stdbool.h>

/* Internal to the renderers -- not a public header, not installed. */

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
