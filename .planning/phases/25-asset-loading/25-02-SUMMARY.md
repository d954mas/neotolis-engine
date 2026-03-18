---
phase: 25-asset-loading
plan: 02
subsystem: graphics
tags: [activator, mesh-table, context-loss, gpu-resources, asset-pipeline]

# Dependency graph
requires:
  - phase: 20-shared-formats
    provides: NtTextureAssetHeader, NtMeshAssetHeader, NtShaderCodeHeader format definitions
  - phase: 21-texture-pool
    provides: nt_gfx texture pool infrastructure
provides:
  - nt_gfx_activate_texture/mesh/shader for blob-to-GPU resource creation
  - nt_gfx_deactivate_texture/mesh/shader for cleanup
  - Mesh side table (NT_GFX_MAX_MESHES=128) with VBO+IBO pair management
  - nt_gfx_get_mesh_info for mesh handle lookup
  - Simplified context loss (flags only, no auto-recovery)
  - nt_shape_renderer_restore_gpu for shape renderer GPU recovery
affects: [25-03-resource-module, 26-rendering]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Activator pattern: parse blob header, validate magic/size, create GPU resources via make_* API"
    - "Buffer metadata: minimal type/usage/size kept for runtime validation, no data copies"
    - "Context loss responsibility: game/nt_resource handles recovery, nt_gfx only sets flags"

key-files:
  created: []
  modified:
    - engine/graphics/nt_gfx.h
    - engine/graphics/nt_gfx.c
    - engine/renderers/nt_shape_renderer.h
    - engine/renderers/nt_shape_renderer.c
    - tests/unit/test_gfx.c

key-decisions:
  - "Buffer metadata struct (nt_gfx_buffer_meta_t) replaces full buffer_desc copies -- 8 bytes vs heap-allocated desc with strdup'd label and memcpy'd data"
  - "Context loss wipes backend handles and mesh table but keeps pool slots allocated -- game must re-create from source"
  - "Shape renderer restore_gpu saves settings, calls shutdown+init, restores settings -- simplest correct approach"

patterns-established:
  - "Activator/deactivator pair: activate returns uint32_t handle (0 on failure), deactivate takes same handle"
  - "Mesh side table with generational handles: lower 16 bits = index, upper 16 bits = generation"

requirements-completed: [SHDR-01, SHDR-02, MESH-02]

# Metrics
duration: 10min
completed: 2026-03-18
---

# Phase 25 Plan 02: GFX Activators Summary

**Asset activators for texture/mesh/shader blob-to-GPU conversion, mesh side table with generational handles, CPU desc copy removal, and shape renderer GPU recovery**

## Performance

- **Duration:** 10 min
- **Started:** 2026-03-18T15:05:49Z
- **Completed:** 2026-03-18T15:15:51Z
- **Tasks:** 2
- **Files modified:** 5

## Accomplishments
- Added nt_gfx_activate/deactivate for texture, mesh, and shader assets -- bridge between pack blob data and live GPU resources
- Introduced mesh side table (128 slots) with VBO+IBO pairs, generational handles, and nt_gfx_get_mesh_info query
- Removed all CPU-side descriptor copies (shader source, pipeline sources, buffer data, texture descs) -- replaced with minimal 8-byte buffer metadata
- Refactored context loss from auto-recovery (restore_context) to flag-only detection -- game takes responsibility
- Added nt_shape_renderer_restore_gpu() for GPU resource re-creation after context loss
- 9 new unit tests for activators, all 19 existing tests pass without regressions

## Task Commits

Each task was committed atomically:

1. **Task 1: Add activator functions and mesh side table to nt_gfx** - `26e8647` (feat)
2. **Task 2: Refactor nt_gfx context loss and add nt_shape_restore_gpu** - `fecbe36` (refactor)

## Files Created/Modified
- `engine/graphics/nt_gfx.h` - Added nt_gfx_mesh_info_t struct, NT_GFX_MAX_MESHES, activate/deactivate declarations, get_mesh_info
- `engine/graphics/nt_gfx.c` - Activator implementations, mesh side table, buffer_metas, refactored context loss, removed CPU desc copies
- `engine/renderers/nt_shape_renderer.h` - Added nt_shape_renderer_restore_gpu() declaration
- `engine/renderers/nt_shape_renderer.c` - Implemented restore_gpu (save settings, shutdown, init, restore settings)
- `tests/unit/test_gfx.c` - 9 new activator tests: valid/bad-magic/too-small for each asset type, mesh table lifecycle

## Decisions Made
- Buffer metadata struct (nt_gfx_buffer_meta_t, 8 bytes) replaces full buffer_desc copies with strdup'd labels and memcpy'd data -- dramatically reduces per-buffer overhead
- Context loss wipes backend handles and mesh table but keeps pool slots allocated -- game must re-create resources from pack data or shader source
- Shape renderer restore_gpu uses save-shutdown-init-restore pattern rather than selective GPU recreation -- simpler, correct, same performance since init() is fast

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
None

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Activator functions are ready for nt_resource callback registration (Plan 03)
- Context loss recovery mechanism is ready: game checks g_nt_gfx.context_restored in begin_frame, calls nt_resource_invalidate() and nt_shape_renderer_restore_gpu()
- Mesh side table provides the VBO+IBO pair lookups needed for rendering mesh assets

---
*Phase: 25-asset-loading*
*Completed: 2026-03-18*
