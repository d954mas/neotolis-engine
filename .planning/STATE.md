---
gsd_state_version: 1.0
milestone: v1.3
milestone_name: Asset Pipeline
current_phase: 28
current_plan: 5
status: executing
last_updated: "2026-03-20T09:49:42.686Z"
last_activity: 2026-03-20
progress:
  total_phases: 18
  completed_phases: 16
  total_plans: 40
  completed_plans: 39
---

# Session State

## Project Reference

See: .planning/PROJECT.md (updated 2026-03-16)

**Core value:** Simple, fast, predictable -- composable features wired through code, zero hidden magic.
**Current focus:** Phase 28 — demo-integration

## Position

**Milestone:** v1.3 Asset Pipeline (Phases 20-29)
**Current phase:** 28
**Current Plan:** 5
**Status:** Executing Phase 28
**Last activity:** 2026-03-20

## Decisions

Carried from v1.2 (relevant to v1.3):

- Unified GL backend: single file handles WebGL 2 and OpenGL 3.3 Core via #ifdef NT_PLATFORM_WEB
- Slot-based backend resource arrays (parallel to shared pool) -- texture pool will follow same pattern
- Consumer-provides-backend CMake pattern -- new modules (nt_resource, nt_entity) follow same
- Window-drives-input pattern: nt_window_poll() calls nt_input_poll() internally

v1.3 key decisions (from questioning):

- Two-level resource system: GFX handles direct + optional asset registry layer
- Material as runtime object (not pack asset) -- runtime flexibility, pack-loaded materials deferred
- Entity system without hierarchy (all roots), no deferred destruction
- .glb (glTF binary) for mesh source format via cgltf
- Builder is full native C binary, not WASM

Phase 20-01 decisions:

- pragma pack(push, 1) for cross-compiler struct layout guarantee (native + WASM)
- Explicit _pad byte in NtAssetEntry for clarity and future-proofing
- nt_crc32 as separate STATIC library, linked transitively through nt_shared INTERFACE

Phase 20-02 decisions:

- Mesh attribute mask bits derived from NT_ATTR_* enum (1u << NT_ATTR_POSITION etc.) for runtime alignment
- Explicit _pad fields in texture/shader headers for alignment clarity
- nt_core linked to test target for cross-verification of attribute mask against nt_gfx.h

Phase 21-01 decisions:

- No CPU copy of pixel data in texture descriptor (data=NULL after backend upload, label-only strdup)
- mag_filter silently clamped to LINEAR when mipmap variant specified (info log, not error)
- Context-loss texture recovery uses data=NULL and gen_mipmaps=false (GPU placeholder only)
- [Phase 24]: Option B for virtual pack entries: asset_count tracks total, unmount/unregister scan all assets[] by pack_index (non-contiguous)
- [Phase 24]: Virtual pack ownership: game creates GPU handles, game destroys -- registry only tracks references, unmount never calls destroy
- [Phase 29]: Builder public API returns raw uint64_t (not nt_hash64_t) to keep nt_builder.h free of nt_hash.h dependency
- [Phase 29]: NtAssetEntry reordered to 24 bytes: resource_id(8) + offset(4) + size(4) + format_version(2) + asset_type(1) + _pad(1) + _pad2(4)
- [Phase 29]: nt_resource public API uses nt_hash32_t for pack_id and nt_hash64_t for resource_id; internal structs use raw uint64_t/uint32_t
- [Phase 26]: Virtual packs for test resource setup instead of test_set_asset_state
- [Phase 26]: Pool size via nt_material_desc_t.max_materials at init, not compile-time define in test
- [Phase 27]: pragma pack(push,1) for nt_render_item_t to achieve exact 12-byte layout
- [Phase 27]: UBO Globals block auto-bound to slot 0 via glGetUniformBlockIndex/glUniformBlockBinding in pipeline creation
- [Phase 27]: Pipeline cache key: (layout_hash << 32 | resolved_vs) for lazy pipeline creation
- [Phase 27]: Instance layout uses attribute locations 4-8 (mat4 columns + color) with vertex divisor=1
- [Phase 27]: Real GFX shader handles in test materials (virtual packs register pool-allocated IDs)
- [Phase 28]: Sponza downloaded as separate glTF then converted to .glb via gltf-pipeline (no pre-made .glb available)
- [Phase 28]: Per-primitive material accessed via cgltf_data internal pointer cast in builder code
- [Phase 28]: Both sponza packs use force mode and share resource_ids for pack stacking
- [Phase 28]: Extract translation from manifest 4x4 matrix for TRS transform component (position from columns 12-14)

