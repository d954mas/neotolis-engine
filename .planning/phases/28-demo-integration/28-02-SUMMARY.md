---
phase: 28-demo-integration
plan: 02
subsystem: graphics, resource
tags: [ubo, uniform-buffer, global-blocks, blob-asset, resource-api]

# Dependency graph
requires:
  - phase: 28-01
    provides: NT_ASSET_BLOB type, NtBlobAssetHeader format
  - phase: 27-01
    provides: Hardcoded Globals UBO auto-bind at pipeline creation
provides:
  - nt_gfx_register_global_block() API for game-registered UBO blocks
  - nt_gfx_get_global_blocks() accessor for backend iteration
  - nt_resource_get_blob() for raw blob data access
  - NT_ASSET_BLOB auto-ready on parse (no activator needed)
affects: [28-03, 28-04, 28-05]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Game registers named UBO blocks before pipeline creation"
    - "Blob assets auto-transition to READY during parse_pack"

key-files:
  created: []
  modified:
    - engine/graphics/nt_gfx.h
    - engine/graphics/nt_gfx.c
    - engine/graphics/gl/nt_gfx_gl.c
    - engine/resource/nt_resource.h
    - engine/resource/nt_resource.c
    - tests/unit/test_gfx.c
    - tests/unit/test_resource.c

key-decisions:
  - "Global block registry as file-scope static array in nt_gfx.c with getter function for backend access"
  - "NT_ASSET_BLOB auto-marked READY during parse_pack (no activator, no GPU upload)"
  - "nt_resource_get_blob scans asset metadata to find blob data pointer within pack blob"

patterns-established:
  - "Game registers UBO blocks via nt_gfx_register_global_block before creating pipelines"
  - "Blob assets bypass activation loop -- data accessed directly from pack blob"

requirements-completed: [DEMO-04]

# Metrics
duration: 10min
completed: 2026-03-20
---

# Phase 28 Plan 02: Global Blocks API and Blob Resource Access Summary

**Registered global UBO blocks API replacing hardcoded Globals auto-bind, plus nt_resource_get_blob for raw NT_ASSET_BLOB access**

## Performance

- **Duration:** 10 min
- **Started:** 2026-03-20T09:05:29Z
- **Completed:** 2026-03-20T09:16:11Z
- **Tasks:** 2
- **Files modified:** 7

## Accomplishments
- Replaced hardcoded "Globals" UBO auto-bind with game-registered global blocks (supports Lighting, Globals, and any custom UBOs)
- Added nt_resource_get_blob() to retrieve raw blob data from loaded NT_ASSET_BLOB resources
- Blob assets auto-transition to READY after pack parse without needing an activator
- 7 new unit tests (3 gfx + 4 resource), all 23 test targets pass

## Task Commits

Each task was committed atomically:

1. **Task 1: Add nt_gfx_register_global_block and replace hardcoded Globals auto-bind** - `84cfc57` (feat)
2. **Task 2: Add nt_resource_get_blob for NT_ASSET_BLOB access and unit tests** - `8e5eee9` (feat)

## Files Created/Modified
- `engine/graphics/nt_gfx.h` - Added nt_global_block_t struct, NT_GFX_MAX_GLOBAL_BLOCKS, register/get declarations
- `engine/graphics/nt_gfx.c` - Global block registry array, register/get implementation, shutdown cleanup
- `engine/graphics/gl/nt_gfx_gl.c` - Replaced hardcoded Globals auto-bind with registered block iteration loop
- `engine/resource/nt_resource.h` - Added nt_resource_get_blob() declaration
- `engine/resource/nt_resource.c` - Implemented get_blob, auto-READY for blob assets in parse_pack
- `tests/unit/test_gfx.c` - 3 new tests: register, max capacity, cleared on shutdown
- `tests/unit/test_resource.c` - 4 new tests: blob ready after parse, data content, null for non-blob, null for invalid

## Decisions Made
- Global block registry uses file-scope static array (`s_global_blocks[8]`) in nt_gfx.c with getter function `nt_gfx_get_global_blocks()` for the GL backend to access -- follows same shared-state pattern as other gfx internals
- NT_ASSET_BLOB assets auto-marked READY during `parse_pack()` (conditional on `asset_type == NT_ASSET_BLOB`) -- no activator registration needed since there is no GPU resource to create
- `nt_resource_get_blob()` scans asset metadata to find the matching READY entry and returns pointer into the pack blob after `NtBlobAssetHeader` -- O(A) scan is acceptable for resource access API

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- Game can now register "Globals" at slot 0 and "Lighting" at slot 1 before creating pipelines
- Pipeline creation auto-binds all registered blocks found in shaders
- Blob resources (e.g., scene data) accessible via nt_resource_get_blob() after pack load
- Ready for 28-03 (Sponza demo scene loading and rendering)

## Self-Check: PASSED

All 7 files verified present. Both commit hashes (84cfc57, 8e5eee9) verified in git log.

---
*Phase: 28-demo-integration*
*Completed: 2026-03-20*
