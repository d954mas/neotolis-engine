# Phase 26: Material System - Context

**Gathered:** 2026-03-19
**Status:** Ready for planning

<domain>
## Phase Boundary

Runtime material objects combining shader references, texture references, named params, render state, and attribute mapping. Game creates materials in code from loaded resources. Material does NOT own pipeline — pipeline creation happens in Phase 27 render module from material + mesh combination. Phase also extends mesh_info with stream descriptors and renames nt_render_state_comp → nt_drawable_comp.

Phase does NOT include: mesh rendering pipeline (Phase 27), demo integration (Phase 28), instancing, shadow mapping, material as pack asset.

</domain>

<decisions>
## Implementation Decisions

### Module architecture
- New `nt_material` engine module with pooled generational handles (`nt_material_t`)
- Pool follows existing engine pattern (slot + generation in uint32_t)
- `NT_MAX_MATERIALS 64` default, game can `#define` override before include
- Materials are **shared** — many entities reference one material
- **Immutable** after creation — per-entity variation through DrawableComponent
- `nt_material` links `nt_resource` directly (CMake dependency) — calls `nt_resource_get()` in material_step()

### Material stores resource handles, not GFX handles
- Shader references stored as `nt_resource_t` (from `nt_resource_request()`)
- Texture references stored as `nt_resource_t`
- `material_step()` resolves to GPU handles via `nt_resource_get()`
- No GFX handles stored on material — always resolved from resource system
- Enables uniform handling of: async loading (not ready yet), context loss (handles reset to 0), pack replacement (new handles)

### material_step() — resource resolution and change detection
- Game calls `nt_material_step()` **explicitly** in frame loop, after `nt_resource_step()`, before render
- Resolves all resources: shaders (`resolved_vs`, `resolved_fs`) and textures (`resolved_tex[]`)
- Tracks `last_vs`, `last_fs` — if `nt_resource_get()` returns different handle, `version++`
- `mat->ready = (resolved_vs != 0 && resolved_fs != 0)` — pipeline needs shaders, textures can fallback
- If `resolved_vs == 0` (context loss, still loading): `ready = false`, pipeline handle zeroed (GPU already destroyed it)
- All cases — loading, context loss, pack swap — handled by one comparison in step()

### Frame order (critical)
```
1. nt_resource_step()    — activates loaded assets
2. nt_material_step()    — resolves handles, detects changes
3. render()              — uses resolved handles, manages pipeline cache
```

