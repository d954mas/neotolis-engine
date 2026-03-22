---
phase: 31-material-shape-fixes
plan: 02
subsystem: renderer
tags: [shape-renderer, index-type, uint32, compile-time, wasm]

# Dependency graph
requires:
  - phase: none
    provides: none
provides:
  - nt_shape_index_t compile-time typedef (uint16 or uint32 based on MAX_VERTICES)
  - NT_SHAPE_INDEX_TYPE macro for nt_gfx IBO creation
  - Updated batch path (array, IBO, triangle, mesh) using nt_shape_index_t
  - Updated public mesh API (nt_shape_renderer_mesh, nt_shape_renderer_mesh_wire) with nt_shape_index_t parameter
affects: [shape-renderer, mesh-rendering]

# Tech tracking
tech-stack:
  added: []
  patterns: [compile-time type selection via preprocessor for index width]

key-files:
  created: []
  modified:
    - engine/renderers/nt_shape_renderer.h
    - engine/renderers/nt_shape_renderer.c
    - tests/unit/test_shape.c

key-decisions:
  - "Compile-time typedef via #if MAX_VERTICES > 65535 -- zero runtime cost, no branching"
  - "Instanced templates keep uint16_t -- their vertex counts are always small"

patterns-established:
  - "Compile-time index type selection: use nt_shape_index_t and NT_SHAPE_INDEX_TYPE for batch index buffers"

requirements-completed: [SHAPE-01, SHAPE-02]

# Metrics
duration: 4min
completed: 2026-03-22
---

# Phase 31 Plan 02: Shape Renderer Index Type Summary

**Compile-time uint16/uint32 index selection for shape renderer batch path via nt_shape_index_t typedef and NT_SHAPE_INDEX_TYPE macro**

## Performance

- **Duration:** 4 min
- **Started:** 2026-03-22T13:02:05Z
- **Completed:** 2026-03-22T13:06:13Z
- **Tasks:** 2
- **Files modified:** 3

## Accomplishments
- Added nt_shape_index_t typedef that selects uint16_t or uint32_t at compile time based on NT_SHAPE_RENDERER_MAX_VERTICES
- Updated all batch index paths (array, IBO creation/update, triangle, mesh, mesh_wire) to use nt_shape_index_t
- Removed the _Static_assert(MAX_VERTICES <= 65535) constraint that blocked large meshes
- Kept instanced templates (rect, cube, circle, sphere, cylinder, capsule) on uint16_t since their vertex counts are always small
- Added 2 new unit tests verifying index type selection and mesh batch path functionality

## Task Commits

Each task was committed atomically:

1. **Task 1: Compile-time index type selection** - `651a81c` (feat)
2. **Task 2: Shape renderer uint32 compile-time verification test** - `278fec7` (test)

## Files Created/Modified
- `engine/renderers/nt_shape_renderer.h` - Added nt_shape_index_t typedef, NT_SHAPE_INDEX_TYPE macro, updated mesh function signatures
- `engine/renderers/nt_shape_renderer.c` - Updated batch index array, IBO creation/update, triangle/mesh paths to nt_shape_index_t; removed _Static_assert
- `tests/unit/test_shape.c` - Added test_shape_index_type_default_uint16 and test_shape_mesh_batch_indices

## Decisions Made
- Compile-time typedef via #if MAX_VERTICES > 65535 -- zero runtime cost, no branching
- Instanced templates keep uint16_t -- their vertex counts are always small and never exceed 65535

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- Shape renderer now supports uint32 indices when NT_SHAPE_RENDERER_MAX_VERTICES is configured above 65535
- Default config (16384) produces identical behavior and binary size to before
- All 30 shape tests pass (28 existing + 2 new)

## Self-Check: PASSED

All files exist. All commits verified.

---
*Phase: 31-material-shape-fixes*
*Completed: 2026-03-22*
