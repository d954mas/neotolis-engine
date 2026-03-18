---
phase: 25-asset-loading
plan: 01
subsystem: io
tags: [http, filesystem, fetch, fread, emscripten, wasm, generational-handles]

# Dependency graph
requires:
  - phase: 20-shared-formats
    provides: nt_types.h (nt_result_t), nt_core module
provides:
  - nt_http module with web/native/stub backends for HTTP requests
  - nt_fs module with web/native/stub backends for file system reads
  - Generational handle pattern for I/O request tracking
  - EM_JS fetch with ReadableStream progress for WASM HTTP
  - Native fread backend for synchronous file loading
affects: [25-02-PLAN, 25-03-PLAN, nt_resource integration]

# Tech tracking
tech-stack:
  added: [emscripten EM_JS fetch, ReadableStream API]
  patterns: [generational I/O handles, backend slot accessor pattern, take_data ownership transfer]

key-files:
  created:
    - engine/http/nt_http.h
    - engine/http/nt_http.c
    - engine/http/web/nt_http_web.c
    - engine/http/native/nt_http_native.c
    - engine/http/stub/nt_http_stub.c
    - engine/http/CMakeLists.txt
    - engine/fs/nt_fs.h
    - engine/fs/nt_fs.c
    - engine/fs/web/nt_fs_web.c
    - engine/fs/native/nt_fs_native.c
    - engine/fs/stub/nt_fs_stub.c
    - engine/fs/CMakeLists.txt
    - tests/unit/test_http.c
    - tests/unit/test_fs.c
  modified:
    - engine/CMakeLists.txt
    - tests/CMakeLists.txt

key-decisions:
  - "Backend slot accessor (nt_http_get_slot/nt_fs_get_slot) exposed via extern for backend files to update slot state"
  - "-U_DLL for nt_fs native target and test_fs (CRT file I/O on Windows, same pattern as builder)"
  - "(void)fclose() casts for cert-err33-c compliance in error paths"

patterns-established:
  - "I/O module pattern: common state in module.c, backend function as extern, backend accessor for slot updates"
  - "take_data ownership transfer: caller receives pointer, slot nulled, caller responsible for free()"

requirements-completed: [LOAD-01, LOAD-02]

# Metrics
duration: 7min
completed: 2026-03-18
---

# Phase 25 Plan 01: I/O Modules Summary

**Two general-purpose I/O modules (nt_http + nt_fs) with generational handles, swappable backends, EM_JS fetch with ReadableStream progress, and native fread -- 22 unit tests**

## Performance

- **Duration:** 7 min
- **Started:** 2026-03-18T15:05:44Z
- **Completed:** 2026-03-18T15:13:10Z
- **Tasks:** 2
- **Files modified:** 16

## Accomplishments
- nt_http module: public API with request/state/progress/take_data/free, web backend using EM_JS fetch with ReadableStream chunked progress, native/stub backends that immediately fail
- nt_fs module: public API with read_file/state/take_data/free, native backend using fopen/fread for synchronous file loading, web/stub backends that immediately fail
- Both modules use generational handles (lower 16 bits = slot index, upper 16 bits = generation) for safe request tracking
- 22 unit tests total (11 http + 11 fs) all passing with zero regressions across full test suite (19 tests)

## Task Commits

Each task was committed atomically:

1. **Task 1: Create nt_http module with web/native/stub backends** - `a619467` (feat)
2. **Task 2: Create nt_fs module with web/native/stub backends** - `872fb12` (feat)

## Files Created/Modified
- `engine/http/nt_http.h` - Public HTTP API: request, state, progress, take_data, free
- `engine/http/nt_http.c` - Common state: NtHttpSlot pool, generational handles, free queue
- `engine/http/web/nt_http_web.c` - EM_JS fetch with ReadableStream progress + KEEPALIVE callbacks
- `engine/http/native/nt_http_native.c` - Native stub (immediately fails, HTTP not in scope)
- `engine/http/stub/nt_http_stub.c` - Test stub (immediately fails all requests)
- `engine/http/CMakeLists.txt` - CMake wiring: nt_http + nt_http_stub
- `engine/fs/nt_fs.h` - Public FS API: read_file, state, take_data, free
- `engine/fs/nt_fs.c` - Common state: NtFsSlot pool, generational handles, free queue
- `engine/fs/native/nt_fs_native.c` - fopen/fread/fclose with full error checking
- `engine/fs/web/nt_fs_web.c` - Web stub (immediately fails, no FS on web)
- `engine/fs/stub/nt_fs_stub.c` - Test stub (immediately fails all requests)
- `engine/fs/CMakeLists.txt` - CMake wiring: nt_fs + nt_fs_stub, -U_DLL for native
- `engine/CMakeLists.txt` - Added http and fs subdirectories
- `tests/CMakeLists.txt` - Added test_http and test_fs targets
- `tests/unit/test_http.c` - 11 unit tests for nt_http (stub backend)
- `tests/unit/test_fs.c` - 11 unit tests for nt_fs (native backend, real file reads)

## Decisions Made
- Backend slot accessor pattern: nt_http_get_slot() and nt_fs_get_slot() exported via extern so backend .c files can update slot state directly (avoids duplicating struct definition in header or internal header)
- -U_DLL added to nt_fs native target and test_fs (Windows CRT file I/O needs static linkage, same pattern established by builder in Phase 23)
- (void)fclose() casts added in error paths for cert-err33-c clang-tidy compliance

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Added -U_DLL to nt_fs and test_fs CMake targets**
- **Found during:** Task 2 (nt_fs module)
- **Issue:** Native backend uses fopen/fread/fseek/ftell which link against dllimport CRT symbols on Windows with _DLL defined, causing linker errors
- **Fix:** Added `target_compile_options(nt_fs PRIVATE -U_DLL)` and `target_compile_options(test_fs PRIVATE -U_DLL)` following the established builder pattern
- **Files modified:** engine/fs/CMakeLists.txt, tests/CMakeLists.txt
- **Verification:** Build succeeds, all 11 fs tests pass
- **Committed in:** 872fb12 (Task 2 commit)

**2. [Rule 1 - Bug] Added (void)fclose() casts for cert-err33-c compliance**
- **Found during:** Task 2 (nt_fs module)
- **Issue:** clang-tidy cert-err33-c requires fclose() return value to be checked or explicitly discarded
- **Fix:** Added (void) casts on all fclose() calls in error paths
- **Files modified:** engine/fs/native/nt_fs_native.c
- **Verification:** clang-tidy passes with zero warnings on all new files
- **Committed in:** 872fb12 (Task 2 commit)

---

**Total deviations:** 2 auto-fixed (1 blocking, 1 bug)
**Impact on plan:** Both auto-fixes necessary for build and static analysis compliance. No scope creep.

## Issues Encountered
- Pre-existing clang-tidy crash (segfault in clang-tidy tool on unrelated file) -- not caused by new code, verified by running tidy on new files only (zero issues)
- Pre-existing build failure in nt_gfx.c (undeclared functions on 47-assets-fetch branch) -- out of scope, does not affect new modules or test execution

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- nt_http and nt_fs modules ready for integration with nt_resource in 25-02
- Web: nt_http_request() triggers JS fetch with progress, nt_resource can poll state/take_data
- Native: nt_fs_read_file() loads pack files synchronously via fread
- Both modules share identical handle pattern (generational, take_data ownership) for uniform resource loader logic

---
*Phase: 25-asset-loading*
*Completed: 2026-03-18*
