---
phase: 30-logging-infrastructure
plan: 02
subsystem: logging
tags: [c17, logging, macros, variadic, domain-injection]

# Dependency graph
requires:
  - phase: 30-01
    provides: "Variadic log API with domain macros (NT_LOG_INFO/WARN/ERROR) and plain functions (nt_log_info/warn/error)"
provides:
  - "All 111 engine module log calls migrated to NT_LOG_* domain macros"
  - "All 25 example/test log calls migrated to variadic plain functions"
  - "Zero snprintf-then-log patterns remaining in engine or examples"
  - "Zero manual domain prefixes in engine log format strings"
affects: [30-03]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Engine modules use NT_LOG_INFO/WARN/ERROR macros (domain auto-injected via CMake)"
    - "Game/example code uses nt_log_info/warn/error plain variadic functions (no domain)"
    - "GL info log pass-through uses \"%s\" format to prevent format injection"

key-files:
  created: []
  modified:
    - "engine/resource/nt_resource.c"
    - "engine/graphics/nt_gfx.c"
    - "engine/graphics/gl/nt_gfx_gl.c"
    - "engine/window/native/nt_window_native.c"
    - "engine/renderers/nt_shape_renderer.c"
    - "engine/renderers/nt_mesh_renderer.c"
    - "engine/material/nt_material.c"
    - "examples/textured_quad/main.c"
    - "examples/sponza/main.c"

key-decisions:
  - "Removed unused #include <stdio.h> from nt_resource.c after snprintf elimination"
  - "Replaced em-dash characters in shape_renderer error message with ASCII double-dash for consistency"
  - "NT_PRINTF_ATTR on plain functions deferred to Plan 03 (was explicitly noted in Plan 01 header)"

patterns-established:
  - "Engine module log pattern: NT_LOG_ERROR(\"message\", args) -- domain auto-injected, no manual prefix"
  - "Example log pattern: nt_log_info(\"message\", args) -- plain variadic, no domain, no snprintf buffer"
  - "GL info log pattern: NT_LOG_ERROR(\"%s\", info) -- %s wrapper prevents format string injection"

requirements-completed: [LOG-01, LOG-02, LOG-07]

# Metrics
duration: 14min
completed: 2026-03-21
---

# Phase 30 Plan 02: Call Site Migration Summary

**All 111 engine module log calls migrated to NT_LOG_* domain macros, all 25 example calls to variadic plain functions, zero snprintf-then-log patterns remaining**

## Performance

- **Duration:** 14 min
- **Started:** 2026-03-21T18:33:36Z
- **Completed:** 2026-03-21T18:47:47Z
- **Tasks:** 2
- **Files modified:** 9

## Accomplishments
- Migrated all engine module log calls (111 sites across 7 files) to NT_LOG_INFO/NT_LOG_WARN/NT_LOG_ERROR domain macros
- Migrated all example log calls (25 sites across 2 files) to variadic nt_log_info/nt_log_error plain functions
- Eliminated all snprintf-then-log patterns (8 in engine, 8 in examples)
- Removed all manual domain prefixes ("nt_resource: ", "gfx: ", "mesh_renderer: ", etc.) from engine log format strings
- Added "%s" format safety for GL info_log pass-through in nt_gfx_gl.c
- Removed unused #include <stdio.h> from nt_resource.c

## Task Commits

Each task was committed atomically:

1. **Task 1: Migrate engine module log call sites to domain macros** - `8725137` (feat)
2. **Task 2: Migrate example and test call sites to variadic plain functions** - `cdce2c2` (feat)

## Files Created/Modified
- `engine/resource/nt_resource.c` - 24 log calls migrated to NT_LOG_INFO/NT_LOG_ERROR, 5 snprintf patterns removed, unused stdio.h removed
- `engine/graphics/nt_gfx.c` - 69 log calls migrated to NT_LOG_INFO/NT_LOG_ERROR, "gfx: " prefix stripped
- `engine/graphics/gl/nt_gfx_gl.c` - 3 log calls migrated to NT_LOG_ERROR with %s format safety
- `engine/window/native/nt_window_native.c` - 4 log calls migrated to NT_LOG_ERROR/NT_LOG_INFO
- `engine/renderers/nt_shape_renderer.c` - 4 log calls migrated to NT_LOG_ERROR
- `engine/renderers/nt_mesh_renderer.c` - 6 log calls migrated to NT_LOG_ERROR
- `engine/material/nt_material.c` - 1 log call migrated to NT_LOG_ERROR
- `examples/textured_quad/main.c` - 5 snprintf patterns replaced with direct variadic nt_log_info calls
- `examples/sponza/main.c` - 3 snprintf patterns replaced with direct variadic nt_log_info calls

## Decisions Made
- Removed unused `#include <stdio.h>` from nt_resource.c since no snprintf/printf calls remain
- Replaced em-dash in shape_renderer error message with ASCII double-dash for cross-platform consistency
- NT_PRINTF_ATTR restoration on plain functions intentionally left for Plan 03 per Plan 01 header note

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- All call sites migrated, ready for Plan 03 (format attributes + level filtering + tests)
- NT_PRINTF_ATTR can now be restored on plain functions since all call sites use format literals
- Log output format will be verified in Plan 03 to match LOG-07 (LEVEL:domain: message)

## Self-Check: PASSED

All 9 modified files verified present. Both task commits (8725137, cdce2c2) verified in git history. SUMMARY.md exists at expected path.

---
*Phase: 30-logging-infrastructure*
*Completed: 2026-03-21*
