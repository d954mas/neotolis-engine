---
phase: 28-demo-integration
plan: 03
subsystem: builder, assets
tags: [sponza, gltf, glb, shader-permutations, pack-stacking, scene-manifest, git-lfs]

# Dependency graph
requires:
  - phase: 28-01
    provides: "nt_glb_scene API, NT_ASSET_BLOB, MikkTSpace tangent, texture-from-memory"
  - phase: 28-02
    provides: "nt_gfx_register_global_block API, nt_resource_get_blob"
provides:
  - "Sponza.glb source asset (52MB, LFS-tracked)"
  - "Three shader permutations: full (normal+specular), diffuse-only, alpha-test"
  - "build_packs.c producing sponza_base.ntpack and sponza_full.ntpack with shared resource_ids"
  - "Scene manifest blob with per-primitive material classification"
  - "textured_quad adapted to registered global blocks API"
affects: [28-04, 28-05, sponza-demo]

# Tech tracking
tech-stack:
  added: [gltf-pipeline]
  patterns:
    - "Per-primitive material classification via cgltf_data internal access"
    - "Two-quality pack generation sharing resource_ids for pack stacking"
    - "Scene manifest blob with hashed resource_ids for runtime lookup"

key-files:
  created:
    - assets/sponza/Sponza.glb
    - assets/shaders/sponza_full.vert
    - assets/shaders/sponza_full.frag
    - assets/shaders/sponza_diffuse.vert
    - assets/shaders/sponza_diffuse.frag
    - assets/shaders/sponza_alpha.vert
    - assets/shaders/sponza_alpha.frag
    - examples/sponza/build_packs.c
    - examples/sponza/CMakeLists.txt
  modified:
    - .gitattributes
    - examples/textured_quad/main.c
    - CMakeLists.txt

key-decisions:
  - "Sponza downloaded as separate glTF then converted to .glb via gltf-pipeline (KhronosGroup repo has no pre-made .glb)"
  - "Per-primitive material accessed via cgltf_data internal pointer cast (acceptable for builder-side tool)"
  - "Alpha shader uses hardcoded 0.5 threshold with u_alpha_cutoff uniform declared for future per-material override"
  - "Both packs use force mode to avoid duplicate errors when adding same texture/shader resource_ids"

patterns-established:
  - "Scene manifest blob format: ManifestHeader (8B) + ManifestNode[] (136B each) with hashed resource_ids"
  - "Shader permutation naming: sponza_{full,diffuse,alpha}.{vert,frag}"
  - "Material classification heuristic: normal_map -> full, alpha_mode MASK/BLEND -> alpha, else -> diffuse"

requirements-completed: [DEMO-01, DEMO-02]

# Metrics
duration: 14min
completed: 2026-03-20
---

# Phase 28 Plan 03: Sponza Assets and Pack Builder Summary

**Sponza.glb with 3 shader permutations and build_packs.c producing two progressive NEOPAK packs (179 assets each) with scene manifest blob for pack stacking**

## Performance

- **Duration:** 14 min
- **Started:** 2026-03-20T09:19:13Z
- **Completed:** 2026-03-20T09:33:13Z
- **Tasks:** 2
- **Files modified:** 12

## Accomplishments
- Downloaded and LFS-tracked Sponza.glb (52MB, 103 primitives, 25 materials, 69 textures)
- Created 3 shader permutations (6 files): full (Blinn-Phong + normal mapping + specular), diffuse-only, alpha-test with discard
- Implemented build_packs.c (402 lines) that parses Sponza.glb, classifies materials per-primitive, generates scene manifest blob, and produces two quality-level packs
- Both packs (276MB base, 282MB full) share identical resource_ids for runtime pack stacking
- Updated textured_quad to register Globals UBO block (required after Plan 02 API change)

## Task Commits

Each task was committed atomically:

1. **Task 1: Download Sponza, setup Git LFS, create shaders, update textured_quad** - `0f0e1fd` (feat)
2. **Task 2: Implement build_packs.c with scene manifest, CMake wiring** - `fcdc2a1` (feat)

