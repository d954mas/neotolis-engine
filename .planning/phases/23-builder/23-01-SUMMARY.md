---
phase: 23-builder
plan: 01
subsystem: builder
tags: [cgltf, stb_image, fnv1a, neopak, pack-writer, crc32]

# Dependency graph
requires:
  - phase: 20-shared-formats
    provides: NtPackHeader, NtAssetEntry, nt_crc32, alignment macros
provides:
  - nt_builder STATIC library with public API
  - cgltf and stb_image vendored STATIC libraries
  - Pack writer core (start_pack/finish_pack)
  - FNV-1a hash with path normalization and duplicate detection
  - pack_dump utility for reading/validating .neopak files
  - float32-to-float16 conversion helper
affects: [23-02 mesh/texture/shader importers, 23-03 builder CLI]

# Tech tracking
tech-stack:
  added: [cgltf v1.15, stb_image]
  patterns: [builder library + executable split, _CRT_SECURE_NO_WARNINGS for Windows C I/O]

key-files:
  created:
    - tools/builder/nt_builder.h
    - tools/builder/nt_builder_internal.h
    - tools/builder/nt_builder.c
    - tools/builder/nt_builder_hash.c
    - tools/builder/nt_builder_dump.c
    - deps/cgltf/cgltf.h
    - deps/cgltf/cgltf_impl.c
    - deps/cgltf/CMakeLists.txt
    - deps/stb/stb_image.h
    - deps/stb/stb_image_impl.c
    - deps/stb/CMakeLists.txt
  modified:
    - CMakeLists.txt
    - tools/builder/CMakeLists.txt

key-decisions:
  - "_CRT_SECURE_NO_WARNINGS and _CRT_NONSTDC_NO_DEPRECATE for builder target to use standard C I/O on Windows"
  - "Inline unsigned alignment arithmetic instead of NT_PACK_ALIGN_UP macro to avoid sign-conversion warnings"
  - "nt_builder as STATIC library separate from builder executable for testability and reuse"

patterns-established:
  - "Builder library + executable split: nt_builder STATIC lib provides all functionality, builder executable is thin main.c"
  - "Vendored deps without project warning flags: third-party code in deps/ gets only add_library, no nt_set_warning_flags"

requirements-completed: [BUILD-04, BUILD-05]

# Metrics
duration: 10min
completed: 2026-03-17
---

# Phase 23 Plan 01: Builder Infrastructure Summary

**NEOPAK pack writer with cgltf/stb_image vendors, FNV-1a hashing, and pack_dump inspector -- complete builder foundation for asset importers**

## Performance

- **Duration:** 10 min
- **Started:** 2026-03-17T04:29:41Z
- **Completed:** 2026-03-17T04:39:55Z
- **Tasks:** 2
- **Files modified:** 13

## Accomplishments
- Vendored cgltf v1.15 and stb_image as STATIC libraries with isolated CMake targets (no project warning flags)
- Created complete nt_builder public API header with 15 function signatures covering pack lifecycle, asset addition, batch processing, and utilities
- Implemented pack writer core: start_pack allocates context, finish_pack writes valid NEOPAK binaries with header, entry array, aligned data, and CRC32 checksum
- Implemented FNV-1a hash with forward-slash path normalization and duplicate resource_id detection
- Implemented pack_dump that reads .neopak files and prints human-readable contents with CRC32 verification

## Task Commits

Each task was committed atomically:

1. **Task 1: Vendor cgltf + stb_image, create nt_builder public API header and internal types** - `0e1a9d4` (feat)
2. **Task 2: Pack writer core (start_pack/finish_pack) and pack_dump utility** - `e39aaaa` (feat)

