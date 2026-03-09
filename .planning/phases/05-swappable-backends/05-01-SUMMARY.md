---
phase: 05-swappable-backends
plan: 01
subsystem: infra
tags: [cmake, alias, swappable-backends, logging, static-library]

# Dependency graph
requires:
  - phase: 03-build-infrastructure
    provides: nt_add_module() CMake function and module build policy
  - phase: 04-module-split
    provides: nt_core as standalone module using nt_add_module()
provides:
  - nt:: namespace ALIAS auto-creation for all engine modules
  - Swappable nt_log module with real (printf) and stub (no-op) implementations
  - Established pattern for one-API-multiple-implementations via link-time selection
affects: [05-02-swap-demo-test, future-swappable-modules]

# Tech tracking
tech-stack:
  added: []
  patterns: [swappable-backend-pattern, nt-namespace-alias, link-time-implementation-selection]

key-files:
  created:
    - engine/log/nt_log.h
    - engine/log/default/nt_log.c
    - engine/log/stub/nt_log_stub.c
    - engine/log/CMakeLists.txt
  modified:
    - cmake/nt_module.cmake
    - engine/CMakeLists.txt

key-decisions:
  - "ALIAS uses string(REPLACE) to strip nt_ prefix -- simple and predictable"
  - "Both implementations are fully independent STATIC libs sharing only the header"
  - "No INTERFACE CMake target for API -- contract enforced at source level via shared .h"

patterns-established:
  - "Swappable backend: shared header + separate impl folders (default/, stub/)"
  - "nt:: namespace ALIAS: every nt_add_module() creates nt::X automatically"
  - "Link-time selection: swap implementation by changing one target_link_libraries line"

requirements-completed: [INFRA-03, SWAP-01, SWAP-02]

# Metrics
duration: 2min
completed: 2026-03-09
---

# Phase 5 Plan 1: Swappable Backends - ALIAS and nt_log Module Summary

**nt:: namespace ALIAS auto-creation in nt_add_module() plus swappable nt_log module with printf-based real and no-op stub implementations**

## Performance

- **Duration:** 2 min
- **Started:** 2026-03-09T17:03:46Z
- **Completed:** 2026-03-09T17:05:24Z
- **Tasks:** 2
- **Files modified:** 6

## Accomplishments
- nt_add_module() now auto-creates nt:: namespace ALIAS for every module (configure-time typo detection)
- Shared nt_log.h API header defines the contract: nt_log_init, nt_log_info, nt_log_error
- Two independent STATIC libraries (nt_log and nt_log_stub) both compile cleanly with zero warnings
- Existing nt_core module automatically gets nt::core ALIAS; all tests still pass

## Task Commits

Each task was committed atomically:

1. **Task 1: Add nt:: ALIAS auto-creation to nt_add_module()** - `40361b0` (feat)
2. **Task 2: Create nt_log module with real and stub implementations** - `0c5d0ee` (feat)

## Files Created/Modified
- `cmake/nt_module.cmake` - Added ALIAS creation (string(REPLACE) + add_library ALIAS) and updated comment
- `engine/CMakeLists.txt` - Added add_subdirectory(log)
- `engine/log/nt_log.h` - Shared API header defining the log contract
- `engine/log/default/nt_log.c` - Real printf-based implementation
- `engine/log/stub/nt_log_stub.c` - No-op stub implementation with (void)msg for -Wunused-parameter
- `engine/log/CMakeLists.txt` - Two nt_add_module() calls creating both targets

## Decisions Made
- ALIAS derivation uses string(REPLACE "nt_" "" ...) -- simple prefix strip, works for nt_core, nt_log, nt_log_stub
- No separate INTERFACE CMake target for the API contract -- shared header file is the contract (per CONTEXT.md)
- Both implementations are fully independent (no dependency between nt_log and nt_log_stub)

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- Swappable backend pattern established and verified
- Ready for Plan 02: swap demonstration test targets linking both implementations
- nt::log and nt::log_stub ALIAS targets available for use in test CMakeLists.txt

## Self-Check: PASSED

All 6 files verified on disk. Both commit hashes (40361b0, 0c5d0ee) confirmed in git log. Both library outputs (nt_log.lib, nt_log_stub.lib) present in build/engine/native-debug/.

---
*Phase: 05-swappable-backends*
*Completed: 2026-03-09*
