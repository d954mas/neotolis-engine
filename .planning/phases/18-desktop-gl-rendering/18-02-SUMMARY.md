---
phase: 18-desktop-gl-rendering
plan: 02
subsystem: graphics
tags: [glfw, opengl, input, window, desktop, cmake]

# Dependency graph
requires:
  - phase: 18-desktop-gl-rendering
    plan: 01
    provides: "glad GL 3.3 Core loader, GLFW 3.4, unified GL backend on native, vsync/fullscreen API"
provides:
  - "GLFW window creation with GL 3.3 Core context (resize, fullscreen, DPR)"
  - "GLFW-integrated app loop with vsync, swap buffers, poll events, window-close"
  - "GLFW input backend: keyboard, mouse, scroll, focus-loss clear"
  - "All three examples (hello, bench_shapes, spinning_cube) building with real GL on desktop"
  - "nt_app_stub and nt_window_stub for headless test builds"
affects: [demo-parity, desktop-rendering, phase-18-complete]

# Tech tracking
tech-stack:
  added: []
  patterns: [stub backends for headless testing, -U_DLL for GLFW UCRT compatibility]

key-files:
  created:
    - engine/app/stub/nt_app_stub.c
    - engine/window/stub/nt_window_stub.c
  modified:
    - engine/window/native/nt_window_native.c
    - engine/app/native/nt_app_native.c
    - engine/input/native/nt_input_native.c
    - engine/window/CMakeLists.txt
    - engine/app/CMakeLists.txt
    - engine/input/CMakeLists.txt
    - examples/spinning_cube/CMakeLists.txt
    - examples/bench_shapes/CMakeLists.txt
    - examples/hello/CMakeLists.txt
    - CMakeLists.txt
    - tests/CMakeLists.txt
    - .vscode/launch.json

key-decisions:
  - "-U_DLL compile flag for GLFW to fix Windows UCRT dllimport linker errors (assert, sscanf, powf, wcscpy)"
  - "Stub backends (nt_app_stub, nt_window_stub) for headless tests -- same pattern as nt_input_stub, nt_gfx_stub"
  - "__builtin_fminf in app loop to bypass Windows UCRT DLL import issue (same pattern as sinf/cosf/sqrtf)"
  - "nt_window links nt_log for error reporting in native backend (glfwInit failure)"
  - "Scroll wheel 16x scaling factor to approximate web pixel-based scroll deltas"

patterns-established:
  - "Stub backend pattern extended to app and window modules (app_stub, window_stub)"
  - "-U_DLL flag for vendored C deps that use standard assert() on Windows clang+ASan"

requirements-completed: [DESK-03, DESK-04, DESK-05, DESK-06]

# Metrics
duration: 7min
completed: 2026-03-13
---

# Phase 18 Plan 02: Desktop Window/Input/App Summary

**GLFW window with GL 3.3 Core, keyboard/mouse input callbacks, vsync-aware app loop, all examples linking real GL on desktop**

## Performance

- **Duration:** 7 min
- **Started:** 2026-03-13T09:40:18Z
- **Completed:** 2026-03-13T09:47:44Z
- **Tasks:** 2 of 3 (Task 3 is human-verify checkpoint)
- **Files modified:** 13

## Accomplishments
- GLFW window with GL 3.3 Core context, framebuffer resize callback, DPR-aware sizing, fullscreen toggle
- GLFW-integrated frame loop with vsync control (off/on/adaptive), swap buffers, poll events, window-close detection
- Full keyboard/mouse input backend: 60+ key mappings, mouse button tracking, scroll wheel, focus-loss clear
- All three examples (hello, bench_shapes, spinning_cube) build and link real GL on native desktop
- Stub backends for headless tests: nt_app_stub and nt_window_stub keep all 10 tests passing

## Task Commits

Each task was committed atomically:

1. **Task 1: GLFW window, GL context, and app loop** - `4ed026b` (feat)
2. **Task 2: GLFW input backend and example CMake wiring** - `dc90fc4` (feat)
3. **Task 3: Verify desktop rendering and input parity** - PENDING (human-verify checkpoint)

## Files Created/Modified
- `engine/window/native/nt_window_native.c` - Full GLFW window: create, resize callback, fullscreen, shutdown
- `engine/app/native/nt_app_native.c` - GLFW-integrated frame loop with vsync, swap, poll, window-close
- `engine/input/native/nt_input_native.c` - GLFW callbacks for keyboard, mouse, scroll, focus
- `engine/app/stub/nt_app_stub.c` - Headless frame loop for tests (no GLFW dependency)
- `engine/window/stub/nt_window_stub.c` - Headless window stub for tests (no GLFW dependency)
- `engine/window/CMakeLists.txt` - Added glad/glfw/nt_log deps + nt_window_stub target
- `engine/app/CMakeLists.txt` - Added glad/glfw deps + nt_app_stub target
- `engine/input/CMakeLists.txt` - Added glad/glfw deps for native input backend
- `examples/spinning_cube/CMakeLists.txt` - Link nt_gfx (real GL) instead of nt_gfx_stub on native
- `examples/bench_shapes/CMakeLists.txt` - Link nt_gfx (real GL) instead of nt_gfx_stub on native
- `examples/hello/CMakeLists.txt` - Link glfw on native for GLFW-dependent window/app backends
- `CMakeLists.txt` - Added -U_DLL for GLFW to fix UCRT dllimport linker errors
- `tests/CMakeLists.txt` - test_app uses nt_app_stub, test_window uses nt_window_stub
- `.vscode/launch.json` - Desktop debug/run launch configs for all three examples

