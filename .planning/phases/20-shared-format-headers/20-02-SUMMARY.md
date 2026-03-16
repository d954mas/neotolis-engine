---
phase: 20-shared-format-headers
plan: 02
subsystem: shared
tags: [binary-format, mesh-format, texture-format, shader-format, struct-layout, attribute-mask]

# Dependency graph
requires:
  - phase: 20-shared-format-headers plan 01
    provides: NtPackHeader, NtAssetEntry, nt_asset_type_t, NT_PACK_ALIGN, nt_shared CMake target
provides:
  - NtMeshAssetHeader (24 bytes packed) with attribute mask aligned to NT_ATTR_* enum
  - NtTextureAssetHeader (20 bytes packed) with pixel format enum
  - NtShaderAssetHeader (24 bytes packed) with vs/fs offset layout
  - NT_MESH_ATTR_* bitmask defines matching nt_gfx.h attribute locations
  - nt_formats.h umbrella header including all five shared format headers
affects: [21-builder-skeleton, 22-mesh-importer, 23-texture-importer, 24-shader-importer, 25-runtime-asset-registry]

# Tech tracking
tech-stack:
  added: []
  patterns: [per-asset-pragma-pack-headers, attribute-mask-bitmask-pattern, magic-byte-verification-tests]

key-files:
  created:
    - shared/include/nt_mesh_format.h
    - shared/include/nt_texture_format.h
    - shared/include/nt_shader_format.h
  modified:
    - shared/include/nt_formats.h
    - tests/unit/test_pack_format.c
    - tests/CMakeLists.txt

key-decisions:
  - "Mesh attribute mask bit positions derived from NT_ATTR_* enum values (1 << NT_ATTR_POSITION etc.) ensuring pipeline can derive vertex layout from mask"
  - "Explicit _pad fields in texture and shader headers for alignment clarity and future-proofing"
  - "Added nt_core to test_pack_format link libraries to access engine/ include root for nt_gfx.h cross-verification"

patterns-established:
  - "Asset header pattern: magic(4) + version(2) + type-specific fields, all pragma pack(push,1) with _Static_assert size check"
  - "Attribute mask pattern: NT_MESH_ATTR_X = (1u << NT_ATTR_X) guarantees format-to-runtime alignment"

requirements-completed: [MESH-01, MESH-03]

# Metrics
duration: 6min
completed: 2026-03-16
---

# Phase 20 Plan 02: Asset Format Headers Summary

**Per-asset binary format headers (Mesh 24B with attribute mask, Texture 20B, Shader 24B) with pragma pack layout, magic byte verification, and 26 total unit tests proving struct sizes, field offsets, and format-to-runtime attribute alignment**

## Performance

- **Duration:** 6 min
- **Started:** 2026-03-16T13:52:35Z
- **Completed:** 2026-03-16T13:58:40Z
- **Tasks:** 2
- **Files modified:** 6

## Accomplishments
- NtMeshAssetHeader (24 bytes) with attribute_mask bitmask aligned to NT_ATTR_POSITION/NORMAL/COLOR/TEXCOORD0 from nt_gfx.h
- NtTextureAssetHeader (20 bytes) with RGBA8 pixel format enum, width/height/mip_count fields
- NtShaderAssetHeader (24 bytes) with vertex/fragment shader offset and size fields
- 12 new unit tests verifying field offsets, magic bytes ("MESH"/"TTEX"/"SHDR"), and attribute mask alignment
- 26 total tests passing (14 Plan 01 + 12 Plan 02), all existing tests unaffected

## Task Commits

Each task was committed atomically:

1. **Task 1: Create mesh, texture, and shader asset format headers** - `10f8e0d` (feat)
2. **Task 2: Add unit tests for asset format headers** - `50c9030` (test)

_Note: TDD tasks had RED commits (974a26a, 6874b82) followed by GREEN commits above_

## Files Created/Modified
- `shared/include/nt_mesh_format.h` - MeshAssetHeader struct with attribute mask bitmask, vertex/index counts, data sizes
- `shared/include/nt_texture_format.h` - TextureAssetHeader struct with pixel format enum, width/height/mip_count
- `shared/include/nt_shader_format.h` - ShaderAssetHeader struct with vs/fs offsets and sizes
- `shared/include/nt_formats.h` - Updated umbrella header to include all five shared format headers
- `tests/unit/test_pack_format.c` - Extended with 12 new tests for asset format headers
- `tests/CMakeLists.txt` - Added nt_core to test_pack_format link libraries for nt_gfx.h access

## Decisions Made
- Mesh attribute mask bit positions use `(1u << NT_ATTR_*)` to guarantee alignment with runtime attribute enum, so pipeline creation can derive vertex layout directly from the mask
- Explicit _pad fields in NtTextureAssetHeader and NtShaderAssetHeader for clarity -- matches the NtAssetEntry pattern from Plan 01
- Linked nt_core to test_pack_format test target to get the engine/ include root, enabling cross-verification that mesh attribute mask bits match NT_ATTR_* enum values

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
None

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- All shared format headers complete: pack header (24B), asset entry (16B), mesh (24B), texture (20B), shader (24B)
- Phase 20 fully complete -- ready for Phase 21 (Builder Skeleton)
- Builder can include nt_formats.h to access all struct definitions and write NEOPAK packs
- Runtime can include the same headers to read packs with guaranteed binary-compatible layout

## Self-Check: PASSED

---
*Phase: 20-shared-format-headers*
*Completed: 2026-03-16*
