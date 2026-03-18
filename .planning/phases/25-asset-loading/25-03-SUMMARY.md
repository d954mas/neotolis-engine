---
phase: 25-asset-loading
plan: 03
subsystem: resource
tags: [pack-loading, activator, budget, retry, blob-eviction, invalidation, nt_resource, nt_http, nt_fs]

# Dependency graph
requires:
  - phase: 25-01
    provides: "nt_http and nt_fs async I/O modules with generational handles"
  - phase: 25-02
    provides: "GFX activate/deactivate functions for textures, meshes, shaders"
  - phase: 24
    provides: "nt_resource module with pack mounting, parsing, virtual packs, slot resolution"
provides:
  - "Pack loading state machine (NONE->REQUESTED->DOWNLOADING->LOADED->READY->FAILED)"
  - "I/O integration: resource_step() polls nt_http/nt_fs for pack completion"
  - "Activator callback system (type-agnostic, no nt_gfx dependency)"
  - "Cost-based activation budget (mesh=1, shader=2, texture=4, default=16)"
  - "Retry policy with x1.5 exponential backoff"
  - "Per-pack blob policy (KEEP/AUTO with TTL-based eviction)"
  - "Context loss invalidation with re-download trigger for evicted blobs"
affects: [asset-integration, context-loss, render-pipeline]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Pack loading via I/O modules with state machine polling in resource_step"
    - "Activator callbacks registered per asset type, called within budget"
    - "Blob ownership: I/O-loaded blobs freed on unmount, parse_pack blobs caller-owned"

key-files:
  created: []
  modified:
    - "engine/resource/nt_resource.h"
    - "engine/resource/nt_resource_internal.h"
    - "engine/resource/nt_resource.c"
    - "engine/resource/CMakeLists.txt"
    - "tests/unit/test_resource.c"
    - "tests/CMakeLists.txt"

key-decisions:
  - "Blob ownership split: I/O-loaded blobs (io_type != NT_IO_NONE) freed on unmount; parse_pack blobs remain caller-owned"
  - "_CRT_SECURE_NO_WARNINGS on nt_resource and test_resource for strncpy on MSVC"
  - "WORKING_DIRECTORY set to CMAKE_SOURCE_DIR for test_resource (file I/O tests need project root)"

patterns-established:
  - "Pack loading via load_file/load_url -> resource_step polls I/O -> parse -> activate"
  - "Activator callbacks are type-agnostic: nt_resource has zero dependency on nt_gfx"
  - "Invalidation re-download: evicted blob + invalidation -> pack_state=NONE -> retry_time_ms=1 -> re-download on next step"

requirements-completed: [LOAD-01, LOAD-02, LOAD-03, LOAD-05]

# Metrics
duration: 15min
completed: 2026-03-18
---

# Phase 25 Plan 03: Pack Loading State Machine Summary

**Pack loading state machine with I/O polling, cost-based activation budget, x1.5 retry backoff, blob eviction, and context loss invalidation with re-download trigger**

## Performance

- **Duration:** 15 min
- **Started:** 2026-03-18T15:19:33Z
- **Completed:** 2026-03-18T15:34:33Z
- **Tasks:** 2
- **Files modified:** 6

## Accomplishments
- Pack loading state machine: mount -> load_file/load_url -> resource_step polls I/O -> parses -> activates assets
- Activator callbacks registered per type, called within per-frame cost-based budget (mesh=1, shader=2, texture=4)
- Retry policy with x1.5 exponential backoff, blob auto-eviction with TTL, context loss invalidation with re-download
- 67 total unit tests (22 new), all passing, full suite green

## Task Commits

Each task was committed atomically:

1. **Task 1: Extend nt_resource types and public API for pack loading** - `6629447` (feat)
2. **Task 2: Implement pack loading state machine, activation loop, and unit tests** - `7b0d591` (feat)