## Performance Metrics

| Phase | Plan | Duration | Tasks | Files |
|-------|------|----------|-------|-------|
| 20 | 01 | 6min | 2 | 7 |
| 20 | 02 | 6min | 2 | 6 |
| 21 | 01 | 9min | 2 | 6 |
| 22 | 01 | 5min | 2 | 7 |
| 22 | 02 | 11min | 2 | 5 |
| 22 | 03 | 9min | 2 | 11 |
| 23 | 01 | 10min | 2 | 13 |
| 23 | 02 | 13min | 2 | 5 |
| 23 | 03 | 14min | 2 | 12 |
| 24 | 01 | 8min | 2 | 7 |
| 24 | 02 | 11min | 2 | 4 |
| 25 | 01 | 7min | 2 | 16 |
| 25 | 02 | 10min | 2 | 5 |
| 25 | 03 | 15min | 2 | 6 |
| 29 | 01 | 14min | 2 | 10 |
| 29 | 03 | 12min | 2 | 11 |

Phase 22-01 decisions:

- NT_ASSERT_ALWAYS added to nt_assert.h (shared utility) for release-mode stale handle assertions
- Entity destroy increments generation and clears alive BEFORE calling storage callbacks (prevents re-add during destroy)

Phase 22-02 decisions:

- vec4 for quaternion rotation instead of versor to avoid cglm type compatibility warnings
- Custom ASSERT_FLOAT_NEAR macro in tests because Unity float assertions disabled globally (UNITY_EXCLUDE_FLOAT)

Phase 22-03 decisions:

- Float suffix uppercase (1.0F) per clang-tidy readability-uppercase-literal-suffix rule
- RenderStateComponent color as float[4] (not cglm vec4) to avoid nt_math dependency

Phase 23-01 decisions:

- _CRT_SECURE_NO_WARNINGS and _CRT_NONSTDC_NO_DEPRECATE for builder target (standard C I/O on Windows)
- Inline unsigned alignment arithmetic instead of NT_PACK_ALIGN_UP macro to avoid sign-conversion warnings
- nt_builder as STATIC library separate from builder executable for testability and reuse

Phase 23-02 decisions:

- NOLINTNEXTLINE for cognitive complexity on sequential pipeline functions
- Explicit NULL checks on gltf_name before strcmp for static analysis compliance
- calloc(max(n,1)) pattern to avoid zero-size allocation UB

Phase 23-03 decisions:

- Programmatic test fixtures (write_test_glb/png/shader in C) over binary files in repo
- -U_DLL for all builder-chain targets (cgltf, stb_image, nt_builder, test_builder, builder_demo)

Phase 24-01 decisions:

- Removed slot_alloc/slot_free/resource_make from nt_resource.c to avoid -Wunused-function; will re-add in 24-02
- nt_resource_desc_t placeholder with _reserved field; init accepts any desc
- Programmatic test blob construction (build_test_pack) consistent with Phase 23 pattern

Phase 24-02 decisions:

- NOLINTNEXTLINE on nt_resource_step() for cognitive complexity (sequential resolve pipeline)
- NT_RESOURCE_TEST_ACCESS on both nt_resource library and test_resource target for test-only functions
- Equal priority tiebreaker: higher pack_index (later mount) wins
- build_pack_with_rid helper for targeted resource_id blob construction in stacking tests

| Phase 24 P03 | 4min | 2 tasks | 2 files |

Phase 25-02 decisions:

- Buffer metadata struct (nt_gfx_buffer_meta_t, 8 bytes) replaces full buffer_desc copies -- type/usage/size only
- Context loss wipes backend handles and mesh table but keeps pool slots -- game must re-create from source
- Shape renderer restore_gpu uses save-shutdown-init-restore pattern for simplicity

Phase 25-03 decisions:

- Blob ownership split: I/O-loaded blobs (io_type != NT_IO_NONE) freed by nt_resource_unmount; parse_pack blobs remain caller-owned
- _CRT_SECURE_NO_WARNINGS on nt_resource target for strncpy (same pattern as builder/fs)
- WORKING_DIRECTORY set to CMAKE_SOURCE_DIR for test_resource (file I/O tests need project root)

