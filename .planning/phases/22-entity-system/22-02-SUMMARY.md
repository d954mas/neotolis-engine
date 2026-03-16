---
phase: 22-entity-system
plan: 02
subsystem: entity
tags: [ecs, sparse-dense, transform, trs-matrix, cglm, component-storage]

# Dependency graph
requires:
  - phase: 22-entity-system/01
    provides: "Entity pool with generational handles, storage registration API"
provides:
  - "nt_transform_comp module: sparse+dense component storage pattern"
  - "Typed add/get/has/remove API for transform components"
  - "TRS world matrix computation with dirty flag optimization"
  - "Auto-cleanup on entity destroy via registered callback"
affects: [22-entity-system/03, 27-render-pipeline]

# Tech tracking
tech-stack:
  added: []
  patterns: [sparse-dense-storage, swap-and-pop-removal, component-storage-registration]

key-files:
  created:
    - engine/transform_comp/nt_transform_comp.h
    - engine/transform_comp/nt_transform_comp.c
    - engine/transform_comp/CMakeLists.txt
    - tests/unit/test_transform_comp.c
  modified:
    - engine/CMakeLists.txt

key-decisions:
  - "vec4 for quaternion rotation instead of versor to avoid cglm type compatibility warnings"
  - "Custom ASSERT_FLOAT_NEAR macro in tests because Unity float assertions disabled globally (UNITY_EXCLUDE_FLOAT)"

patterns-established:
  - "Sparse+dense component storage: sparse[max_entities+1] -> dense[capacity] with NT_INVALID_COMP_INDEX sentinel"
  - "Swap-and-pop removal: maintains packed dense array by swapping removed element with last"
  - "Component auto-cleanup: register on_destroy callback with entity system during comp_init"

requirements-completed: [COMP-01, COMP-02, COMP-03, COMP-04, XFORM-01, XFORM-02, XFORM-03]

# Metrics
duration: 11min
completed: 2026-03-16
---

# Phase 22 Plan 02: Transform Component Summary

**Sparse+dense transform component with typed API, swap-and-pop removal, and TRS world matrix computation via cglm**

## Performance

- **Duration:** 11 min
- **Started:** 2026-03-16T18:40:59Z
- **Completed:** 2026-03-16T18:52:22Z
- **Tasks:** 2
- **Files modified:** 5

## Accomplishments
- Transform component module with full sparse+dense storage pattern (canonical for all future components)
- TRS world matrix computation: T * R * S via cglm, only recomputes dirty transforms
- 15 unit tests covering lifecycle, default values, swap-and-pop, matrix computation, dirty flag, and auto-cleanup
- Auto-cleanup registration with entity system for transparent component removal on entity destroy

## Task Commits

Each task was committed atomically:

1. **Task 1: Create nt_transform_comp module** - `dd05906` (feat)
2. **Task 2: Create transform component unit tests** - `99d2cfa` (test)

## Files Created/Modified
- `engine/transform_comp/nt_transform_comp.h` - Component type, descriptor, typed public API
- `engine/transform_comp/nt_transform_comp.c` - Sparse+dense storage, swap-and-pop, TRS world matrix
- `engine/transform_comp/CMakeLists.txt` - Module definition linking nt_entity and nt_math
- `engine/CMakeLists.txt` - Added add_subdirectory(transform_comp)
- `tests/unit/test_transform_comp.c` - 15 unit tests for full component coverage

## Decisions Made
- Used `vec4` for quaternion local_rotation instead of `versor` -- cglm's versor is just float[4] and some quaternion functions accept vec4, avoiding type compatibility warnings
- Created custom `ASSERT_FLOAT_NEAR` macro in test file because Unity framework has `UNITY_EXCLUDE_FLOAT` defined globally (to avoid _fdclass linker issues on Windows with Clang)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Fixed clang-tidy readability-isolate-declaration error**
- **Found during:** Task 1 (module implementation)
- **Issue:** `mat4 translation, rotation, scale_mat, temp;` -- multiple declarations in single statement flagged by clang-tidy
- **Fix:** Split into four separate `mat4` declarations
- **Files modified:** engine/transform_comp/nt_transform_comp.c
- **Verification:** clang-tidy passes, build succeeds
- **Committed in:** dd05906 (Task 1 commit)

**2. [Rule 3 - Blocking] Worked around UNITY_EXCLUDE_FLOAT in test assertions**
- **Found during:** Task 2 (unit tests)
- **Issue:** `TEST_ASSERT_FLOAT_WITHIN` compiled to nothing due to global `UNITY_EXCLUDE_FLOAT`, causing unused variable warnings (-Werror)
- **Fix:** Created custom `ASSERT_FLOAT_NEAR` macro using `fabsf` and `TEST_ASSERT_TRUE_MESSAGE`
- **Files modified:** tests/unit/test_transform_comp.c
- **Verification:** All 15 tests pass, clang-format clean
- **Committed in:** 99d2cfa (Task 2 commit)

---

**Total deviations:** 2 auto-fixed (2 blocking)
**Impact on plan:** Both fixes necessary for build compliance. No scope creep.

## Issues Encountered
None beyond the auto-fixed deviations above.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Sparse+dense component storage pattern proven and ready for replication in Plan 03 (mesh/material/render_state components)
- Entity system fully functional with auto-cleanup on destroy
- All existing tests (entity + transform) passing

---
*Phase: 22-entity-system*
*Completed: 2026-03-16*
