---
phase: 30-logging-infrastructure
plan: 03
subsystem: logging
tags: [nt_log, builder, domain-macros, printf-migration, C17]

# Dependency graph
requires:
  - phase: 30-01
    provides: "nt_log module with domain macros (NT_LOG_INFO/WARN/ERROR) and NT_LOG_DOMAIN_DEFAULT injection"
provides:
  - "Builder tool fully wired to nt_log with domain 'builder'"
  - "All ~110 builder printf/fprintf calls replaced with NT_LOG_INFO/NT_LOG_ERROR/NT_LOG_WARN"
  - "Builder output now domain-tagged (INFO:builder:, ERROR:builder:, etc.)"
affects: [builder, logging-infrastructure]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Builder modules use NT_LOG_DOMAIN_DEFAULT via CMake compile definition"
    - "NT_BUILD_ASSERT left untouched as assertion mechanism (not logging)"

key-files:
  created: []
  modified:
    - "tools/builder/CMakeLists.txt"
    - "tools/builder/nt_builder.c"
    - "tools/builder/nt_builder_dump.c"
    - "tools/builder/nt_builder_mesh.c"
    - "tools/builder/nt_builder_scene.c"
    - "tools/builder/nt_builder_glob.c"
    - "tools/builder/nt_builder_shader.c"
    - "tools/builder/nt_builder_texture.c"
    - "tools/builder/nt_builder_tangent.c"
    - "tools/builder/nt_builder_internal.h"
    - "tools/builder/main.c"

key-decisions:
  - "NT_BUILD_ASSERT fprintf(stderr) left untouched -- it is an assertion mechanism, not a logging call"
  - "nt_builder_internal.h inline function warning converted to NT_LOG_WARN (not covered by NT_BUILD_ASSERT exclusion)"
  - "Builder executable target gets its own NT_LOG_DOMAIN_DEFAULT=builder compile definition (not inherited from library)"

patterns-established:
  - "Builder log domain: target_compile_definitions with NT_LOG_DOMAIN_DEFAULT for both library and executable targets"

requirements-completed: [LOG-01, LOG-02, LOG-04, LOG-07, LOG-08]

# Metrics
duration: 12min
completed: 2026-03-21
---

# Phase 30 Plan 03: Builder Log Migration Summary

**All ~110 builder printf/fprintf calls replaced with NT_LOG_INFO/NT_LOG_ERROR/NT_LOG_WARN domain macros, builder output now tagged as INFO:builder:/ERROR:builder:**

## Performance

- **Duration:** 12 min
- **Started:** 2026-03-21T18:33:38Z
- **Completed:** 2026-03-21T18:45:14Z
- **Tasks:** 2
- **Files modified:** 11

## Accomplishments
- Wired builder CMakeLists.txt to link nt_log and set NT_LOG_DOMAIN_DEFAULT="builder" for both library and executable targets
- Migrated all ~110 printf/fprintf logging calls across 9 builder .c files and 1 header to NT_LOG_INFO/NT_LOG_ERROR/NT_LOG_WARN
- Preserved NT_BUILD_ASSERT macro untouched (assertion mechanism, not logging)
- Builder executable now outputs domain-tagged log format: `INFO:builder: Neotolis Builder v0.1.0`

## Task Commits

Each task was committed atomically:

1. **Task 1: Wire builder to nt_log and set domain** - `424a00e` (chore)
2. **Task 2: Migrate builder printf/fprintf calls to nt_log macros** - `42331e2` (feat)

## Files Created/Modified
- `tools/builder/CMakeLists.txt` - Added nt_log linkage and NT_LOG_DOMAIN_DEFAULT="builder" for both nt_builder library and builder executable
- `tools/builder/nt_builder_internal.h` - Added #include "log/nt_log.h", converted float16 overflow warning to NT_LOG_WARN
- `tools/builder/nt_builder.c` - Migrated ~31 printf/fprintf calls to NT_LOG_INFO/NT_LOG_ERROR
- `tools/builder/nt_builder_dump.c` - Migrated ~26 printf/fprintf calls to NT_LOG_INFO/NT_LOG_ERROR/NT_LOG_WARN
- `tools/builder/nt_builder_mesh.c` - Migrated ~19 fprintf calls to NT_LOG_ERROR/NT_LOG_WARN
- `tools/builder/nt_builder_scene.c` - Migrated ~15 printf/fprintf calls to NT_LOG_INFO/NT_LOG_ERROR/NT_LOG_WARN
- `tools/builder/nt_builder_glob.c` - Migrated ~3 fprintf calls to NT_LOG_ERROR
- `tools/builder/nt_builder_shader.c` - Migrated ~4 fprintf calls to NT_LOG_ERROR
- `tools/builder/nt_builder_texture.c` - Migrated ~4 fprintf calls to NT_LOG_ERROR
- `tools/builder/nt_builder_tangent.c` - Migrated 1 fprintf call to NT_LOG_ERROR
- `tools/builder/main.c` - Migrated 2 printf calls to NT_LOG_INFO, replaced #include <stdio.h> with log/nt_log.h

## Decisions Made
- NT_BUILD_ASSERT fprintf(stderr) left untouched -- it is an assertion mechanism (fprintf+abort), not a logging call, per plan Rule 5 and research Pitfall 8
- The fprintf(stderr, "WARNING: float16 overflow...") in nt_builder_internal.h inline function was converted to NT_LOG_WARN -- this is a genuine warning log, not part of NT_BUILD_ASSERT
- Builder executable target gets its own NT_LOG_DOMAIN_DEFAULT="builder" compile definition since it is a separate CMake target from the nt_builder library

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing Critical] Converted float16 overflow warning in nt_builder_internal.h**
- **Found during:** Task 2 (migration audit)
- **Issue:** nt_builder_internal.h contained an fprintf(stderr, "WARNING: float16 overflow...") inside the nt_builder_convert_component inline function -- this is a logging warning not covered by the NT_BUILD_ASSERT exclusion
- **Fix:** Converted to NT_LOG_WARN, added #include "log/nt_log.h" to the header
- **Files modified:** tools/builder/nt_builder_internal.h
- **Verification:** Build passes, all 25 tests pass
- **Committed in:** 42331e2 (Task 2 commit)

---

**Total deviations:** 1 auto-fixed (1 missing critical)
**Impact on plan:** Necessary for complete migration coverage. No scope creep.

## Issues Encountered
None

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Builder logging fully migrated to nt_log domain system
- Phase 30 logging infrastructure complete (Plans 01-03)
- Ready for next phase (Basis Universal texture compression or other v1.4 features)

---
*Phase: 30-logging-infrastructure*
*Completed: 2026-03-21*
