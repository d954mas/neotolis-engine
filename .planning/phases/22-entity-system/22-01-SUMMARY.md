---
phase: 22-entity-system
plan: 01
subsystem: entity
tags: [entity, handle, generational-id, pool, component-storage, c17]

# Dependency graph
requires:
  - phase: 20-shared-formats
    provides: nt_core (nt_types.h, nt_assert.h), nt_log
provides:
  - nt_entity module with generational handle pool and storage registration API
  - NT_ASSERT_ALWAYS macro for release-mode critical invariant assertions
  - 15 unit tests covering entity pool lifecycle
affects: [22-entity-system, 23-component-storage, 27-render-pipeline]

# Tech tracking
tech-stack:
  added: []
  patterns: [entity-pool-free-queue-stack, generational-handle-uint16-index-gen, storage-registration-callbacks]

key-files:
  created:
    - engine/entity/nt_entity.h
    - engine/entity/nt_entity.c
    - engine/entity/CMakeLists.txt
    - tests/unit/test_entity.c
  modified:
    - engine/core/nt_assert.h
    - engine/CMakeLists.txt
    - tests/CMakeLists.txt

key-decisions:
  - "NT_ASSERT_ALWAYS added to nt_assert.h (not local to nt_entity.c) for reuse by other modules"
  - "Entity destroy increments generation and clears alive BEFORE calling storage callbacks (prevents re-add during destroy)"

patterns-established:
  - "Entity handle encoding: uint16 index (lower 16) + uint16 generation (upper 16) in uint32_t"
  - "Storage registration: name + has() + on_destroy() callback struct, max 16 registrations"
  - "Free queue stack: slot 0 reserved, indices 1..max_entities, LIFO alloc/free"

requirements-completed: [ENT-01, ENT-02, ENT-03, ENT-04]

# Metrics
duration: 5min
completed: 2026-03-16
---

# Phase 22 Plan 01: Entity Pool Core Summary

**Generational entity handle pool with free queue stack, storage registration API, NT_ASSERT_ALWAYS macro, and 15 unit tests**

## Performance

- **Duration:** 5 min
- **Started:** 2026-03-16T18:31:58Z
- **Completed:** 2026-03-16T18:37:25Z
- **Tasks:** 2
- **Files modified:** 7

## Accomplishments
- Entity pool with uint16 index + uint16 generation packed in uint32_t handle, following existing nt_gfx_pool_t pattern
- NT_ASSERT_ALWAYS macro for release-mode critical invariant assertions (stale entity handles)
- Component storage registration API ready for downstream component modules (transform, mesh, material, renderstate)
- 15 unit tests covering all entity pool behaviors: create, destroy, is_alive, enabled flags, pool capacity, slot reuse, handle encoding, storage cleanup callbacks

## Task Commits

Each task was committed atomically:

1. **Task 1: Create nt_entity module with pool, handle encoding, and storage registration** - `5537c1a` (feat)
2. **Task 2: Create entity pool unit tests** - `fc971d9` (test)

## Files Created/Modified
- `engine/entity/nt_entity.h` - Entity handle type, pool API, storage registration API (55 lines)
- `engine/entity/nt_entity.c` - Entity pool implementation with free queue stack, destroy cleanup loop (127 lines)
- `engine/entity/CMakeLists.txt` - CMake module definition, links nt_core and nt_log
- `engine/core/nt_assert.h` - Added NT_ASSERT_ALWAYS macro (fires in both debug and release)
- `engine/CMakeLists.txt` - Added add_subdirectory(entity)
- `tests/unit/test_entity.c` - 15 unit tests for entity pool (156 lines)
- `tests/CMakeLists.txt` - Added test_entity target

## Decisions Made
- NT_ASSERT_ALWAYS placed in nt_assert.h (shared utility) rather than local to nt_entity.c, since stale handle assertions in release are a cross-cutting concern
- Entity destroy increments generation and clears alive/enabled flags BEFORE calling storage callbacks, per Pitfall 5 from research -- prevents callbacks from seeing entity as alive during destruction

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
None

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Entity pool ready for component storage modules (transform, mesh, material, renderstate)
- Storage registration API accepts callbacks -- component modules will call nt_entity_register_storage() during their init
- nt_entity_max() available for component storage sparse array sizing
- All 13 tests pass (12 existing + 1 test_entity with 15 tests inside)

---
*Phase: 22-entity-system*
*Completed: 2026-03-16*
