---
gsd_state_version: 1.0
milestone: v1.2
milestone_name: Runtime Renderer
status: in-progress
stopped_at: Completed 13-01-PLAN.md
last_updated: "2026-03-11T17:07:00.000Z"
last_activity: 2026-03-11 -- Completed 13-01 nt_window contract
progress:
  total_phases: 6
  completed_phases: 1
  total_plans: 4
  completed_plans: 3
  percent: 25
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-03-10)

**Core value:** Simple, fast, predictable engine runtime -- composable features wired by game code with zero hidden magic
**Current focus:** Phase 13 - Web Platform Layer (in progress)

## Current Position

Phase: 13 of 17 (Web Platform Layer)
Plan: 1 of 2 complete
Status: 13-01 complete, ready for 13-02
Last activity: 2026-03-11 -- Completed 13-01 nt_window contract

Progress: [###░░░░░░░] 25%

## Performance Metrics

**Velocity:**
- Total plans completed: 22 (4 from v1.0 + 17 from v1.1 + 1 from v1.2)
- Average duration: ~3min
- Total execution time: --

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 1-2 (v1.0) | 4 | -- | -- |
| 3-11 (v1.1) | 17 | ~45min | ~2.6min |
| 12 (v1.2) | 2 | 13min | 6.5min |
| 13 (v1.2) | 1 | 4min | 4min |

*Updated after each plan completion*

## Accumulated Context

### Decisions

Key context carried from v1.1:
- C17 (not C23) due to Emscripten compatibility
- cglm v0.9.6 and Unity v2.6.1 vendored in deps/
- EM_JS over EM_ASM for JS bridge (avoids C17 variadic macro warning)
- Swappable backend pattern established (INTERFACE API + STATIC implementations)
- HTML shell with spinner, crash overlay, context menu blocking already in place
- nt_add_module() positional args; cmake_parse_arguments for optional-param helpers

Phase 12 execution decisions:
- Single nt_time.c with ifdef paths (not swappable backends) for timer -- code too small per platform
- Manual float_near() test helper because Unity compiled with UNITY_EXCLUDE_FLOAT
- Uppercase float suffixes (0.0F) per clang-tidy readability-uppercase-literal-suffix
- nt_app_web.c wrapped in #ifdef NT_PLATFORM_WEB with typedef fallback for native clang-tidy
- nt_app_web only built under if(EMSCRIPTEN) in CMake; nt_app_native always available
- First frame dt=0 convention: no previous timestamp, prevents initial spike

Phase 13 execution decisions:
- roundf (not lroundf) for fb dimension rounding -- lroundf unavailable in MSVC runtime on Windows
- nt_window_apply_sizes in public header since both platform backends need it
- nt_window module follows same global extern struct pattern as nt_app

Research decisions for v1.2:
- emscripten_request_animation_frame_loop over emscripten_set_main_loop (simpler, passes timestamp)
- All engine state in file-scope statics (Emscripten main loop semantics)
- Triangle-based wireframe (glLineWidth is no-op in WebGL)
- glBufferData orphan pattern for dynamic shape data (avoid buffer stalls)
- GLES3/gl3.h directly, no SDL/GLFW (zero overhead)
- WebGL 2 only: -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sUSE_WEBGL2=1

### Pending Todos

None.

### Blockers/Concerns

None.

## Session Continuity

Last session: 2026-03-11T17:07:00.000Z
Stopped at: Completed 13-01-PLAN.md
Resume file: .planning/phases/13-web-platform-layer/13-02-PLAN.md
