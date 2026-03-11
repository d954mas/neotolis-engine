---
phase: 13-web-platform-layer
plan: 02
subsystem: platform
tags: [window, canvas, emscripten, em_js, web, native, c17]

# Dependency graph
requires:
  - phase: 13-web-platform-layer/01
    provides: "nt_window_t struct, g_nt_window extern, nt_window_apply_sizes DPR math"
provides:
  - "nt_window_web.c with 4 EM_JS canvas query functions and init/poll/shutdown with change detection"
  - "nt_window_native.c with 800x600 stub init, no-op poll/shutdown"
  - "Platform-conditional CMake build: EMSCRIPTEN -> web backend, else -> native backend"
affects: [14-input-system, 15-webgl-renderer]

# Tech tracking
tech-stack:
  added: []
  patterns: ["EM_JS for all JS bridge calls (no EM_ASM)", "Platform ifdef guard with typedef fallback for empty TU", "File-scope statics for frame-to-frame change detection"]

key-files:
  created:
    - engine/window/web/nt_window_web.c
    - engine/window/native/nt_window_native.c
  modified:
    - engine/window/CMakeLists.txt

key-decisions:
  - "EM_JS double return type for numeric values (EM_JS limitation -- no uint32_t returns)"
  - "Change detection via file-scope statics comparing CSS size, device DPR, and max_dpr"

patterns-established:
  - "Window web backend follows same ifdef NT_PLATFORM_WEB + typedef fallback as nt_app_web.c"
  - "Platform backend directories: web/ for Emscripten, native/ for desktop stubs"

requirements-completed: [PLAT-01, PLAT-02, PLAT-03, PLAT-07]

# Metrics
duration: 2min
completed: 2026-03-11
---

# Phase 13 Plan 02: nt_window Platform Backends Summary

**Web EM_JS canvas backend with 4 JS bridge functions and change-detection poll, plus native 800x600 stubs, wired via platform-conditional CMake**

## Performance

- **Duration:** 2 min
- **Started:** 2026-03-11T17:10:21Z
- **Completed:** 2026-03-11T17:12:09Z
- **Tasks:** 2
- **Files modified:** 3

## Accomplishments
- nt_window_web.c provides 4 EM_JS functions: get_dpr, get_css_width, get_css_height, set_backing_size for canvas queries
- Web init reads canvas CSS size and device DPR, poll detects changes (size, DPR, max_dpr) with early exit on no change
- nt_window_native.c provides 800x600@1x stub for native testing and development
- CMakeLists.txt uses if(EMSCRIPTEN) for platform-conditional backend selection
- All 7 tests pass, all quality gates clean, zero EM_ASM usage (PLAT-07 satisfied)

## Task Commits

Each task was committed atomically:

1. **Task 1: Create platform backends and update CMake** - `1662710` (feat)
2. **Task 2: Quality gates** - no commit (verification-only task, all gates passed without changes)

## Files Created/Modified
- `engine/window/web/nt_window_web.c` - Web backend: 4 EM_JS functions, init/poll/shutdown with change detection statics
- `engine/window/native/nt_window_native.c` - Native backend: init with 800x600 defaults, no-op poll and shutdown
- `engine/window/CMakeLists.txt` - Platform-conditional build: EMSCRIPTEN -> web, else -> native

## Decisions Made
- Used `double` return type for EM_JS numeric functions (EM_JS limitation -- no uint32_t returns from JS)
- Change detection in poll compares CSS width/height, device DPR, and max_dpr to detect both external (browser resize/DPR change) and internal (game-set max_dpr) changes
- Web file uses `#ifdef NT_PLATFORM_WEB` guard with `typedef int` fallback, matching established nt_app_web.c pattern

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
None.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- nt_window module is complete with public API, shared DPR math, and both platform backends
- Ready for downstream consumers: renderer (Phase 15) can read fb_width/fb_height, input (Phase 14) can read CSS dimensions
- WASM build verification of nt_window_web.c deferred to CI (Emscripten not available in local dev environment)

## Self-Check: PASSED

All 3 created/modified files verified on disk. Commit hash 1662710 confirmed in git log.

---
*Phase: 13-web-platform-layer*
*Completed: 2026-03-11*
