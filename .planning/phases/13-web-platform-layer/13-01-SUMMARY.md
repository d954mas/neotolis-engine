---
phase: 13-web-platform-layer
plan: 01
subsystem: platform
tags: [window, dpr, canvas, tdd, c17]

# Dependency graph
requires:
  - phase: 12-frame-lifecycle
    provides: "nt_app global struct pattern, CMake module infrastructure"
provides:
  - "nt_window_t struct with CSS size, framebuffer size, and DPR fields"
  - "g_nt_window global extern with max_dpr=2.0 default"
  - "nt_window_apply_sizes shared DPR math function"
  - "8 passing unit tests for DPR computation correctness"
affects: [13-02, 14-input-system, 15-webgl-renderer]

# Tech tracking
tech-stack:
  added: []
  patterns: ["roundf for fb dimension rounding (not lroundf -- MSVC linker compat)"]

key-files:
  created:
    - engine/window/nt_window.h
    - engine/window/nt_window.c
    - engine/window/CMakeLists.txt
    - tests/unit/test_window.c
  modified:
    - engine/CMakeLists.txt
    - tests/CMakeLists.txt

key-decisions:
  - "Used roundf instead of lroundf for MSVC/Windows linker compatibility"
  - "nt_window_apply_sizes declared in public header (both platform backends need it)"

patterns-established:
  - "Window module follows same global extern struct pattern as nt_app"
  - "DPR capping: effective = min(device, max), floored to 1.0"

requirements-completed: [PLAT-01, PLAT-02, PLAT-03]

# Metrics
duration: 4min
completed: 2026-03-11
---

# Phase 13 Plan 01: nt_window Contract Summary

**nt_window_t struct with DPR-capped framebuffer math, proven by 8 TDD unit tests covering 1x/2x/fractional/capped/floored DPR scenarios**

## Performance

- **Duration:** 4 min
- **Started:** 2026-03-11T17:03:08Z
- **Completed:** 2026-03-11T17:06:56Z
- **Tasks:** 2 (TDD RED + GREEN)
- **Files modified:** 6

## Accomplishments
- nt_window.h defines complete public contract: nt_window_t struct with 6 fields (max_dpr, width, height, fb_width, fb_height, dpr), g_nt_window extern, lifecycle declarations, apply_sizes
- nt_window.c implements shared DPR math: effective DPR = min(device, max) floored to 1.0, framebuffer dimensions via roundf
- 8 unit tests covering all specified behavior cases pass on native-debug
- CMake wiring: nt_window module linked to nt_core, test_window target registered with CTest

## Task Commits

Each task was committed atomically:

1. **TDD RED: Failing tests + stub** - `d82f976` (test)
2. **TDD GREEN: Implement apply_sizes** - `4fcf044` (feat)

_No REFACTOR phase needed -- implementation is minimal and clean._

## Files Created/Modified
- `engine/window/nt_window.h` - Public API: nt_window_t struct, g_nt_window extern, function declarations
- `engine/window/nt_window.c` - g_nt_window definition (max_dpr=2.0F), apply_sizes DPR math
- `engine/window/CMakeLists.txt` - nt_window module (shared only, no platform backends yet)
- `tests/unit/test_window.c` - 8 unit tests for DPR computation and field consistency
- `engine/CMakeLists.txt` - Added window subdirectory
- `tests/CMakeLists.txt` - Added test_window target

## Decisions Made
- Used `roundf` instead of `lroundf` for fb dimension rounding -- `lroundf` is not available in the MSVC C runtime on Windows (linker error). `roundf` produces identical results for positive values and is more portable.
- `nt_window_apply_sizes` declared in public header since both platform backends (web and native) need to call it.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Replaced lroundf with roundf for Windows/MSVC compatibility**
- **Found during:** TDD GREEN phase
- **Issue:** `lroundf` caused linker error (`undefined symbol: __declspec(dllimport) lroundf`) on Windows with Clang/MSVC runtime
- **Fix:** Replaced `lroundf(x)` with `roundf(x)` cast to `uint32_t` -- identical behavior for positive values
- **Files modified:** engine/window/nt_window.c
- **Verification:** Build succeeds, all 8 tests pass, clang-tidy clean
- **Committed in:** 4fcf044 (GREEN phase commit)

---

**Total deviations:** 1 auto-fixed (1 blocking)
**Impact on plan:** Minimal -- same mathematical behavior, more portable function.

## Issues Encountered
None beyond the lroundf linker issue documented above.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- nt_window public contract is complete and tested
- Plan 02 will add platform backends: web/nt_window_web.c (EM_JS canvas queries) and native/nt_window_native.c (stub defaults)
- g_nt_window struct ready for downstream consumers (renderer, input)

## Self-Check: PASSED

All 5 created files verified on disk. Both commit hashes (d82f976, 4fcf044) confirmed in git log.

---
*Phase: 13-web-platform-layer*
*Completed: 2026-03-11*
