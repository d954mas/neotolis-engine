---
phase: 14-input-system
plan: 02
subsystem: input
tags: [input, emscripten, em_js, pointer-events, keyboard, web-platform, cmake]

# Dependency graph
requires:
  - phase: 14-input-system
    plan: 01
    provides: "nt_input.h API, nt_input.c shared logic, internal helpers for platform backends"
  - phase: 13-web-platform-layer
    provides: "g_nt_window.dpr for CSS-to-framebuffer coordinate mapping"
provides:
  - "nt_input_web.c: EM_JS event listeners for keyboard, pointer, wheel on canvas"
  - "nt_input_native.c: no-op desktop backend for native builds"
  - "Platform-conditional CMake build wiring EMSCRIPTEN -> web, else -> native"
affects: [15-webgl-renderer, 17-demo-integration]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "EM_JS event accumulation: JS-side arrays drained in platform_poll via KEEPALIVE wrappers"
    - "JS keyMap object mapping KeyboardEvent.code strings to C enum integers"
    - "Wheel deltaMode normalization: LINE*16, PAGE*innerHeight for cross-browser consistency"
    - "touchAction=none on canvas to prevent browser touch gestures"

key-files:
  created:
    - engine/input/web/nt_input_web.c
    - engine/input/native/nt_input_native.c
  modified:
    - engine/input/CMakeLists.txt

key-decisions:
  - "Event accumulation in JS arrays, drained during C-side platform_poll -- avoids reentrancy, matches poll-based architecture"
  - "KEEPALIVE wrapper functions (nt_input_web_on_*) instead of direct Module._ calls to internal helpers -- cleaner JS/C boundary"
  - "preventDefault on game keys (Space, Tab, arrows, F1-F4, F6-F10) but NOT F5/F11/F12 -- preserves refresh, fullscreen, devtools"
  - "pointercancel handled identically to pointerup -- prevents stuck pointers on touch interruption"

patterns-established:
  - "EM_JS event accumulation pattern: JS listeners push to arrays, C poll drains them"
  - "KEEPALIVE wrappers as JS/C bridge interface: thin C functions called via Module._name()"
  - "Platform module structure: web/ + native/ + stub/ with CMake if(EMSCRIPTEN) conditional"

requirements-completed: [PLAT-04, PLAT-05, PLAT-06]

# Metrics
duration: 4min
completed: 2026-03-12
---

# Phase 14 Plan 02: Platform Backends Summary

**Web EM_JS event listeners for keyboard/pointer/wheel with JS-side key mapping, event accumulation, and platform-conditional CMake wiring**

## Performance

- **Duration:** 4 min
- **Started:** 2026-03-11T20:12:26Z
- **Completed:** 2026-03-11T20:16:28Z
- **Tasks:** 2
- **Files modified:** 3

## Accomplishments
- Web backend with EM_JS event listeners for keyboard (69-key mapping), pointer (down/move/up/cancel), and wheel events on canvas
- JS-side event accumulation buffers drained during platform_poll via KEEPALIVE wrapper functions
- preventDefault on game keys, deltaMode normalization, pointercancel-as-pointerup, blur-clears-keys
- Native no-op backend for desktop builds (future GLFW/SDL integration point)
- Platform-conditional CMake: EMSCRIPTEN builds web backend, native builds desktop backend, stub always available

## Task Commits

Each task was committed atomically:

1. **Task 1: Web backend -- EM_JS event listeners and JS-side key mapping** - `b7844f0` (feat)
2. **Task 2: Native backend, CMake platform wiring, and quality gates** - `5bc2bdd` (feat)

## Files Created/Modified
- `engine/input/web/nt_input_web.c` - Full web input backend: KEEPALIVE wrappers, EM_JS event registration (keyboard/pointer/wheel listeners with JS keyMap), EM_JS event flush (drains accumulated events to C helpers)
- `engine/input/native/nt_input_native.c` - No-op platform backend for native desktop builds
- `engine/input/CMakeLists.txt` - Platform-conditional build: nt_input (web or native) + nt_input_stub (always), with UNIX math library linking

## Decisions Made
- **Event accumulation pattern:** JS event listeners push to Module._ntInput*Events arrays. C-side platform_poll calls EM_JS flush function that drains arrays and calls KEEPALIVE wrappers. Avoids reentrancy issues and matches the poll-based architecture.
- **KEEPALIVE wrappers:** Created thin wrapper functions (nt_input_web_on_key, on_pointer_down, etc.) rather than having JS call internal helpers directly. Cleaner JS/C boundary with explicit type conversion.
- **preventDefault scope:** Block Space, Tab, arrows, F1-F4, F6-F10. Preserve F5 (refresh), F11 (fullscreen), F12 (devtools) for developer ergonomics.
- **Stub target ordering:** Moved stub after platform nt_input in CMakeLists.txt to match logical module organization (primary target first).

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
None.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Complete nt_input module: public API, shared logic, web backend, native backend, stub, tests
- Full pipeline: browser JS events -> EM_JS accumulation -> KEEPALIVE wrappers -> shared C logic -> g_nt_input struct
- Ready for Phase 15 (WebGL renderer) and Phase 17 (demo integration) to use input polling
- All 8 existing tests pass, all quality gates clean

## Self-Check: PASSED

All files verified present:
- engine/input/web/nt_input_web.c: FOUND
- engine/input/native/nt_input_native.c: FOUND
- engine/input/CMakeLists.txt: FOUND
- .planning/phases/14-input-system/14-02-SUMMARY.md: FOUND

All commits verified:
- b7844f0: FOUND (feat: web backend)
- 5bc2bdd: FOUND (feat: native backend + CMake)

---
*Phase: 14-input-system*
*Completed: 2026-03-12*
