---
phase: 29-nt-hash-module-unified-crc32-string-hash-resource-label-hashing
plan: 02
subsystem: resource
tags: [nt_hash, nt_resource, fnv-1a, type-safety, uint64, resource-id]

requires:
  - phase: 29-01
    provides: "nt_hash module with FNV-1a 32/64, typed nt_hash32_t/nt_hash64_t wrappers"
provides:
  - "nt_resource API migrated to typed hash types (nt_hash32_t for pack_id, nt_hash64_t for resource_id)"
  - "NtAssetEntry widened to 24 bytes with uint64_t resource_id"
  - "FNV-1a duplication removed from nt_resource.c"
  - "nt_resource_hash() deleted -- callers use nt_hash64_str/nt_hash32_str directly"
affects: [29-03-builder-migration]

tech-stack:
  added: []
  patterns:
    - "Extract .value from nt_hash32_t/nt_hash64_t at API boundary, raw uint32/uint64 internally"
    - "Pack IDs use nt_hash32_t (32-bit), resource IDs use nt_hash64_t (64-bit)"

key-files:
  created: []
  modified:
    - shared/include/nt_pack_format.h
    - engine/resource/nt_resource.h
    - engine/resource/nt_resource_internal.h
    - engine/resource/nt_resource.c
    - engine/resource/CMakeLists.txt
    - tests/unit/test_resource.c
    - tests/unit/test_pack_format.c
    - examples/textured_quad/main.c

key-decisions:
  - "NtAssetEntry reordered for natural alignment: resource_id(8) + offset(4) + size(4) + format_version(2) + asset_type(1) + _pad(1) + _pad2(4) = 24 bytes"
  - "NtPackHeader.pack_id stays uint32_t in on-disk format (API extracts .value from nt_hash32_t)"
  - "Internal structs use raw uint64_t/uint32_t, API functions extract .value at boundary"
  - "PRIx64 from inttypes.h for portable 64-bit hex formatting in dump_pack"

patterns-established:
  - "Type-safe hash API boundary: public functions take nt_hash32_t/nt_hash64_t, extract .value for internal use"
  - "64-bit resource_id modulo for slot map uses explicit (uint64_t) cast to avoid implicit widening warnings"

requirements-completed: []

duration: 18min
completed: 2026-03-19
---

# Phase 29 Plan 02: Resource Migration Summary

**Migrated nt_resource module from uint32_t to typed nt_hash32_t/nt_hash64_t, widened NtAssetEntry to 24 bytes, deleted FNV-1a duplication**

## Performance

- **Duration:** 18 min
- **Started:** 2026-03-18T20:23:55Z
- **Completed:** 2026-03-18T20:41:57Z
- **Tasks:** 2
- **Files modified:** 8

## Accomplishments
- NtAssetEntry widened from 16 to 24 bytes with uint64_t resource_id for near-zero collision probability
- All nt_resource public API migrated to type-safe nt_hash32_t (pack_id) and nt_hash64_t (resource_id)
- Deleted nt_resource_hash() and FNV-1a constants -- single hash source of truth in nt_hash module
- Migrated ~100 test call sites in test_resource.c and textured_quad example
- Full test suite green (20/20 tests, 64+ resource tests passing)

## Task Commits

Each task was committed atomically:

1. **Task 1: Widen NtAssetEntry + migrate nt_resource API signatures + internal structs** - `6a2f9f1` (feat)
2. **Task 2: Migrate test_resource.c + test_pack_format.c + textured_quad example** - `96246ed` (feat)

## Files Created/Modified
- `shared/include/nt_pack_format.h` - NtAssetEntry widened to 24 bytes, uint64_t resource_id, reordered fields
- `engine/resource/nt_resource.h` - All public API uses nt_hash32_t/nt_hash64_t, deleted nt_resource_hash()
- `engine/resource/nt_resource_internal.h` - NtAssetMeta and NtResourceSlot use uint64_t resource_id
- `engine/resource/nt_resource.c` - Deleted FNV-1a constants, migrated all functions, added inttypes.h
- `engine/resource/CMakeLists.txt` - Added nt_hash to link dependencies
- `tests/unit/test_resource.c` - ~100 call sites migrated, 3 hash tests deleted (moved to test_hash.c)
- `tests/unit/test_pack_format.c` - Updated NtAssetEntry size to 24, field offsets for new layout
- `examples/textured_quad/main.c` - Pack IDs as nt_hash32_t, resource requests with nt_hash64_str

## Decisions Made
- NtAssetEntry field reordering: resource_id first (8B), then offset/size (4B each), then format/type/pad at tail, plus _pad2 for 24B total
- NtPackHeader.pack_id stays uint32_t on-disk; API layer converts with .value extraction
- Internal struct fields use raw uint64_t (not typed wrapper) for storage efficiency
- Used explicit (uint64_t) cast in slot map modulo to satisfy clang-tidy implicit widening check

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed clang-tidy implicit widening in slot map modulo**
- **Found during:** Task 2 (post-build static analysis)
- **Issue:** `resource_id % NT_SLOT_MAP_SIZE` triggered bugprone-implicit-widening-of-multiplication-result because NT_SLOT_MAP_SIZE expands to int multiplication
- **Fix:** Added explicit `(uint64_t)` cast: `resource_id % (uint64_t)NT_SLOT_MAP_SIZE`
- **Files modified:** engine/resource/nt_resource.c
- **Verification:** clang-tidy passes clean on nt_resource.c
- **Committed in:** 96246ed (Task 2 commit)

---

**Total deviations:** 1 auto-fixed (1 bug)
**Impact on plan:** Minor fix for static analysis compliance. No scope creep.

## Issues Encountered
None

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- nt_resource module fully migrated to nt_hash types
- Ready for Plan 03: builder migration to nt_hash
- All tests pass, formatting clean, static analysis clean

---
*Phase: 29-nt-hash-module-unified-crc32-string-hash-resource-label-hashing*
*Completed: 2026-03-19*