## Decisions Made
- GLFW compiled with `-U_DLL` to prevent Windows UCRT dllimport symbol errors (assert/_wassert, sscanf, powf, wcscpy) under clang+ASan. This is the same class of issue as the `__builtin_sinf` redirects.
- Created nt_app_stub and nt_window_stub following the established pattern (nt_input_stub, nt_gfx_stub). Tests link stub targets to avoid GLFW dependency in headless CI.
- Used `__builtin_fminf` in app loop to avoid the same UCRT DLL import issue that affects sinf/cosf/sqrtf.
- nt_window now links nt_log on native for error reporting when glfwInit() fails.
- Scroll wheel uses 16x multiplier to approximate web pixel-based scroll deltas (per RESEARCH.md findings).
- Always default window to resizable=true per CONTEXT.md decision.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Created nt_app_stub and nt_window_stub for headless tests**
- **Found during:** Task 1 (GLFW window and app loop)
- **Issue:** nt_app and nt_window native backends now depend on GLFW, but test_app and test_window are headless and cannot use GLFW
- **Fix:** Created stub backends (engine/app/stub/nt_app_stub.c, engine/window/stub/nt_window_stub.c) following the existing nt_input_stub/nt_gfx_stub pattern. Updated test CMakeLists.txt to link stub targets.
- **Files modified:** engine/app/stub/nt_app_stub.c, engine/window/stub/nt_window_stub.c, engine/app/CMakeLists.txt, engine/window/CMakeLists.txt, tests/CMakeLists.txt
- **Verification:** All 10 tests pass with stub targets
- **Committed in:** 4ed026b (Task 1 commit)

**2. [Rule 3 - Blocking] Added -U_DLL for GLFW to fix UCRT dllimport linker errors**
- **Found during:** Task 1 (first build attempt)
- **Issue:** GLFW uses assert(), sscanf, powf, wcscpy which resolve to __declspec(dllimport) stubs under CMake's default -D_DLL flag. The linker cannot find the dllimport thunks because we link static UCRT.
- **Fix:** Added `target_compile_options(glfw PRIVATE -U_DLL)` in root CMakeLists.txt
- **Files modified:** CMakeLists.txt
- **Verification:** All examples link successfully, all tests pass
- **Committed in:** 4ed026b (Task 1 commit)

**3. [Rule 3 - Blocking] Added glad/glfw link dependencies to window, app, and input modules**
- **Found during:** Task 1 (GLFW headers not found in window/app)
- **Issue:** Native backends include glad/gl.h and GLFW/glfw3.h but modules didn't link those targets
- **Fix:** Added `target_link_libraries(... PRIVATE glad glfw)` to window, app, and input CMakeLists.txt on non-Emscripten
- **Files modified:** engine/window/CMakeLists.txt, engine/app/CMakeLists.txt, engine/input/CMakeLists.txt
- **Verification:** Build succeeds, headers found
- **Committed in:** 4ed026b (Task 1), dc90fc4 (Task 2)

**4. [Rule 3 - Blocking] Added nt_log dependency to nt_window for error reporting**
- **Found during:** Task 1 (linker error: undefined symbol nt_log_error)
- **Issue:** nt_window_native.c calls nt_log_error() on glfwInit() failure but nt_window did not link nt_log
- **Fix:** Added `target_link_libraries(nt_window PRIVATE ... nt_log)` on non-Emscripten
- **Files modified:** engine/window/CMakeLists.txt
- **Verification:** hello.exe links successfully
- **Committed in:** 4ed026b (Task 1 commit)

---

**Total deviations:** 4 auto-fixed (4 blocking)
**Impact on plan:** All auto-fixes were necessary to resolve build/link errors. The stub backends follow an established project pattern. No scope creep.

## Issues Encountered
- Windows UCRT DLL import resolution is a recurring theme in this codebase (clang+ASan). The `-U_DLL` approach for GLFW is cleaner than per-function __builtin redirects since GLFW is vendored third-party code.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- All automation complete. Task 3 checkpoint requires human verification:
  - Run spinning_cube.exe and verify visual output matches web
  - Test keyboard and mouse input
  - Test window resize and close
  - Verify web build still works

## Self-Check: PENDING

Awaiting Task 3 human verification checkpoint.

---
*Phase: 18-desktop-gl-rendering*
*Completed: 2026-03-13*
