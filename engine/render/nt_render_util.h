#ifndef NT_RENDER_UTIL_H
#define NT_RENDER_UTIL_H

#include "entity/nt_entity.h"
#include "render/nt_render_defs.h"

/* ---- Visibility check (5-check filter) ---- */

bool nt_render_is_visible(nt_entity_t entity);

/* ---- Default globals initializer ---- */

void nt_gfx_get_defaults(nt_globals_t *globals);

#endif /* NT_RENDER_UTIL_H */