## Files Created/Modified
- `assets/sponza/Sponza.glb` - Crytek Sponza glTF binary (52MB, LFS-tracked)
- `assets/shaders/sponza_full.vert` - Blinn-Phong vertex shader with normal mapping and instancing
- `assets/shaders/sponza_full.frag` - Blinn-Phong fragment shader with normal+specular maps and Lighting UBO
- `assets/shaders/sponza_diffuse.vert` - Diffuse-only vertex shader (no tangent attribute)
- `assets/shaders/sponza_diffuse.frag` - Diffuse-only fragment shader with Lighting UBO
- `assets/shaders/sponza_alpha.vert` - Alpha-test vertex shader (same layout as diffuse)
- `assets/shaders/sponza_alpha.frag` - Alpha-test fragment shader with discard at 0.5 threshold
- `examples/sponza/build_packs.c` - Pack builder: parses glb, adds 103 meshes + 69 textures + 6 shaders + manifest blob
- `examples/sponza/CMakeLists.txt` - Build target for sponza pack builder (native only)
- `.gitattributes` - Added LFS tracking for sponza assets
- `examples/textured_quad/main.c` - Added nt_gfx_register_global_block("Globals", 0)
- `CMakeLists.txt` - Added examples/sponza subdirectory

## Decisions Made
- Sponza model not available as pre-made .glb from KhronosGroup; downloaded separate glTF + textures and converted using gltf-pipeline (npx). The resulting 52MB .glb has all textures embedded.
- Per-primitive material info accessed by casting scene->_internal to cgltf_data* since the nt_glb_scene API only exposes first primitive's material per mesh. Acceptable for builder-side tool code.
- Alpha shader uses hardcoded 0.5 threshold (simplest). The u_alpha_cutoff uniform is declared but unused; per-material cutoff stored in manifest (alpha_cutoff_x100) for future use.
- Force mode enabled on both builder contexts to handle shared shader resource_ids across both packs without duplicate errors.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Converted glTF to glb via gltf-pipeline**
- **Found during:** Task 1 (asset download)
- **Issue:** Plan assumed Sponza.glb available at KhronosGroup raw URL; actual repo only has separate .gltf + textures
- **Fix:** Downloaded all 71 files (gltf + bin + 69 images), converted to single .glb using npx gltf-pipeline
- **Files modified:** assets/sponza/Sponza.glb (created)
- **Verification:** Valid glTF binary header, 52.6MB, builder parses successfully
- **Committed in:** 0f0e1fd (Task 1 commit)

**2. [Rule 1 - Bug] Added null check for mat parameter in classify_material**
- **Found during:** Task 2 (clang-tidy)
- **Issue:** clang-analyzer detected potential null dereference of mat parameter in classify_material
- **Fix:** Added `if (mat == NULL) return SHADER_DIFFUSE;` guard at function entry
- **Files modified:** examples/sponza/build_packs.c
- **Verification:** clang-tidy passes without NullDereference warning
- **Committed in:** fcdc2a1 (Task 2 commit)

**3. [Rule 1 - Bug] Added missing stdlib.h include**
- **Found during:** Task 2 (build)
- **Issue:** calloc/free used without including stdlib.h, caught by -Wimplicit-function-declaration
- **Fix:** Added `#include <stdlib.h>`
- **Files modified:** examples/sponza/build_packs.c
- **Verification:** Build succeeds without warnings
- **Committed in:** fcdc2a1 (Task 2 commit)

---

**Total deviations:** 3 auto-fixed (1 blocking, 2 bugs)
**Impact on plan:** All auto-fixes necessary for correctness. No scope creep.

## Issues Encountered
None beyond the deviations documented above.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Sponza source asset and packs ready for runtime rendering (Plan 04)
- Scene manifest blob provides per-primitive material/shader/texture mapping
- Pack stacking works: load sponza_base first, then sponza_full upgrades quality
- Shader permutations support full Blinn-Phong, diffuse-only, and alpha-test materials

## Self-Check: PASSED

All 12 files verified present. Both commit hashes (0f0e1fd, fcdc2a1) verified in git log.

---
*Phase: 28-demo-integration*
*Completed: 2026-03-20*
