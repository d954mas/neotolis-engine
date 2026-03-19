---
phase: 27-mesh-rendering-pipeline
plan: 01
subsystem: rendering
tags: [render-items, sort-keys, ubo, globals, visibility, drawable-tag, nt_hash32]

# Dependency graph
requires:
  - phase: 22-entity-component
    provides: entity system, drawable/mesh/material/transform components
  - phase: 29-hash-module
    provides: nt_hash32_t typed wrapper for render tags
provides:
  - nt_globals_t (256B std140 UBO struct)
  - nt_mesh_instance_t (80B per-instance data)
  - nt_render_item_t (12B sort+entity reference)
  - Sort key helpers (opaque, depth, z encoding)
  - nt_sort_by_key (qsort-based render item sort)
  - nt_render_is_visible (5-check visibility filter)
  - nt_gfx_get_defaults (zero-initialized globals)
  - NT_BUFFER_UNIFORM buffer type and UBO binding API
  - Drawable tag as nt_hash32_t (migrated from uint16_t)
affects: [27-02-mesh-renderer, shaders, game-code]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Render item struct with packed pragma for exact 12-byte layout"
    - "Sort key encoding via inline bit-packing helpers"
    - "UBO slot convention: slot 0 = globals, auto-bound in pipeline creation"

key-files:
  created:
    - engine/render/nt_render_defs.h
    - engine/render/nt_render_items.h
    - engine/render/nt_render_items.c
    - engine/render/nt_render_util.h
    - engine/render/nt_render_util.c
    - engine/render/CMakeLists.txt
    - tests/unit/test_render_items.c
  modified:
    - engine/drawable_comp/nt_drawable_comp.h
    - engine/drawable_comp/nt_drawable_comp.c
    - engine/drawable_comp/CMakeLists.txt
    - engine/graphics/nt_gfx.h
    - engine/graphics/nt_gfx.c
    - engine/graphics/nt_gfx_internal.h
    - engine/graphics/stub/nt_gfx_stub.c
    - engine/graphics/gl/nt_gfx_gl.c
    - engine/CMakeLists.txt
    - tests/unit/test_components.c
    - tests/unit/test_gfx.c
    - tests/CMakeLists.txt

key-decisions:
  - "pragma pack(push,1) for nt_render_item_t to achieve exact 12-byte layout (uint64_t+uint32_t)"
  - "UBO Globals block auto-bound to slot 0 via glGetUniformBlockIndex/glUniformBlockBinding in pipeline creation"

patterns-established:
  - "Render module consumer-provides-backend pattern (same as nt_shape_renderer)"
  - "GL_UNIFORM_BUFFER support through existing buffer pool infrastructure"

requirements-completed: [REND-01, REND-02]

# Metrics
duration: 11min
completed: 2026-03-19
---

# Phase 27 Plan 01: Render Infrastructure Summary

**Render defs (globals 256B, instance 80B, item 12B), sort key helpers, visibility check, UBO buffer support, and drawable tag migration to nt_hash32_t**

## Performance

- **Duration:** 11 min
- **Started:** 2026-03-19T18:27:55Z
- **Completed:** 2026-03-19T18:38:47Z
- **Tasks:** 2
- **Files modified:** 19

## Accomplishments
- New engine/render module with all foundational render types (globals, instance, render item) with compile-time size assertions
- Sort key helpers for opaque (material+mesh), depth (back-to-front), and z-order rendering
- 5-check visibility filter (alive, enabled, has drawable, visible, alpha > 0) and zero-init globals helper
- NT_BUFFER_UNIFORM through full GFX lifecycle (create/update/bind/destroy) with GL backend glBindBufferBase
- Drawable tag migrated from uint16_t to nt_hash32_t across header, implementation, and all tests
- 26 new unit tests (22 render items + 4 UBO), all existing tests updated and passing

## Task Commits

Each task was committed atomically:

1. **Task 1: Render defs, items, utilities, and drawable tag migration** - `93c4c08` (feat)
2. **Task 2: GFX UBO buffer support and stub/GL backend** - `5e35c0d` (feat)

