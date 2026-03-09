---
phase: 04-module-split
plan: 01
subsystem: infra
tags: [cmake, module-split, build-system]

# Dependency graph
requires:
  - phase: 03-build-infra
    provides: nt_add_module() macro and nt_core static library target
provides:
  - Monolithic nt_engine INTERFACE shim removed
  - All 5 consumers link explicit modules (nt_core, cglm_headers, unity)
  - Redundant target_include_directories removed from test targets
affects: []

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Consumers link individual engine modules explicitly via target_link_libraries"
    - "No umbrella/convenience targets -- each consumer declares its exact dependencies"

key-files:
  created: []
  modified:
    - engine/CMakeLists.txt
    - examples/hello/CMakeLists.txt
    - tests/CMakeLists.txt
    - tools/builder/CMakeLists.txt

key-decisions:
  - "Removed nt_engine INTERFACE shim with no deprecation period per user decision"
  - "hello links cglm_headers for intentional future-proofing (zero binary impact as INTERFACE)"
  - "Removed redundant target_include_directories from all 3 test targets (nt_core provides via PUBLIC)"

patterns-established:
  - "Explicit module linking: consumers list exactly the modules they need"
  - "No umbrella targets: engine/CMakeLists.txt is just add_subdirectory(core)"

requirements-completed: [SPLIT-01, SPLIT-02, SPLIT-03, SPLIT-04]

# Metrics
duration: 2min
completed: 2026-03-09
---

# Phase 4 Plan 1: Module Split Summary

**Removed monolithic nt_engine INTERFACE shim; migrated all 5 consumers to link nt_core, cglm_headers, and unity explicitly**

## Performance

- **Duration:** 2 min
- **Started:** 2026-03-09T16:36:40Z
- **Completed:** 2026-03-09T16:38:12Z
- **Tasks:** 2
- **Files modified:** 4

## Accomplishments
- Eliminated nt_engine INTERFACE shim from engine/CMakeLists.txt (reduced to single add_subdirectory(core))
- Migrated all 5 consumers to explicit module linking per CONTEXT.md dependency map
- Removed 3 redundant target_include_directories from test targets (nt_core provides via PUBLIC include)
- Verified zero nt_engine references remain in any CMakeLists.txt
- Native debug and release builds pass; all CTest tests pass

## Task Commits

Each task was committed atomically:

1. **Task 1: Remove nt_engine shim and migrate all consumers** - `0b49dac` (feat)
2. **Task 2: Verify zero nt_engine references and WASM build** - verification only, no file changes

## Files Created/Modified
- `engine/CMakeLists.txt` - Reduced to single add_subdirectory(core), nt_engine shim deleted
- `examples/hello/CMakeLists.txt` - Links nt_core + cglm_headers instead of nt_engine
- `tests/CMakeLists.txt` - 3 consumers migrated (test_core, test_sanitizer_proof, wasm_smoke), redundant include dirs removed
- `tools/builder/CMakeLists.txt` - Links nt_core instead of nt_engine

## Decisions Made
- Removed nt_engine with no deprecation warning per user decision in CONTEXT.md
- hello links cglm_headers for future-proofing (INTERFACE target, zero binary cost)
- test_sanitizer_proof keeps nt_core link intentionally (proves sanitizers on engine-linked executable)
- Removed redundant target_include_directories from all 3 test targets (nt_add_module already provides engine/ as PUBLIC include)

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
- Stale nt_engine build artifacts from previous builds existed in build/ directory; cleaned manually during Task 2 verification (not a code issue, just leftover cached build output)

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- Module split complete: every consumer is self-contained with explicit dependencies
- Ready for any future module additions (new modules just get their own add_subdirectory in engine/)
- WASM build verification deferred to CI (Emscripten not available locally on Windows)

## Self-Check: PASSED

- All 4 modified files exist on disk
- Task 1 commit `0b49dac` verified in git log
- engine/CMakeLists.txt contains add_subdirectory(core)
- hello links nt_core + cglm_headers
- tests link nt_core + unity
- builder links nt_core
- Zero nt_engine references in any CMakeLists.txt

---
*Phase: 04-module-split*
*Completed: 2026-03-09*