## Files Created/Modified
- `tools/builder/nt_builder.h` - Complete public API: start_pack, finish_pack, add_mesh/texture/shader (single + batch), dump_pack, hash, stream layout types
- `tools/builder/nt_builder_internal.h` - NtBuilderContext struct with data buffer, entry array, duplicate tracking, error state
- `tools/builder/nt_builder.c` - Pack writer core: context lifecycle, data append with growth, asset registration with alignment, NEOPAK binary output
- `tools/builder/nt_builder_hash.c` - FNV-1a hash (0x811C9DC5 basis, 0x01000193 prime), path normalization, duplicate detection, float16 conversion
- `tools/builder/nt_builder_dump.c` - Pack inspector: reads .neopak files, validates magic/version/CRC32, prints header and entry table
- `tools/builder/CMakeLists.txt` - nt_builder STATIC library + builder executable targets with dependency linking
- `deps/cgltf/cgltf.h` - Vendored cgltf v1.15 single-header glTF parser
- `deps/cgltf/cgltf_impl.c` - cgltf implementation compilation unit
- `deps/cgltf/CMakeLists.txt` - cgltf STATIC library target
- `deps/stb/stb_image.h` - Vendored stb_image single-header image loader
- `deps/stb/stb_image_impl.c` - stb_image implementation compilation unit
- `deps/stb/CMakeLists.txt` - stb_image STATIC library target
- `CMakeLists.txt` - Added cgltf and stb subdirectories in non-Emscripten block

## Decisions Made
- Added _CRT_SECURE_NO_WARNINGS and _CRT_NONSTDC_NO_DEPRECATE compile definitions for nt_builder target because builder uses standard C I/O functions (fopen, strncpy, strdup) that MSVC deprecates
- Used inline unsigned alignment arithmetic `(size + (ALIGN - 1U)) & ~(ALIGN - 1U)` instead of NT_PACK_ALIGN_UP macro in builder code because the macro uses bare integer literals that cause sign-conversion warnings with -Wsign-conversion -Werror
- Split nt_builder into STATIC library + thin executable for testability and to allow game projects to link the builder library directly

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed clang-tidy warnings in nt_builder_hash.c**
- **Found during:** Task 1
- **Issue:** Lowercase literal suffixes (0x8000u), narrowing char conversion, unchecked fprintf return, null pointer dereference warning
- **Fix:** Uppercase suffixes (0x8000U), explicit (char) cast, (void)fprintf pattern, null check on fnv1a input
- **Files modified:** tools/builder/nt_builder_hash.c
- **Verification:** clang-tidy passes with zero errors on builder files
- **Committed in:** 0e1a9d4 (Task 1 commit)

**2. [Rule 3 - Blocking] Added MSVC CRT deprecation suppressions**
- **Found during:** Task 2
- **Issue:** fopen, strncpy, strdup deprecated on MSVC causing -Werror build failure
- **Fix:** Added _CRT_SECURE_NO_WARNINGS and _CRT_NONSTDC_NO_DEPRECATE to nt_builder target compile definitions
- **Files modified:** tools/builder/CMakeLists.txt
- **Verification:** Build completes without errors
- **Committed in:** e39aaaa (Task 2 commit)

**3. [Rule 3 - Blocking] Fixed NT_PACK_ALIGN_UP sign-conversion errors**
- **Found during:** Task 2
- **Issue:** NT_PACK_ALIGN_UP macro uses signed integer literals, causing -Wsign-conversion -Werror failures
- **Fix:** Replaced macro calls with inline unsigned arithmetic using U suffix on constants
- **Files modified:** tools/builder/nt_builder.c
- **Verification:** Build completes without sign-conversion warnings
- **Committed in:** e39aaaa (Task 2 commit)

---

**Total deviations:** 3 auto-fixed (1 bug, 2 blocking)
**Impact on plan:** All auto-fixes necessary for clean compilation under project's strict warning policy. No scope creep.

## Issues Encountered
- Network download via curl failed (SSL error code 35), resolved by using git clone instead for vendoring cgltf and stb_image

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- nt_builder library provides complete API surface for Plan 02 (mesh/texture/shader importers)
- cgltf and stb_image linked and ready for use in importer implementations
- register_asset and append_data internal helpers ready for asset importer consumption
- All 15 existing tests continue to pass

## Self-Check: PASSED

All 13 created files verified present. Both task commits (0e1a9d4, e39aaaa) verified in git log.

---
*Phase: 23-builder*
*Completed: 2026-03-17*
