---
phase: 27-mesh-rendering-pipeline
plan: 02
subsystem: rendering
tags: [instancing, pipeline-cache, mesh-renderer, webgl2, draw-calls]

# Dependency graph
requires:
  - phase: 27-01
    provides: render defs (globals 256B, instance 80B, item 12B), sort key helpers, UBO support, drawable tag
provides:
  - nt_mesh_renderer module with init/shutdown/draw_list/restore_gpu API
  - Pipeline cache keyed by (layout_hash << 32 | shader_handle)
  - Run detection for instanced draw call batching
  - Instance buffer packing (world_matrix 64B + color 16B = 80B per instance)
affects: [28-demo-scene, mesh-rendering, context-loss-recovery]

# Tech tracking
tech-stack:
  added: []
  patterns: [pipeline-cache-keyed-by-shader-layout, run-detection-batching, instance-buffer-vertex-divisor]

key-files:
  created:
    - engine/renderers/nt_mesh_renderer.h
    - engine/renderers/nt_mesh_renderer.c
    - tests/unit/test_mesh_renderer.c
  modified:
    - engine/renderers/CMakeLists.txt
    - tests/CMakeLists.txt

key-decisions:
  - "Real GFX shader handles for test materials (virtual packs register pool-allocated shader IDs, not arbitrary numbers)"
  - "Pipeline cache key: (layout_hash << 32 | resolved_vs) -- vs handle sufficient to identify shader pair"
  - "Instance layout uses attribute locations 4-8 (mat4 columns + color) with divisor=1"

patterns-established:
  - "Pipeline cache: linear scan with uint64 key, lazy creation on first encounter"
  - "Run detection: consecutive same material+mesh items batched into single instanced draw"
  - "Sub-batch splitting: instance_count > max_instances triggers multiple draw calls for same run"

requirements-completed: [REND-01, REND-02, REND-03]

# Metrics
duration: 9min
completed: 2026-03-19
---

# Phase 27 Plan 02: Mesh Renderer Summary

**Mesh renderer with pipeline cache, run detection, and instanced drawing -- 2-parameter draw_list per user decision (game binds globals UBO)**

## Performance

- **Duration:** 9 min
- **Started:** 2026-03-19T18:42:58Z
- **Completed:** 2026-03-19T18:52:57Z
- **Tasks:** 1 (TDD)
- **Files modified:** 5

## Accomplishments
- Mesh renderer processes sorted render item arrays with run detection (consecutive same material+mesh = single instanced draw call)
- Pipeline cache lazily creates and reuses pipelines keyed by (layout_hash << 32 | resolved_vs)
- Instance data (world_matrix 64B + color 16B = 80B) packed from transform/drawable components into dynamic buffer with vertex attribute divisor=1
- Context loss recovery clears pipeline cache and rebuilds instance buffer via save-shutdown-init pattern
- 9 unit tests covering init/shutdown, empty draw, single item, batching, different materials, alternating materials, cache reuse, cache differentiation, restore_gpu

## Task Commits

Each task was committed atomically:

1. **Task 1: Mesh renderer module with pipeline cache and instanced drawing** - `3d63c66` (feat)

## Files Created/Modified
- `engine/renderers/nt_mesh_renderer.h` - Public API: init, shutdown, draw_list(items, count), restore_gpu, test accessors
- `engine/renderers/nt_mesh_renderer.c` - Pipeline cache, run detection, instance packing, instanced draw calls (~340 lines)
- `engine/renderers/CMakeLists.txt` - Added nt_mesh_renderer module with component dependencies
- `tests/unit/test_mesh_renderer.c` - 9 unit tests with programmatic mesh blob and material creation helpers
- `tests/CMakeLists.txt` - Added test_mesh_renderer target with all required linkages

## Decisions Made
- Real GFX shader handles in tests: virtual packs register actual pool-allocated shader IDs (from nt_gfx_make_shader) instead of arbitrary numbers, since nt_gfx_make_pipeline validates shader handle pool validity
- Pipeline cache key uses resolved_vs as shader identifier (sufficient because vs+fs pair is unique per material)
- Instance layout occupies attribute locations 4-8 (after mesh vertex attributes 0-3)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed material test helper to use real GFX shader handles**
- **Found during:** Task 1 (initial test run)
- **Issue:** Test helper passed arbitrary uint32 values (10, 20) as shader handles to virtual pack registration, but nt_gfx_make_pipeline validates shader handles are valid pool entries
- **Fix:** Changed create_test_material() to first create actual GFX shaders via nt_gfx_make_shader, then register those pool handles as virtual pack runtime handles
- **Files modified:** tests/unit/test_mesh_renderer.c
- **Verification:** All 9 tests pass, pipeline creation succeeds
- **Committed in:** 3d63c66

**2. [Rule 1 - Bug] Fixed NT_ERR -> NT_ERR_INIT_FAILED and nt_log_warn -> nt_log_error**
- **Found during:** Task 1 (first build)
- **Issue:** Used non-existent NT_ERR and nt_log_warn -- actual API has NT_ERR_INIT_FAILED and only nt_log_error/nt_log_info
- **Fix:** Replaced with correct identifiers
- **Files modified:** engine/renderers/nt_mesh_renderer.c
- **Verification:** Builds clean with -Werror
- **Committed in:** 3d63c66

---

**Total deviations:** 2 auto-fixed (2 bugs)
**Impact on plan:** Both fixes necessary for compilation and test correctness. No scope creep.

## Issues Encountered
None beyond the auto-fixed deviations above.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Mesh renderer complete, ready for integration into demo scene or game code
- Game is responsible for: creating globals UBO, filling with camera/time data, binding to slot 0 before calling draw_list
- Pipeline cache rebuilds lazily after context loss (no game action needed beyond calling restore_gpu)

---
*Phase: 27-mesh-rendering-pipeline*
*Completed: 2026-03-19*
