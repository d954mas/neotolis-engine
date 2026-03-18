# Phase 22: Entity System - Context

**Gathered:** 2026-03-16
**Status:** Ready for planning

<domain>
## Phase Boundary

Entity handles with generational IDs, sparse+dense component storage, transform + render components. Game creates entities, attaches typed components, destroys entities with automatic component cleanup. No hierarchy (all entities are roots), no deferred destruction. This phase delivers the data structures and APIs — rendering pipeline (Phase 27) consumes them.

</domain>

<decisions>
## Implementation Decisions

### ECS approach
- Custom entity system per spec — no external library (flecs rejected: too large for WASM, conflicts with code-first/explicit-over-implicit/tiny-size philosophy)
- Minimal implementation: ~15 functions, 4 data structures

### Pool sizing & configuration
- Descriptor struct at init (follows nt_gfx pattern): `nt_entity_desc_t` with `max_entities`, each component has `_comp_desc_t` with `capacity`
- No defaults — game must explicitly set all capacities. Init fails/asserts if any are zero/unset
- Assert if component capacity > max_entities (more components than entities is impossible to use)
- Independent capacities per component type (e.g., 4096 entities but only 1024 transforms)

### Destroy & cleanup model
- Immediate destruction — entity_destroy() removes all components and frees slot right now
- Registered storage list: each component module registers with entity system during its `_comp_init()`
- Registration struct per storage: `name (const char*)` + `has()` function pointer + `on_destroy()` callback
- No userdata in callbacks — storages are module-internal static, callbacks know their own storage
- On entity_destroy(): entity system calls ALL registered on_destroy callbacks; each callback does its own has() check (O(1) sparse lookup) then remove() if present
- Debug support: iterate registered storages and call has() to list all components on an entity
- Stale handle (using destroyed entity): always assert, even in release
- Double destroy: assert in debug, ignore in release

### Module & API organization
- One CMake module per component type — flat layout in engine/, same as all other modules
- `engine/entity/` → `nt_entity` (core pool + storage registration)
- `engine/transform_comp/` → `nt_transform_comp` (links: nt_entity, nt_math)
- `engine/mesh_comp/` → `nt_mesh_comp` (links: nt_entity)
- `engine/material_comp/` → `nt_material_comp` (links: nt_entity)
- `engine/render_state_comp/` → `nt_render_state_comp` (links: nt_entity)
- Includes follow existing convention: `#include "transform_comp/nt_transform_comp.h"`
- Naming pattern: `nt_<name>_comp_<verb>()` — makes clear these are component operations
- Static module-internal storage per component — game uses functions only, never touches storage structs

### RenderStateComponent
- Fields: `uint16_t tag`, `bool visible`, `vec4 color`
- tag: pass/group filter chosen by game (0 = default/all passes)
- visible: render visibility (false = skip entity in render)
- color: per-entity tint + alpha, sent as u_color uniform. Default: (1,1,1,1)
- params0 deferred — will be separate ShaderParams component when shader effects are actually needed
- Defaults on add: tag=0, visible=true, color=white

### Claude's Discretion
- Sparse+dense storage implementation details (array sizing, INVALID_INDEX sentinel value)
- Internal struct layout and padding for component types
- Exact assertion messages and debug logging format
- Test structure and coverage approach
- CMakeLists.txt wiring details

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Entity & component architecture
- `docs/neotolis_engine_spec_1.md` §6.1-6.4 — Entity identity, generation, entity table, destruction strategies
- `docs/neotolis_engine_spec_1.md` §7 — Component storage pattern (sparse+dense, typed API template)
- `docs/neotolis_engine_spec_1.md` §8.1 — TransformComponent fields and update rules
- `docs/neotolis_engine_spec_1.md` §9.1-9.3 — RenderStateComponent, MeshComponent, MaterialComponent

### Existing patterns to follow
- `engine/graphics/nt_gfx.h` — Pool/handle pattern (slot+generation), descriptor struct at init
- `engine/graphics/nt_gfx_internal.h` — NT_GFX_SLOT_SHIFT/MASK encoding for generation+index
- `engine/graphics/nt_gfx.c` lines 83-149 — Pool init/alloc/free/valid implementation
- `engine/math/nt_math.h` — cglm types (vec3, quat, mat4) used by TransformComponent

### Requirements
- `.planning/REQUIREMENTS.md` §Entity System — ENT-01 through ENT-04
- `.planning/REQUIREMENTS.md` §Component Storage — COMP-01 through COMP-04
- `.planning/REQUIREMENTS.md` §Transform Component — XFORM-01 through XFORM-03
- `.planning/REQUIREMENTS.md` §Render Components — RCOMP-01 through RCOMP-03

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `nt_gfx_pool_t` pattern: slot/generation encoding, free queue stack, pool_init/alloc/free/valid — entity pool should follow same principle but with uint16 index + uint16 generation
- `nt_math.h`: wraps cglm with warning suppression — provides vec3, quat, mat4 for TransformComponent
- `nt_assert.h`: NT_ASSERT macro via __builtin_trap() — use for handle validation
- `nt_types.h`: nt_result_t enum — use for init return values

### Established Patterns
- Module state: `static struct { ... } s_state;` for internal module data
- Descriptor at init: `nt_<module>_init(const nt_<module>_desc_t* desc)` pattern
- CMake modules: `nt_add_module()` macro, targets get `nt::` aliases
- Include paths: `#include "<module_dir>/<header>.h"` from engine/ root
- Testing: Unity framework, `tests/unit/test_<module>.c`, setUp/tearDown per test

### Integration Points
- Entity handles will be used by all component modules (add/get/has/remove take nt_entity_t)
- Component storages register with nt_entity for auto-cleanup on destroy
- TransformComponent.world_matrix feeds into render pipeline (Phase 27) as model matrix
- RenderStateComponent.color feeds into render pipeline as u_color uniform
- MeshComponent/MaterialComponent reference asset handles from Phase 24-26

</code_context>

<specifics>
## Specific Ideas

- Storage is always module-internal static — game never creates or manages storage structs, only calls functions
- Registration should support debug inspection: "show me all components on this entity" via name + has() per storage
- Color stays in RenderState because render pipeline always needs it (avoids per-entity if-check). When instancing comes, color moves from uniform to instance buffer — data stays in same component
- No generic void* component API — every component type has its own typed functions

</specifics>

<deferred>
## Deferred Ideas

Migrated to GitHub issues — see label `deferred`.

</deferred>

---

*Phase: 22-entity-system*
*Context gathered: 2026-03-16*
