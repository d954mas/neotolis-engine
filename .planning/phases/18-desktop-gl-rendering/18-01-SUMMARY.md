---
phase: 18-desktop-gl-rendering
plan: 01
subsystem: graphics
tags: [glad, glfw, opengl, cmake, desktop, vendoring]

# Dependency graph
requires:
  - phase: 15-gfx-module
    provides: "GL backend architecture (nt_gfx_gl.c, nt_gfx_gl_ctx.h platform abstraction)"
provides:
  - "glad GL 3.3 Core loader vendored in deps/glad/"
  - "GLFW 3.4 vendored in deps/glfw/"
  - "nt_gfx GL backend compiling on native with real GL functions via glad"
  - "Platform-conditional shader version prepend (#version 330 core / 300 es)"
  - "nt_window_t struct with title and resizable fields"
  - "nt_window_set_fullscreen API with web no-op stub"
  - "nt_vsync_t enum and vsync field in nt_app_t"
  - "nt_gfx_gl_ctx_native.c with gladLoadGL via GLFW"
affects: [18-02-PLAN, desktop-window, desktop-input, demo-parity]

# Tech tracking
tech-stack:
  added: [glad 2 GL 3.3 Core, GLFW 3.4]
  patterns: [platform-conditional GL headers via ifdef, clang-format-off for include order constraints]

key-files:
  created:
    - deps/glad/ (CMakeLists.txt, src/gl.c, include/glad/gl.h, include/KHR/khrplatform.h, LICENSE)
    - deps/glfw/ (vendored GLFW 3.4 tree)
    - engine/graphics/gl/nt_gfx_gl_ctx_native.c
  modified:
    - CMakeLists.txt
    - engine/window/nt_window.h
    - engine/window/web/nt_window_web.c
    - engine/window/native/nt_window_native.c
    - engine/app/nt_app.h
    - engine/graphics/gl/nt_gfx_gl.c
    - engine/graphics/CMakeLists.txt

key-decisions:
  - "glad must be included before GLFW to prevent system GL header conflict (clang-format-off guard)"
  - "nt_gfx links glfw for glfwGetProcAddress access in native context creation"
  - "Native window stub gets fullscreen no-op to prevent linker errors before Plan 02"

patterns-established:
  - "clang-format-off guard for GL include order: glad before GLFW"
  - "Non-Emscripten deps gated by if(NOT EMSCRIPTEN) in root CMakeLists.txt"

requirements-completed: [DESK-01, DESK-02]

# Metrics
duration: 6min
completed: 2026-03-13
---

# Phase 18 Plan 01: Desktop GL Foundation Summary

**Vendored glad GL 3.3 Core + GLFW 3.4, unified GL backend compiling on native via platform ifdefs, desktop API fields in window/app headers**

## Performance

- **Duration:** 6 min
- **Started:** 2026-03-13T09:29:37Z
- **Completed:** 2026-03-13T09:35:50Z
- **Tasks:** 3
- **Files modified:** 8 engine files + 175 vendored dependency files

## Accomplishments
- Vendored glad GL 3.3 Core loader and GLFW 3.4 as CMake static library targets
- GL backend now compiles on native with real GL function declarations via glad (was Emscripten-only)
- Platform-conditional shader version prepend: #version 330 core on desktop, #version 300 es on web
- API headers updated with title, resizable, fullscreen, vsync fields for desktop support
- All 10 existing tests pass with zero regressions

## Task Commits

Each task was committed atomically:

1. **Task 1: Vendor glad and GLFW dependencies** - `ef1633e` (chore)
2. **Task 2: Update API headers with desktop fields and web fullscreen stub** - `57cb4df` (feat)
3. **Task 3: GL backend platform ifdefs and native context** - `cedc733` (feat)

## Files Created/Modified
- `deps/glad/CMakeLists.txt` - glad static library CMake target
- `deps/glad/src/gl.c` - GL 3.3 Core function loader implementation
- `deps/glad/include/glad/gl.h` - GL 3.3 Core function declarations
- `deps/glad/include/KHR/khrplatform.h` - Khronos platform types
- `deps/glad/LICENSE` - glad MIT + Khronos Apache 2.0 license
- `deps/glfw/` - Vendored GLFW 3.4 (windowing/input library)
- `CMakeLists.txt` - Added GLFW + glad subdirectories for non-Emscripten builds
- `engine/window/nt_window.h` - Added title, resizable fields and fullscreen declaration
- `engine/window/web/nt_window_web.c` - Added fullscreen no-op stub for web
- `engine/window/native/nt_window_native.c` - Added fullscreen no-op stub for native (Plan 02 replaces)
- `engine/app/nt_app.h` - Added nt_vsync_t enum and vsync field
- `engine/graphics/gl/nt_gfx_gl.c` - Platform-conditional GL headers and shader version
- `engine/graphics/gl/nt_gfx_gl_ctx_native.c` - Desktop GL context creation via glad + GLFW
- `engine/graphics/CMakeLists.txt` - Native GL build with glad + glfw + OpenGL

## Decisions Made
- glad must be included before GLFW/glfw3.h to avoid system GL header conflict; guarded with clang-format-off
- nt_gfx links glfw (not just glad) because nt_gfx_gl_ctx_native.c needs glfwGetProcAddress
- Added fullscreen no-op stub to native window file (not just web) to prevent linker errors until Plan 02 implements the real GLFW version

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Added fullscreen stub to native window backend**
- **Found during:** Task 2 (header updates)
- **Issue:** Plan only specified web fullscreen stub, but native window backend also needs the function to link
- **Fix:** Added no-op nt_window_set_fullscreen to engine/window/native/nt_window_native.c
- **Files modified:** engine/window/native/nt_window_native.c
- **Verification:** Native build links successfully, all tests pass
- **Committed in:** 57cb4df (Task 2 commit)

**2. [Rule 1 - Bug] Fixed glad/GLFW include order in native context**
- **Found during:** Task 3 (native context creation)
- **Issue:** clang-format sorted GLFW before glad alphabetically, causing system GL header to be included first, breaking glad
- **Fix:** Added clang-format-off guard to enforce glad before GLFW include order
- **Files modified:** engine/graphics/gl/nt_gfx_gl_ctx_native.c
- **Verification:** Build succeeds, all tests pass
- **Committed in:** cedc733 (Task 3 commit)

**3. [Rule 3 - Blocking] Added glfw link to nt_gfx target**
- **Found during:** Task 3 (CMakeLists update)
- **Issue:** Plan specified linking glad to nt_gfx but nt_gfx_gl_ctx_native.c also needs GLFW headers for glfwGetProcAddress
- **Fix:** Added glfw to target_link_libraries for nt_gfx on non-Emscripten builds
- **Files modified:** engine/graphics/CMakeLists.txt
- **Verification:** Build succeeds, GLFW header found
- **Committed in:** cedc733 (Task 3 commit)

---

**Total deviations:** 3 auto-fixed (1 bug, 2 blocking)
**Impact on plan:** All auto-fixes necessary for correct compilation and linking. No scope creep.

## Issues Encountered
- glad2 pip install had a file permission error on Windows, but the package was already installed from a previous session and generation succeeded

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Foundation ready for Plan 02: functional GLFW window, native input, and desktop demo
- glad + GLFW vendored and building as static libraries
- GL backend compiles with real GL functions on native
- extern GLFWwindow pointer in nt_gfx_gl_ctx_native.c will be resolved when Plan 02 adds nt_window_native.c with GLFW

## Self-Check: PASSED

All 8 key files verified present. All 3 task commits verified in git log.

---
*Phase: 18-desktop-gl-rendering*
*Completed: 2026-03-13*
