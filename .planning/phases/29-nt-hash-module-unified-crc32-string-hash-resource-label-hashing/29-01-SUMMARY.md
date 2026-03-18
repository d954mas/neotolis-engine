---
phase: 29-nt-hash-module-unified-crc32-string-hash-resource-label-hashing
plan: 01
subsystem: hash
tags: [fnv-1a, hash, type-safety, benchmark, wasm]

requires:
  - phase: 20-shared-format-headers-neopak-crc32-asset-format-structs
    provides: CRC32 implementation in shared/, pack format structs
provides:
  - nt_hash module with FNV-1a 32/64-bit typed hash API
  - nt_hash32_t and nt_hash64_t struct wrappers for type safety
  - Debug label system (opt-in via NT_HASH_LABELS=1)
  - Benchmark results confirming FNV-1a as optimal choice
affects: [29-02 resource migration, 29-03 builder migration]

tech-stack:
  added: [nt_hash module]
  patterns: [typed struct hash wrappers, compile-time feature toggle for debug labels]

key-files:
  created:
    - engine/hash/nt_hash.h
    - engine/hash/nt_hash.c
    - engine/hash/CMakeLists.txt
    - tests/unit/test_hash.c
    - tools/research/hash_benchmark/main.c
    - tools/research/hash_benchmark/CMakeLists.txt
    - tools/research/hash_benchmark/RESULTS.md
  modified:
    - engine/CMakeLists.txt
    - tests/CMakeLists.txt
    - CMakeLists.txt

key-decisions:
  - "FNV-1a confirmed as winner via benchmark: fastest 32-bit on short strings, smallest code footprint"
  - "_CRT_SECURE_NO_WARNINGS for nt_hash module (strncpy in label table, same pattern as builder/fs)"
  - "Label system compiled via target_compile_definitions on both nt_hash and test_hash targets"

patterns-established:
  - "Typed hash struct wrapper: typedef struct { uint32_t value; } nt_hash32_t"
  - "Compile-time feature toggle: NT_HASH_LABELS default off, test target enables it"
  - "Research benchmark in tools/research/ directory with standalone CMakeLists.txt"

requirements-completed: []

duration: 14min
completed: 2026-03-19
---

# Phase 29 Plan 01: nt_hash Module Summary

**FNV-1a 32/64-bit hash module with typed struct wrappers, debug label system, and 4-candidate benchmark confirming algorithm selection**

## Performance

- **Duration:** 14 min
- **Started:** 2026-03-18T20:05:37Z
- **Completed:** 2026-03-18T20:20:20Z
- **Tasks:** 2
- **Files modified:** 10

## Accomplishments
- Created nt_hash module with FNV-1a 32-bit and 64-bit hash functions using IETF reference constants
- Typed struct wrappers (nt_hash32_t, nt_hash64_t) prevent accidental mixing with plain integers at compile time
- Debug label system with open-addressing hash table, toggled via NT_HASH_LABELS compile define
- Inline nt_hash32_str/nt_hash64_str convenience wrappers with auto-registration when labels enabled
- 16 unit tests covering known vectors, string wrappers, NULL/empty input, determinism, distribution, binary data, labels, lifecycle
- Benchmark harness comparing FNV-1a, xxHash, MurmurHash3, CRC32 at both 32/64-bit widths
- FNV-1a confirmed as optimal: fastest 32-bit on short strings, smallest code, WASM-friendly

## Task Commits

Each task was committed atomically:

1. **Task 1: Create nt_hash module with FNV-1a 32/64 + label system + unit tests**
   - `d47a316` (test: add failing tests -- RED phase)
   - `e0d9fed` (feat: implement FNV-1a + label system -- GREEN phase)
2. **Task 2: Hash benchmark harness with all 4 candidates and RESULTS.md** - `0d7e300` (feat)

## Files Created/Modified
- `engine/hash/nt_hash.h` - Public API: typed hash structs, hash functions, label API, inline string helpers
- `engine/hash/nt_hash.c` - FNV-1a implementation + conditional label table
- `engine/hash/CMakeLists.txt` - Module build configuration with nt_core linkage
- `engine/CMakeLists.txt` - Added hash subdirectory before resource
- `tests/CMakeLists.txt` - Added test_hash target with NT_HASH_LABELS=1
- `tests/unit/test_hash.c` - 16 unit tests for hash correctness and label system
- `tools/research/hash_benchmark/main.c` - All 4 hash candidates, throughput + collision tests
- `tools/research/hash_benchmark/CMakeLists.txt` - Standalone benchmark build target
- `tools/research/hash_benchmark/RESULTS.md` - Benchmark results and FNV-1a selection rationale
- `CMakeLists.txt` - Added hash_benchmark subdirectory

## Decisions Made
- FNV-1a confirmed as winner via benchmark: fastest 32-bit on short strings (39.5M ops/s), smallest code (~15 lines), zero collisions on 10k sequential keys
- _CRT_SECURE_NO_WARNINGS added to nt_hash module for strncpy in label table (same pattern as builder/fs modules)
- NT_HASH_LABELS compile definition applied to both nt_hash library and test_hash target via target_compile_definitions (same pattern as NT_RESOURCE_TEST_ACCESS)
- LABEL_TABLE_SIZE macro uses explicit uint32_t cast to satisfy clang-tidy bugprone-implicit-widening check

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] _CRT_SECURE_NO_WARNINGS for strncpy**
- **Found during:** Task 1 (GREEN phase build)
- **Issue:** MSVC deprecates strncpy, causing -Werror build failure
- **Fix:** Added _CRT_SECURE_NO_WARNINGS to nt_hash CMakeLists.txt
- **Files modified:** engine/hash/CMakeLists.txt
- **Verification:** Build succeeds, all tests pass
- **Committed in:** e0d9fed (Task 1 GREEN commit)

**2. [Rule 1 - Bug] Implicit widening in LABEL_TABLE_SIZE macro**
- **Found during:** Task 1 (static analysis check)
- **Issue:** clang-tidy bugprone-implicit-widening-of-multiplication-result on LABEL_TABLE_SIZE used with uint64_t modulo
- **Fix:** Cast macro to `(uint64_t)LABEL_TABLE_SIZE` at usage sites for 64-bit label functions
- **Files modified:** engine/hash/nt_hash.c
- **Verification:** clang-tidy passes for nt_hash.c, all tests pass
- **Committed in:** e0d9fed (Task 1 GREEN commit)

---

**Total deviations:** 2 auto-fixed (1 blocking, 1 bug)
**Impact on plan:** Both auto-fixes necessary for build correctness. No scope creep.

## Issues Encountered
None.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- nt_hash module ready for Plan 02 (resource migration): resource_id will change from uint32_t to nt_hash64_t
- nt_hash module ready for Plan 03 (builder migration): nt_builder_fnv1a() will be replaced with nt_hash calls
- FNV-1a 32-bit output matches existing nt_resource_hash() (verified: "hello" -> 0x4F9F2CAB in both)
- Module builds as STATIC library and links via nt_core dependency chain

---
*Phase: 29-nt-hash-module-unified-crc32-string-hash-resource-label-hashing*
*Completed: 2026-03-19*