## Files Created/Modified
- `engine/resource/nt_resource.h` - Extended public API: load_file/url/auto, pack_state, activator, budget, retry, blob policy, invalidate
- `engine/resource/nt_resource_internal.h` - Extended NtPackMeta with pack_state, io linkage, retry, blob eviction, load_path[256]; added NtActivatorEntry
- `engine/resource/nt_resource.c` - Full pack loading state machine, I/O polling, activation loop, retry, blob eviction, invalidation
- `engine/resource/CMakeLists.txt` - Added nt_http, nt_fs, nt_time deps; added CRT warnings suppression
- `tests/unit/test_resource.c` - 22 new tests: loading, activation, budget, retry, blob policy, invalidation, re-download
- `tests/CMakeLists.txt` - Added CRT warnings, -U_DLL, WORKING_DIRECTORY for test_resource

## Decisions Made
- Blob ownership split: I/O-loaded blobs (io_type != NT_IO_NONE) are freed by nt_resource_unmount; blobs provided directly via parse_pack remain caller-owned. This preserves backward compatibility with Phase 24 tests.
- _CRT_SECURE_NO_WARNINGS and _CRT_NONSTDC_NO_DEPRECATE added to nt_resource target for strncpy on MSVC (same pattern as builder/fs modules)
- WORKING_DIRECTORY set to CMAKE_SOURCE_DIR for test_resource registration (file I/O tests write/read temp .neopak files relative to project root)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed double-free on unmount with caller-owned blobs**
- **Found during:** Task 2 (implementation)
- **Issue:** nt_resource_unmount unconditionally freed pack->blob, but existing tests pass caller-owned blobs via parse_pack that the test also frees
- **Fix:** Added io_type check: only free blob when io_type != NT_IO_NONE (resource system owns it)
- **Files modified:** engine/resource/nt_resource.c
- **Verification:** ASan-enabled test suite passes with 0 memory errors
- **Committed in:** 7b0d591 (Task 2 commit)

**2. [Rule 1 - Bug] Fixed parse_pack rejection during I/O-loaded pack parsing**
- **Found during:** Task 2 (implementation)
- **Issue:** Phase 0a set pack->blob before calling parse_pack in Phase 0b, but parse_pack checks blob != NULL and rejects as "already parsed"
- **Fix:** Restructured I/O completion to pass blob directly to parse_pack without pre-setting pack->blob
- **Files modified:** engine/resource/nt_resource.c
- **Verification:** File-based loading tests pass end-to-end
- **Committed in:** 7b0d591 (Task 2 commit)

**3. [Rule 3 - Blocking] Added missing includes and CRT fixes**
- **Found during:** Task 2 (implementation)
- **Issue:** snprintf implicit declaration (missing stdio.h), strncpy deprecated on MSVC, remove/fwrite linking errors
- **Fix:** Added #include <stdio.h>, _CRT_SECURE_NO_WARNINGS on target, -U_DLL on test target
- **Files modified:** engine/resource/nt_resource.c, engine/resource/CMakeLists.txt, tests/CMakeLists.txt
- **Verification:** Clean build with zero warnings
- **Committed in:** 7b0d591 (Task 2 commit)

---

**Total deviations:** 3 auto-fixed (2 bugs, 1 blocking)
**Impact on plan:** All auto-fixes necessary for correctness. No scope creep.

## Issues Encountered
- build_test_pack creates assets with rotating types (MESH, TEXTURE, SHADER). Budget test needed all-same-type pack -- added write_test_pack_multi helper.
- Blob eviction TTL test needed real time delay (spin loop insufficient on fast machines) -- used nt_time_sleep(0.005).

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Phase 25 (Asset Loading) is now complete: all 3 plans done
- nt_resource has full pack loading pipeline: mount -> load -> poll -> parse -> activate -> resolve
- Game can connect nt_gfx activate functions as activator callbacks to complete the asset pipeline
- Ready for Phase 26 (Rendering) or integration testing

---
*Phase: 25-asset-loading*
*Completed: 2026-03-18*
