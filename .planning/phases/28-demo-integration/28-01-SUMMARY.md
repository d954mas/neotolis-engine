---
phase: 28-demo-integration
plan: 01
subsystem: builder
tags: [cgltf, mikktspace, gltf, blob, tangent, scene-parsing]

# Dependency graph
requires:
  - phase: 23-builder
    provides: "nt_builder library, cgltf/stb_image vendors, pack writer, mesh/texture/shader importers"
  - phase: 20-pack-format
    provides: "NtPackHeader, NtAssetEntry, nt_asset_type_t enum"
provides:
  - "nt_glb_scene parse/inspect/extract API for multi-mesh glTF files"
  - "NT_ASSET_BLOB asset type with NtBlobAssetHeader (8 bytes)"
  - "MikkTSpace tangent computation integration"
  - "nt_builder_add_texture_from_memory for embedded glb textures"
  - "nt_tangent_mode_t enum (AUTO/COMPUTE/REQUIRE/NONE)"
affects: [28-demo-integration, sponza-builder, sponza-demo]

# Tech tracking
tech-stack:
  added: [mikktspace]
  patterns: [parse-inspect-extract, deferred-scene-mesh, blob-asset-type, texture-from-memory]

key-files:
  created:
    - tools/builder/nt_builder_scene.c
    - tools/builder/nt_builder_blob.c
    - tools/builder/nt_builder_tangent.c
    - shared/include/nt_blob_format.h
    - deps/mikktspace/mikktspace.h
    - deps/mikktspace/mikktspace.c
    - deps/mikktspace/CMakeLists.txt
  modified:
    - tools/builder/nt_builder.h
    - tools/builder/nt_builder_internal.h
    - tools/builder/nt_builder.c
    - tools/builder/nt_builder_texture.c
    - tools/builder/CMakeLists.txt
    - shared/include/nt_pack_format.h
    - CMakeLists.txt
    - tests/unit/test_builder.c

key-decisions:
  - "MikkTSpace vendored as 2-file C library in deps/mikktspace/ with -U_DLL for static CRT"
  - "NT_ASSET_BLOB = 4 added to nt_asset_type_t; NtBlobAssetHeader is 8 bytes (magic + version + pad)"
  - "Scene mesh import uses same type conversion and interleaving pattern as nt_builder_mesh.c"
  - "Tangent mode is per-mesh configurable via nt_mesh_opts_t, not global"
  - "nt_glb_texture_t maps to cgltf images (not cgltf textures) for direct buffer_view data access"

patterns-established:
  - "Parse-inspect-extract: nt_builder_parse_glb_scene -> iterate scene.meshes/materials/textures/nodes -> nt_builder_add_scene_mesh selectively"
  - "Deferred blob/texture-mem entries: data deep-copied at add time, imported at finish_pack time"
  - "MikkTSpace callback pattern: NtMikkUserData struct with indexed vertex arrays"

requirements-completed: [DEMO-01]

# Metrics
duration: 30min
completed: 2026-03-20
---

# Phase 28 Plan 01: Builder Extensions Summary

**Multi-mesh glTF scene parsing (nt_glb_scene API), NT_ASSET_BLOB type, MikkTSpace tangent computation, and texture-from-memory support for the Sponza demo pipeline**

## Performance

- **Duration:** 30 min
- **Started:** 2026-03-20T08:30:54Z
- **Completed:** 2026-03-20T09:01:06Z
- **Tasks:** 2
- **Files modified:** 15

## Accomplishments
- Extended builder with nt_glb_scene parse/inspect/extract API that handles multi-mesh, multi-material, multi-texture glTF files via cgltf
- Added NT_ASSET_BLOB (type 4) for generic binary data assets with 8-byte NtBlobAssetHeader
- Vendored MikkTSpace library and implemented tangent computation wrapper with configurable modes (AUTO/COMPUTE/REQUIRE/NONE)
- Added texture-from-memory import path using stbi_load_from_memory for embedded glb textures
- 3 new unit tests (blob round-trip, texture-from-memory, scene parse) -- all 26 builder tests pass (23 total suite tests pass)

## Task Commits

Each task was committed atomically:

