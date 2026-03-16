---
phase: 19-isolate-glfw-behind-nt-window-api
plan: 01
subsystem: window
tags: [glfw, window-api, platform-abstraction, refactoring, c17]

# Dependency graph
requires:
  - phase: 18-desktop-gl-rendering
    provides: GLFW window, native input callbacks, desktop GL rendering
provides:
  - GLFW isolated behind nt_window API (swap_buffers, set_vsync, should_close, request_close)
  - nt_window_poll() internally calls nt_input_poll() on all platforms
  - nt_app and nt_input are GLFW-free with clean module boundaries
  - Game explicitly calls nt_window_swap_buffers() for frame presentation
affects: []

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Window drives input: nt_window_poll() calls nt_input_poll() internally"
    - "Game controls swap timing via nt_window_swap_buffers()"
    - "Conditional CMake dependencies based on platform (EMSCRIPTEN vs native)"

key-files:
  created: []
  modified:
    - engine/window/nt_window.h
    - engine/window/native/nt_window_native.c
    - engine/window/web/nt_window_web.c
    - engine/window/stub/nt_window_stub.c
    - engine/window/CMakeLists.txt
    - engine/input/native/nt_input_native.c
    - engine/input/CMakeLists.txt
    - engine/input/nt_input.h
    - engine/app/nt_app.h
    - engine/app/native/nt_app_native.c
    - examples/spinning_cube/main.c
    - examples/bench_shapes/main.c
    - examples/hello/main.c
    - tests/unit/test_window.c
    - tests/unit/test_window_native.c

key-decisions:
  - "nt_vsync_t enum moved from nt_app.h to nt_window.h -- vsync controls swap interval which is a window/surface concern"
  - "nt_input_native.c kept as explicit no-ops (not merged into stub) -- maintains consistent 3-backend structure"
  - "CMake: nt_input keeps nt_window on Emscripten (web needs g_nt_window.dpr), drops it on native"
  - "__builtin_fminf in nt_app_native.c -- same UCRT DLL import bypass as previous phases"

patterns-established:
  - "Window-drives-input: nt_window_poll() owns event polling and calls nt_input_poll() internally"
  - "Game-owns-swap: nt_window_swap_buffers() called explicitly by game, not auto-swapped by nt_app"
  - "Platform-conditional CMake deps: different link targets per platform using if(EMSCRIPTEN)/else()"

requirements-completed: [WINISO-01, WINISO-02, WINISO-03, WINISO-04, WINISO-05]

# Metrics
duration: 6min
completed: 2026-03-16
---

# Phase 19 Plan 01: Isolate GLFW behind nt_window API Summary

**GLFW consolidated into nt_window_native.c with 4 new public API functions (swap_buffers, set_vsync, should_close, request_close), making nt_app and nt_input GLFW-free**

## Performance

- **Duration:** 6 min
- **Started:** 2026-03-16T07:39:31Z
- **Completed:** 2026-03-16T07:45:32Z
- **Tasks:** 2
- **Files modified:** 15

## Accomplishments
- GLFW usage confined to exactly 2 engine .c files: nt_window_native.c (all windowing) and nt_gfx_gl_ctx_native.c (single glfwGetProcAddress)
- nt_app_native.c has zero GLFW includes, uses nt_window_should_close/set_vsync/request_close exclusively
- nt_input_native.c has zero GLFW includes, all platform functions are no-ops (callbacks live in nt_window)
- All 3 examples updated: no double-poll, explicit nt_window_swap_buffers() at end of frame
- 7 new tests (4 stub + 3 native) all passing, full suite of 11 tests green

## Task Commits

Each task was committed atomically:

1. **Task 1: Move GLFW code to nt_window, expand API, update all backends and CMake** - `e77f57f` (refactor)
2. **Task 2: Update examples and run full verification** - `0d160ea` (feat)

## Files Created/Modified
- `engine/window/nt_window.h` - Added nt_vsync_t enum + 4 new function declarations
- `engine/window/native/nt_window_native.c` - Received all GLFW callbacks, key mapping, 4 new functions
- `engine/window/web/nt_window_web.c` - Added nt_input_poll() in poll, 4 parity no-op/false functions
- `engine/window/stub/nt_window_stub.c` - Added 4 parity no-op/false functions
- `engine/window/CMakeLists.txt` - nt_window now PRIVATE links nt_input (native + web)
- `engine/input/native/nt_input_native.c` - Reduced to 3 no-op platform functions
- `engine/input/CMakeLists.txt` - Conditional: web keeps nt_window, native drops it
- `engine/input/nt_input.h` - Added comment about nt_window_poll() calling nt_input_poll()
- `engine/app/nt_app.h` - Removed nt_vsync_t enum, added #include "window/nt_window.h"
- `engine/app/native/nt_app_native.c` - Zero GLFW includes, uses nt_window_* API
- `examples/spinning_cube/main.c` - Removed nt_input_poll(), added nt_window_swap_buffers()
- `examples/bench_shapes/main.c` - Removed nt_input_poll(), added nt_window_swap_buffers()
- `examples/hello/main.c` - Removed nt_input_poll(), added nt_window_swap_buffers()
- `tests/unit/test_window.c` - Added 4 stub tests for new window functions
- `tests/unit/test_window_native.c` - Added 3 native tests for new window functions

## Decisions Made
- Moved nt_vsync_t enum from nt_app.h to nt_window.h -- vsync controls swap interval, a window/surface concern. nt_app.h includes nt_window.h (no circular dependency since nt_window.h only includes nt_types.h)
- Kept nt_input_native.c as explicit no-ops rather than merging into stub -- maintains consistent 3-backend structure (native/web/stub)
- CMake: nt_input conditionally links nt_window on Emscripten only (web needs g_nt_window.dpr for coordinate mapping) but drops it on native (all no-ops)
- Used __builtin_fminf in nt_app_native.c via nt_builtins.h -- consistent with UCRT DLL import bypass from Phase 18

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- Phase 19 is the final phase of v1.2 Runtime Renderer milestone
- All GLFW isolation complete, clean module boundaries established
- Codebase ready for next milestone work

---
*Phase: 19-isolate-glfw-behind-nt-window-api*
*Completed: 2026-03-16*
