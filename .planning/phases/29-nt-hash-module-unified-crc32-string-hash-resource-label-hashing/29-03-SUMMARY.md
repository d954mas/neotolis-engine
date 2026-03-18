---
phase: 29-nt-hash-module-unified-crc32-string-hash-resource-label-hashing
plan: 03
subsystem: builder
tags: [fnv1a, nt_hash, hash-migration, builder, spec]

# Dependency graph
requires:
  - phase: 29-01
    provides: nt_hash module with FNV-1a 32/64, typed wrappers, label system
provides:
  - Builder fully migrated to nt_hash (no FNV-1a duplication)
  - nt_builder_normalize_and_hash() public API (64-bit)
  - Spec document updated with nt_hash module and 64-bit resource_id
affects: [29-02, resource-system, pack-format]

# Tech tracking
tech-stack:
  added: []
  patterns: [builder-links-nt_hash, 64-bit-resource-id-internal]

key-files:
  created: []
  modified:
    - tools/builder/nt_builder_hash.c
    - tools/builder/nt_builder.h
    - tools/builder/nt_builder_internal.h
    - tools/builder/nt_builder.c
    - tools/builder/nt_builder_mesh.c
    - tools/builder/nt_builder_texture.c
    - tools/builder/nt_builder_shader.c
    - tools/builder/nt_builder_dump.c
    - tools/builder/CMakeLists.txt
    - tests/unit/test_builder.c
    - docs/neotolis_engine_spec_1.md

key-decisions:
  - "uint64_t resource_id in builder internals (NtBuildEntry, import functions, register_asset)"
  - "nt_builder_dump.c updated for 64-bit resource_id display format (0x%016llX)"

patterns-established:
  - "Builder links nt_hash and uses #include hash/nt_hash.h for all hash operations"
  - "nt_builder_normalize_and_hash returns raw uint64_t (not nt_hash64_t) to keep public API simple"

requirements-completed: []

# Metrics
duration: 12min
completed: 2026-03-19
---

# Phase 29 Plan 03: Builder Hash Migration + Spec Update Summary

**Builder FNV-1a duplication eliminated -- nt_builder_fnv1a deleted, all hash calls migrated to nt_hash module, spec updated with nt_hash section and 64-bit resource_id**

## Performance

- **Duration:** 12 min
- **Started:** 2026-03-18T20:23:57Z
- **Completed:** 2026-03-18T20:35:57Z
- **Tasks:** 2
- **Files modified:** 11

## Accomplishments
- Deleted nt_builder_fnv1a() and FNV-1a constants from builder
- Replaced nt_builder_hash() with nt_builder_normalize_and_hash() returning 64-bit hash
- All 22 test_builder tests pass with migrated API
- Spec document now documents nt_hash module (section 17.10) and 64-bit resource_id

## Task Commits

Each task was committed atomically:

1. **Task 1: Migrate builder hash calls to nt_hash + link nt_hash** - `d2f1f8e` (feat)
2. **Task 2: Update spec document for nt_hash module and 64-bit resource_id** - `1079a1f` (docs)

## Files Created/Modified
- `tools/builder/nt_builder_hash.c` - Deleted FNV-1a code, added nt_builder_normalize_and_hash() using nt_hash64_str
- `tools/builder/nt_builder.h` - Replaced nt_builder_hash() with nt_builder_normalize_and_hash()
- `tools/builder/nt_builder_internal.h` - NtBuildEntry.resource_id widened to uint64_t, deleted fnv1a declaration
- `tools/builder/nt_builder.c` - All call sites migrated to nt_hash64_str/nt_hash32_str
- `tools/builder/nt_builder_mesh.c` - Attribute name hashing uses nt_hash32_str
- `tools/builder/nt_builder_texture.c` - Import function signature updated to uint64_t resource_id
- `tools/builder/nt_builder_shader.c` - Import function signature updated to uint64_t resource_id
- `tools/builder/nt_builder_dump.c` - Resource ID display updated for 64-bit format
- `tools/builder/CMakeLists.txt` - Added nt_hash to target_link_libraries
- `tests/unit/test_builder.c` - All tests migrated to nt_builder_normalize_and_hash with uint64_t
- `docs/neotolis_engine_spec_1.md` - Added nt_hash section, updated resource_id types and examples

## Decisions Made
- Builder public API returns raw uint64_t (not nt_hash64_t) to avoid nt_hash.h dependency in nt_builder.h
- nt_builder_dump.c updated to display 64-bit resource_id as 0x%016llX (wider column)
- pack_id remains uint32_t (on-disk NtPackHeader.pack_id, uses nt_hash32_str)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Fixed nt_builder_dump.c format string for 64-bit resource_id**
- **Found during:** Task 1 (builder migration)
- **Issue:** NtAssetEntry.resource_id was already widened to uint64_t by pre-existing 29-02 changes, causing -Wformat error in dump utility
- **Fix:** Updated printf format from 0x%08X to 0x%016llX with (unsigned long long) cast, widened column headers
- **Files modified:** tools/builder/nt_builder_dump.c
- **Verification:** Build succeeds, clang-format clean
- **Committed in:** d2f1f8e (Task 1 commit)

---

**Total deviations:** 1 auto-fixed (1 blocking)
**Impact on plan:** Minor format string fix required by pre-existing pack format widening. No scope creep.

## Issues Encountered
None

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Builder fully migrated to nt_hash -- no FNV-1a duplication remains
- Plan 29-02 (resource module migration) can proceed independently
- Spec document is up to date as source of truth

---
*Phase: 29-nt-hash-module-unified-crc32-string-hash-resource-label-hashing*
*Completed: 2026-03-19*