### Pipeline ownership — NOT in material
- Material does NOT store pipeline or vertex layout
- Pipeline = shader (from material) + vertex layout (from mesh) + render state (from material)
- Pipeline created and cached by render module (Phase 27)
- Pipeline cache in render module keyed by `(material_id, mesh_layout_hash)`
- Render checks `mat->version` against cached version → recreate on mismatch
- Pipeline cache cleared on `g_nt_gfx.context_restored` flag (stale handles, don't destroy — GPU already did)
- Same mesh format + same material = same pipeline. 20 trees = 1 pipeline, 20 draw calls

### Attribute mapping (per-material)
- Material defines `attr_map[]`: maps mesh stream `name_hash` → shader `location`
- Game controls mapping — each shader can use any location convention
- Render (Phase 27) builds `nt_vertex_layout_t` from mesh streams + material attr_map:
  - Iterate ALL mesh streams, accumulate offset for every stream (including unused by shader)
  - Only add to layout.attrs for streams found in attr_map
  - Stride = total vertex size including unused attributes
- Validation: assert if material's attr_map expects an attribute the mesh doesn't have

### Named texture slots
- Material desc uses named slots: `{ .name = "u_albedo", .texture = resource_handle }`
- Name maps to shader `uniform sampler2D` by exact string match
- `glGetUniformLocation(program, name)` called **once** at pipeline creation, int location cached in pipeline cache entry
- Per-frame: only `glUniform1i(cached_loc, unit)` + `nt_gfx_bind_texture()` — zero string lookups
- `NT_MATERIAL_MAX_TEXTURES = 4` (albedo, normal, roughness, metallic covers PBR)

### Named param slots
- Material desc uses named slots: `{ .name = "u_roughness", .value = vec4 }`
- All numeric params stored as `vec4` (per spec S16.2)
- float uses .x, vec2 uses .xy, vec3 uses .xyz, vec4 uses .xyzw
- Uniform locations cached same way as textures — one-time string lookup
- `NT_MATERIAL_MAX_PARAMS = 4`

### Named per-entity param slots (deferred but designed)
- Material desc defines per-entity param names: `{ .name = "u_dissolve" }`
- Maps to `ShaderParamsComponent` data on entity (vec4 by index)
- Name in material (knows shader), data in component (just vec4)
- Uniform locations cached in pipeline cache entry at pipeline creation
- If shader doesn't have the uniform → location -1 → glUniform no-op
- `NT_MAX_PER_ENTITY_PARAMS = 4`
- ShaderParamsComponent itself is **deferred** — field in material desc is ready

### Per-entity uniforms (render conventions)
- Render module (Phase 27) always sets fixed per-entity uniforms: `u_mvp`, `u_color`
- Locations cached in pipeline cache entry alongside material slot locations
- Data source: `u_mvp` from TransformComponent, `u_color` from DrawableComponent
- If shader doesn't use one → location -1 → no-op

### Render state — clear split, no overlap
- **Material** stores GPU pipeline state: blend_mode, depth_test, depth_write, cull_mode → feeds into `nt_pipeline_desc_t` at pipeline creation
- **DrawableComponent** stores per-entity data: visible, color, tag → uniforms and skip logic
- No overlap, no conflict, no merge logic needed

### Rename nt_render_state_comp → nt_drawable_comp
- Current name conflicts with material render state (blend/depth/cull)
- DrawableComponent: visible (bool) + tag (uint16) + color (vec4) — always present on rendered entities
- Instancing-friendly: same fields work as uniforms (v1.3) or instance buffer (v1.4+)
- Refactor as part of Phase 26

### Mesh info extension (part of Phase 26)
- Extend `nt_gfx_mesh_info_t` with: `NtStreamDesc streams[NT_MESH_MAX_STREAMS]`, `uint16_t stride`, `uint32_t layout_hash`
- Mesh activator saves stream descriptors and computes stride + layout_hash at activation time
- `layout_hash` computed once — used as pipeline cache key, no per-frame hashing
- Enables render module to build vertex layout from mesh without re-reading pack data

### Texture fallback
- If texture not ready (`resolved_tex == 0`), render uses fallback texture (e.g., checkerboard)
- Same pattern as existing textured_quad demo
- Material is "ready" when shaders are ready — textures can be pending

### Claude's Discretion
- Internal material slot struct layout and padding
- Pipeline cache entry struct details (Phase 27 but informed by material design)
- Exact per_entity_params field naming in material desc
- `nt_material_init()`/`nt_material_shutdown()` descriptor details
- attr_map max entries constant name and value
- build_layout() utility function placement (shared utility vs render-internal)
- DrawableComponent rename migration details (find/replace scope)

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Material system spec
- `docs/neotolis_engine_spec_1.md` §16.1 — MaterialAsset purpose: shader + render state + values
- `docs/neotolis_engine_spec_1.md` §16.2 — Numeric params policy: all vec4, intentional
- `docs/neotolis_engine_spec_1.md` §16.3 — MaterialAssetHeader binary layout (future reference for pack asset)
- `docs/neotolis_engine_spec_1.md` §16.4 — No separate MaterialRuntime copy decision
- `docs/neotolis_engine_spec_1.md` §16.5 — Render state in material: needed for bucket/sort/batch

### Requirements
- `.planning/REQUIREMENTS.md` §Material System — MAT-01 through MAT-04

### Existing code (material module will interact with)
- `engine/graphics/nt_gfx.h` — Pipeline desc, handle types, uniform API, mesh_info_t (to extend), texture/shader/buffer types
- `engine/graphics/nt_gfx.c` — Mesh activator (to extend with stream storage), pipeline make/destroy
- `engine/resource/nt_resource.h` — nt_resource_t, nt_resource_get(), nt_resource_request(), nt_resource_is_ready()
- `engine/material_comp/nt_material_comp.h` — Existing MaterialComponent (stores uint32_t handle → will store nt_material_t.id)
- `engine/render_state_comp/nt_render_state_comp.h` — To be renamed to nt_drawable_comp
- `engine/render_state_comp/nt_render_state_comp.c` — Implementation to rename
- `shared/include/nt_mesh_format.h` — NtStreamDesc, NtMeshAssetHeader (stream descriptors to store in mesh_info)

### Prior phase decisions
- `.planning/phases/25-asset-loading/25-CONTEXT.md` — Shader activation = compile only (no pipeline), context loss: game calls invalidate(), resource_step re-activates
- `.planning/phases/22-entity-system/22-CONTEXT.md` — MaterialComponent stores opaque uint32_t, RenderStateComponent design, params0 deferred

### Existing demo (pattern reference)
- `examples/textured_quad/main.c` — Lines 181-215: manual pipeline creation + texture resolve pattern that material_step() automates

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `nt_gfx_mesh_info_t` (`engine/graphics/nt_gfx.h:30`): mesh side table with VBO/IBO — extend with streams/stride/layout_hash
- `nt_gfx_activate_mesh()` (`engine/graphics/nt_gfx.c:737`): already reads NtStreamDesc from pack data — just needs to save them
- `nt_resource_get()` / `nt_resource_request()`: material stores resource handles, resolves to GPU handles
- `nt_comp_storage_t` pattern: sparse+dense storage used by all existing components — material_comp already uses it
- `g_nt_gfx.context_restored` flag: exists, lives one frame — pipeline cache can use it

### Established Patterns
- Generational handles: `typedef struct { uint32_t id; } nt_material_t;` — same as nt_shader_t, nt_pipeline_t, etc.
- Pool + free stack: nt_gfx pool pattern for shader/pipeline/buffer/texture pools
- Module state: `static struct { ... } s_state;` for internal material data
- Descriptor at init: `nt_material_init(const nt_material_desc_t* desc)` pattern
- Named uniform API: `nt_gfx_set_uniform_vec4(name, val)` — already string-based, material extends with cached locations

### Integration Points
- `engine/CMakeLists.txt` — add_subdirectory for new nt_material module
- `engine/material_comp/` — update to reference nt_material_t handles
- `engine/render_state_comp/` — rename to `engine/drawable_comp/` (nt_drawable_comp)
- `engine/graphics/nt_gfx.h` — extend nt_gfx_mesh_info_t struct
- `engine/graphics/nt_gfx.c` — update nt_gfx_activate_mesh() to save streams
- Tests: existing test_material_comp, test_render_state_comp need updates for rename

</code_context>

<specifics>
## Specific Ideas

- Material is a "recipe from references" — no GPU objects stored directly, everything resolved through nt_resource
- "Один if на все случаи" — single comparison `vs != last_vs` handles first load, context loss, and pack replacement uniformly
- Demo already implements the manual version of material_step() pattern (textured_quad lines 181-215) — material module automates this
- Per-entity variation (roughness, dissolve, damage) goes through components (DrawableComponent.color, future ShaderParamsComponent.params), not material mutation
- Shadow pass = different material (minimal shader, position-only attr_map, no textures/params), same mesh — validates the architecture
- Quality levels = swap material handle on entities, not mutate existing material
- Pipeline cache entry stores uniform locations alongside pipeline handle — strings needed once at creation, then only int locations

</specifics>

<deferred>
## Deferred Ideas

- ShaderParamsComponent (per-entity custom vec4 params) — material desc field ready, component implementation when needed
- Material as pack asset (MATASSET-01, MATASSET-02) — v1.4, runtime creation covers v1.3
- Pipeline cache implementation — Phase 27 render module
- Instancing (per-instance buffers for color/params/transforms) — v1.4+
- Shadow mapping (per-pass uniforms like u_light_vp) — v1.4+
- Texture atlas / texture arrays for DC reduction — v1.4+
- Auto layout mapping via engine convention (hash→location table) — if needed when mesh format variety increases
- params0 in DrawableComponent — deferred, add when per-entity shader effects needed

</deferred>

---

*Phase: 26-material-system*
*Context gathered: 2026-03-19*