## Files Created/Modified
- `engine/render/nt_render_defs.h` - nt_globals_t (256B), nt_mesh_instance_t (80B), nt_render_item_t (12B) with _Static_assert
- `engine/render/nt_render_items.h` - Sort key helpers (opaque, depth, z) and calc_view_depth/sort_by_key declarations
- `engine/render/nt_render_items.c` - View depth calculation (dot product) and qsort-based sort implementation
- `engine/render/nt_render_util.h` - nt_render_is_visible and nt_gfx_get_defaults declarations
- `engine/render/nt_render_util.c` - 5-check visibility filter and zero-init globals
- `engine/render/CMakeLists.txt` - Module definition linking nt_core, nt_entity, nt_drawable_comp, nt_transform_comp
- `engine/drawable_comp/nt_drawable_comp.h` - Tag type changed to nt_hash32_t, added nt_hash.h include
- `engine/drawable_comp/nt_drawable_comp.c` - Internal storage, default, swap, init updated for nt_hash32_t
- `engine/drawable_comp/CMakeLists.txt` - Added nt_hash to PUBLIC link libraries
- `engine/graphics/nt_gfx.h` - Added NT_BUFFER_UNIFORM enum, nt_gfx_bind_uniform_buffer declaration
- `engine/graphics/nt_gfx.c` - Implemented nt_gfx_bind_uniform_buffer with pool validation and type check
- `engine/graphics/nt_gfx_internal.h` - Added nt_gfx_backend_bind_uniform_buffer declaration
- `engine/graphics/stub/nt_gfx_stub.c` - No-op stub for bind_uniform_buffer
- `engine/graphics/gl/nt_gfx_gl.c` - GL_UNIFORM_BUFFER in create_buffer, glBindBufferBase in bind, Globals auto-bind in pipeline
- `engine/CMakeLists.txt` - Added add_subdirectory(render)
- `tests/unit/test_render_items.c` - 22 new tests covering all render types, sort keys, visibility, defaults
- `tests/unit/test_components.c` - Updated drawable tag tests for nt_hash32_t
- `tests/unit/test_gfx.c` - 4 new UBO tests (make, bind, update, destroy)
- `tests/CMakeLists.txt` - Added test_render_items target, nt_hash to test_components

## Decisions Made
- Used pragma pack(push,1) for nt_render_item_t to achieve exact 12-byte layout. Without packing, compiler adds 4 bytes padding after uint32_t entity (aligning struct to 8-byte boundary for uint64_t). Same pattern used throughout the project for cross-compiler layout.
- UBO Globals block auto-bound to slot 0 during pipeline creation via glGetUniformBlockIndex + glUniformBlockBinding. This means any shader declaring a "Globals" uniform block will automatically use binding point 0.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed nt_render_item_t struct padding**
- **Found during:** Task 1 (render defs)
- **Issue:** nt_render_item_t was 16 bytes due to alignment padding (uint64_t + uint32_t). Plan specified 12 bytes with _Static_assert.
- **Fix:** Added #pragma pack(push, 1) / #pragma pack(pop) around the struct definition, consistent with project pattern for cross-compiler layout.
- **Files modified:** engine/render/nt_render_defs.h
- **Verification:** _Static_assert(sizeof(nt_render_item_t) == 12) compiles successfully
- **Committed in:** 93c4c08 (Task 1 commit)

---

**Total deviations:** 1 auto-fixed (1 bug)
**Impact on plan:** Essential fix for correct struct size. No scope creep.

## Issues Encountered
None

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- All render infrastructure types, utilities, and UBO support ready for Plan 02 (mesh renderer)
- nt_render_item_t, sort key helpers, and visibility check provide the collection/sort layer
- NT_BUFFER_UNIFORM and Globals auto-binding provide UBO infrastructure for frame-level data upload
- Drawable tag now nt_hash32_t, ready for game code to use nt_hash32_str("world") etc.

---
*Phase: 27-mesh-rendering-pipeline*
*Completed: 2026-03-19*
