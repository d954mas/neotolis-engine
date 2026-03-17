---
phase: 23-builder
plan: 02
subsystem: build-pipeline
tags: [cgltf, stb_image, glTF, mesh, texture, shader, float16, NEOPAK]

# Dependency graph
requires:
  - phase: 20-shared-formats
    provides: "NtMeshAssetHeader, NtTextureAssetHeader, NtShaderCodeHeader, NtStreamDesc, nt_stream_type_t"
  - phase: 23-builder (plan 01)
    provides: "NtBuilderContext, nt_builder_append_data, nt_builder_register_asset, nt_builder_fnv1a, nt_builder_float32_to_float16, cgltf/stb vendored deps"
provides:
  - "nt_builder_add_mesh / nt_builder_add_mesh_with_id -- glTF mesh import with stream layout and type conversion"
  - "nt_builder_add_texture / nt_builder_add_texture_with_id -- PNG texture import forced RGBA8"
  - "nt_builder_add_shader / nt_builder_add_shader_with_id -- GLSL shader import with comment strip and validation"
affects: [23-builder-plan-03, 24-runtime-loader, 25-resource-manager]

# Tech tracking
tech-stack:
  added: []
  patterns: ["sequential pipeline with goto cleanup", "state machine comment stripping", "explicit stream layout for mesh attribute extraction"]

key-files:
  created:
    - tools/builder/nt_builder_mesh.c
    - tools/builder/nt_builder_texture.c
    - tools/builder/nt_builder_shader.c
  modified:
    - tools/builder/nt_builder.c
    - tools/builder/CMakeLists.txt

key-decisions:
  - "NOLINTNEXTLINE for cognitive complexity on pipeline functions (sequential steps, not true complexity)"
  - "Explicit null checks on gltf_name before strcmp to satisfy static analysis"
  - "calloc(max(n,1)) pattern to avoid zero-size allocation UB"

patterns-established:
  - "Importer pattern: validate inputs -> load source -> convert to runtime format -> append to pack -> register asset"
  - "Path normalization before hashing for cross-platform determinism"

requirements-completed: [BUILD-01, BUILD-02, BUILD-03, BUILD-07]

# Metrics
duration: 13min
completed: 2026-03-17
---

# Phase 23 Plan 02: Asset Importers Summary

**Three asset importers (mesh/texture/shader) converting .glb, .png, .glsl source files to NEOPAK binary format via cgltf and stb_image**

## Performance

- **Duration:** 13 min
- **Started:** 2026-03-17T04:43:44Z
- **Completed:** 2026-03-17T04:57:13Z
- **Tasks:** 2
- **Files modified:** 5

## Accomplishments
- Mesh importer reads .glb via cgltf with explicit stream layout, type conversion (float32/float16/int8/uint8/int16/uint16), auto-selected index type, POSITION validation
- Texture importer reads .png via stb_image forced RGBA8, validates dimensions against NT_BUILD_MAX_TEXTURE_SIZE
- Shader importer reads .glsl, strips C-style comments (line + block), collapses whitespace, validates #version and void main(), stores null-terminated source
- All three importers use nt_builder_append_data + nt_builder_register_asset to produce correct NEOPAK pack data

## Task Commits

Each task was committed atomically:

1. **Task 1: Mesh importer -- cgltf extraction with stream layout and type conversion** - `59e2ef4` (feat)
2. **Task 2: Texture and shader importers** - `50ecfa3` (feat)

## Files Created/Modified
- `tools/builder/nt_builder_mesh.c` - glTF mesh import: cgltf parse, attribute extraction by stream layout, type conversion, interleaved vertex data, index extraction
- `tools/builder/nt_builder_texture.c` - PNG texture import: stb_image load forced RGBA8, dimension validation, NtTextureAssetHeader + raw pixel data
- `tools/builder/nt_builder_shader.c` - GLSL shader import: comment stripping state machine, whitespace collapse, #version/void main() checks, NtShaderCodeHeader + null-terminated source
- `tools/builder/nt_builder.c` - Removed individual add_* stubs (now in separate files), kept batch stubs for Plan 03
- `tools/builder/CMakeLists.txt` - Added nt_builder_mesh.c, nt_builder_texture.c, nt_builder_shader.c to STATIC library

## Decisions Made
- Used NOLINTNEXTLINE for cognitive complexity on pipeline functions -- these are sequential multi-step functions, not truly complex logic
- Added explicit NULL checks on gltf_name before strcmp calls to satisfy clang-analyzer null dereference warnings
- Used calloc(max(n,1)) pattern to avoid zero-size allocation (undefined behavior per C standard)
- Path normalization (backslash to forward slash) applied before resource_id hashing for cross-platform determinism

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed clang-tidy narrowing conversion in shader comment stripper**
- **Found during:** Task 2 (Shader importer)
- **Issue:** Ternary `(i+1 < src_len) ? src[i+1] : '\0'` triggers bugprone-narrowing-conversions because '\0' is int
- **Fix:** Changed to `char next = 0; if (...) { next = src[i+1]; }` to avoid ternary promotion
- **Files modified:** tools/builder/nt_builder_shader.c
- **Verification:** clang-tidy passes clean
- **Committed in:** 50ecfa3

**2. [Rule 1 - Bug] Fixed math parentheses for clang-tidy readability**
- **Found during:** Task 1 (Mesh importer)
- **Issue:** Expressions like `c * 255.0F + 0.5F` trigger readability-math-missing-parentheses
- **Fix:** Added explicit parentheses: `(c * 255.0F) + 0.5F`
- **Files modified:** tools/builder/nt_builder_mesh.c
- **Verification:** clang-tidy passes clean
- **Committed in:** 59e2ef4

---

**Total deviations:** 2 auto-fixed (2 bug fixes for static analysis compliance)
**Impact on plan:** Both fixes necessary for clean static analysis. No scope creep.

## Issues Encountered
- Cognitive complexity exceeded threshold (25) for mesh pipeline functions -- resolved by splitting into helper functions (nt_validate_stream_layout, nt_parse_gltf, nt_extract_vertex_streams, nt_interleave_vertices) plus NOLINTNEXTLINE for the remaining orchestrator function

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- All three importers compile and link into nt_builder static library
- Ready for Plan 03: glob-based batch asset addition and builder tests
- Pre-existing clang-tidy issue in nt_builder.c:155 (readability-math-missing-parentheses in finish_pack) -- not introduced by this plan

## Self-Check: PASSED

All files exist, all commits verified, all acceptance criteria met (22/22 checks passed).

---
*Phase: 23-builder*
*Completed: 2026-03-17*