Phase 29-01 decisions:

- FNV-1a confirmed as winner: fastest 32-bit on short strings, smallest code, WASM-friendly
- _CRT_SECURE_NO_WARNINGS on nt_hash for strncpy in label table (same pattern as builder/fs)
- NT_HASH_LABELS compile definition on both nt_hash and test_hash targets (same as NT_RESOURCE_TEST_ACCESS)

Phase 29-03 decisions:

- Builder public API returns raw uint64_t (not nt_hash64_t) to keep nt_builder.h free of nt_hash.h dependency
- nt_builder_dump.c displays 64-bit resource_id as 0x%016llX with wider column headers

Phase 25-01 decisions:

- Backend slot accessor pattern (nt_http_get_slot/nt_fs_get_slot via extern) for backend files to update slot state
- -U_DLL for nt_fs native target and test_fs (CRT file I/O on Windows, same pattern as builder)
- (void)fclose() casts for cert-err33-c compliance in error paths

| Phase 29 P03 | 12min | 2 tasks | 11 files |
| Phase 29 P02 | 18min | 2 tasks | 8 files |
| Phase 26 P01 | 9min | 2 tasks | 6 files |
| Phase 26 P02 | 14min | 2 tasks | 6 files |
| Phase 27 P01 | 11min | 2 tasks | 19 files |
| Phase 27 P02 | 9min | 1 task | 5 files |
| Phase 28 P01 | 30min | 2 tasks | 15 files |
| Phase 28 P02 | 10min | 2 tasks | 7 files |

Phase 28-02 decisions:

- Global block registry as file-scope static array in nt_gfx.c with getter function for backend access
- NT_ASSET_BLOB auto-marked READY during parse_pack (no activator needed, no GPU resource)
- nt_resource_get_blob scans asset metadata to find blob data pointer within pack blob

Phase 28-01 decisions:

- MikkTSpace vendored as 2-file C library in deps/mikktspace/ with -U_DLL for static CRT
- NT_ASSET_BLOB = 4 added to nt_asset_type_t; NtBlobAssetHeader is 8 bytes (magic + version + pad)
- Scene mesh import uses same type conversion and interleaving pattern as nt_builder_mesh.c
- Tangent mode is per-mesh configurable via nt_mesh_opts_t, not global
- nt_glb_texture_t maps to cgltf images (not cgltf textures) for direct buffer_view data access

| Phase 28 P03 | 14min | 2 tasks | 12 files |
| Phase 28 P04 | 10min | 1 tasks | 3 files |

## Accumulated Context

### Pending Todos

- Stats overlay module (FPS, draw calls) -- requires text rendering, use nt_log until then

### Roadmap Evolution

- Phase 29 added: nt_hash module — unified CRC32 + string hash, resource label hashing

### Quick Tasks Completed

| # | Description | Date | Commit | Directory |
|---|-------------|------|--------|-----------|
| 260316-jc3 | Optimize CI pipeline time to reduce costs | 2026-03-16 | 93a9a29 | [260316-jc3-optimize-ci-pipeline-time-to-reduce-cost](./quick/260316-jc3-optimize-ci-pipeline-time-to-reduce-cost/) |

## Session Log