1. **Task 1: Vendor MikkTSpace, add NT_ASSET_BLOB format, extend builder API** - `a650fe8` (feat)
2. **Task 2: Implement nt_glb_scene parse/inspect/extract and unit tests** - `e72f376` (feat)

## Files Created/Modified
- `deps/mikktspace/mikktspace.h` - MikkTSpace tangent computation header
- `deps/mikktspace/mikktspace.c` - MikkTSpace implementation
- `deps/mikktspace/CMakeLists.txt` - Build target for mikktspace static lib
- `shared/include/nt_blob_format.h` - NtBlobAssetHeader definition (8 bytes)
- `shared/include/nt_pack_format.h` - Added NT_ASSET_BLOB = 4 to nt_asset_type_t
- `tools/builder/nt_builder.h` - nt_glb_scene types, tangent mode enum, mesh opts, new API functions
- `tools/builder/nt_builder_internal.h` - NtBuildBlobData, NtBuildSceneMeshData, NtBuildTexMemData, new import declarations
- `tools/builder/nt_builder.c` - finish_pack switch for BLOB/SCENE_MESH/TEXTURE_MEM, public add_blob/add_texture_from_memory
- `tools/builder/nt_builder_scene.c` - Full nt_glb_scene API: parse, free, add_scene_mesh, import_scene_mesh with tangent support
- `tools/builder/nt_builder_blob.c` - Blob import with NtBlobAssetHeader prepend
- `tools/builder/nt_builder_tangent.c` - MikkTSpace callback wrappers and nt_builder_compute_tangents
- `tools/builder/nt_builder_texture.c` - import_texture_from_memory using stbi_load_from_memory
- `tools/builder/CMakeLists.txt` - Added 3 new source files and mikktspace dependency
- `CMakeLists.txt` - Added deps/mikktspace subdirectory
- `tests/unit/test_builder.c` - 3 new tests: blob import, texture from memory, scene parse

## Decisions Made
- MikkTSpace vendored as static library with `-U_DLL` for Windows static CRT compatibility (same pattern as cgltf, stb_image)
- NtBlobAssetHeader uses pragma pack(push,1) at 8 bytes (magic + version + pad) consistent with other asset headers
- Scene textures map to cgltf_image array (not cgltf_texture) because buffer_view data is on images
- Type conversion code duplicated from nt_builder_mesh.c into nt_builder_scene.c rather than extracting shared functions (keeps files self-contained, consistent with existing pattern)
- Tangent mode is per-mesh (via nt_mesh_opts_t) not global, allowing mixed tangent handling in one pack

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Added -U_DLL to mikktspace CMakeLists.txt**
- **Found during:** Task 2 (build)
- **Issue:** MikkTSpace uses math functions (cosf, sqrtf, acosf) that get dllimport stubs under _DLL on Windows static CRT builds
- **Fix:** Added `target_compile_options(mikktspace PRIVATE -U_DLL)` consistent with other native-only targets
- **Files modified:** deps/mikktspace/CMakeLists.txt
- **Verification:** Build succeeds, all tests pass
- **Committed in:** e72f376 (Task 2 commit)

**2. [Rule 3 - Blocking] Created write_test_glb_with_node() for scene parse test**
- **Found during:** Task 2 (test)
- **Issue:** Existing write_test_glb() creates a minimal glb without nodes or scenes arrays; test_glb_scene_parse needs nodes
- **Fix:** Created write_test_glb_with_node() that includes a scene, node referencing the mesh
- **Files modified:** tests/unit/test_builder.c
- **Verification:** test_glb_scene_parse passes, verifies node->mesh_index == 0
- **Committed in:** e72f376 (Task 2 commit)

---

**Total deviations:** 2 auto-fixed (2 blocking)
**Impact on plan:** Both auto-fixes necessary for correct builds and test coverage. No scope creep.

## Issues Encountered
None

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Builder now supports multi-mesh glTF scene parsing -- ready for Sponza pack building (28-02 or later plans)
- NT_ASSET_BLOB type available for scene manifests
- Tangent computation via MikkTSpace ready for normal-mapped materials
- Texture-from-memory path enables embedded glb texture extraction

---
*Phase: 28-demo-integration*
*Completed: 2026-03-20*