- 2026-03-16: v1.3 milestone started, requirements defined (58 across 14 categories)
- 2026-03-16: Research completed (stack, features, architecture, pitfalls) -- HIGH confidence
- 2026-03-16: Roadmap created -- 9 phases (20-28), 58/58 requirements mapped
- 2026-03-16: Completed 20-01-PLAN.md -- NEOPAK pack format headers, CRC32, 14 unit tests (6min)
- 2026-03-16: Completed 20-02-PLAN.md -- asset format headers (mesh/texture/shader), 26 total tests (6min)
- 2026-03-16: Phase 20 complete -- all shared format headers defined, ready for Phase 21
- 2026-03-16: Completed 21-01-PLAN.md -- texture pool infrastructure, 14 new tests (32 total), 9min
- 2026-03-16: Completed 22-01-PLAN.md -- entity pool core, generational handles, storage registration, 15 tests, 5min
- 2026-03-16: Completed 22-03-PLAN.md -- render components (mesh, material, render_state), 16 unit tests, 9min
- 2026-03-16: Completed 22-02-PLAN.md -- transform component, sparse+dense storage, TRS matrix, 15 tests, 11min
- 2026-03-16: Phase 22 complete -- entity system with 4 component types, 46 unit tests total
- 2026-03-17: Completed 23-01-PLAN.md -- builder infrastructure, cgltf+stb vendors, pack writer, FNV-1a hash, dump utility, 10min
- 2026-03-17: Completed 23-02-PLAN.md -- mesh/texture/shader importers (cgltf, stb_image, GLSL comment strip), 13min
- 2026-03-17: Completed 23-03-PLAN.md -- glob patterns, 19 unit tests, demo builder, 14min
- 2026-03-17: Phase 23 complete -- builder library feature-complete with all BUILD-01 through BUILD-07
- 2026-03-17: Completed 24-01-PLAN.md -- nt_resource module, NEOPAK parser, FNV-1a hash, 16 unit tests, 8min
- 2026-03-17: Completed 24-02-PLAN.md -- pack stacking, resource access, step() resolve, 16 new tests (32 total), 11min
- 2026-03-17: Completed 24-03-PLAN.md -- virtual packs, placeholder fallback, 13 new tests (45 total), 4min
- 2026-03-17: Phase 24 complete -- asset registry with virtual packs, placeholder, 45 unit tests total
- 2026-03-18: Completed 25-01-PLAN.md -- nt_http + nt_fs I/O modules, generational handles, 22 unit tests, 7min
- 2026-03-18: Completed 25-02-PLAN.md -- GFX activators (texture/mesh/shader), mesh side table, CPU desc removal, restore_gpu, 10min
- 2026-03-18: Completed 25-03-PLAN.md -- pack loading state machine, activation loop, retry, blob eviction, invalidation, 22 new tests (67 total), 15min
- 2026-03-18: Phase 25 complete -- asset loading with I/O integration, activator system, retry, blob management, 67 unit tests
- 2026-03-19: Completed 29-01-PLAN.md -- nt_hash module, FNV-1a 32/64, typed wrappers, label system, 16 unit tests, benchmark (FNV-1a winner), 14min
- 2026-03-19: Completed 29-03-PLAN.md -- builder hash migration to nt_hash, spec updated with nt_hash module and 64-bit resource_id, 12min
- 2026-03-19: Completed 29-02-PLAN.md -- resource migration to nt_hash types, NtAssetEntry widened to 24B, ~100 test sites migrated, 18min
- 2026-03-19: Phase 29 complete -- nt_hash module unified hash, all resource/builder modules migrated, 3/3 plans done
- 2026-03-19: Completed 26-01-PLAN.md -- drawable comp rename, mesh info extension with streams/stride/layout_hash, 9min
- 2026-03-19: Completed 26-02-PLAN.md -- nt_material module, pooled handles, step resolution, 22 unit tests, 14min
- 2026-03-19: Phase 26 complete -- material system with descriptor creation, resource resolution, change detection, 22 unit tests
- 2026-03-19: Completed 27-01-PLAN.md -- render defs (globals 256B, instance 80B, item 12B), sort keys, visibility, UBO support, drawable tag migration, 26 new tests, 11min
- 2026-03-19: Completed 27-02-PLAN.md -- mesh renderer with pipeline cache, run detection, instanced drawing, 9 unit tests, 9min
- 2026-03-19: Phase 27 complete -- mesh rendering pipeline with render defs, sort keys, pipeline cache, instanced draw, 2/2 plans done
- 2026-03-20: Completed 28-01-PLAN.md -- builder extensions: nt_glb_scene API, NT_ASSET_BLOB, MikkTSpace tangent, texture-from-memory, 3 new tests, 30min
- 2026-03-20: Completed 28-02-PLAN.md -- global blocks API replacing hardcoded Globals, nt_resource_get_blob, 7 new tests, 10min
- 2026-03-20: Completed 28-03-PLAN.md -- Sponza asset (52MB glb), 3 shader permutations, build_packs.c producing two progressive packs (179 assets each), 14min
- 2026-03-20: Completed 28-04-PLAN.md -- Sponza runtime demo (721 lines), FPS camera, Lighting UBO, scene manifest loading, 3 shader permutations, progressive pack stacking, 10min
