---
gsd_state_version: 1.0
milestone: v1.3
milestone_name: Asset Pipeline
current_phase: Phase 23 -- Builder (Plan 1 of 3 complete)
current_plan: Plan 2 of 3
status: executing
last_updated: "2026-03-17T04:39:55Z"
last_activity: 2026-03-17 -- Completed 23-01 (builder infrastructure, cgltf/stb vendors, pack writer); 10min
progress:
  total_phases: 10
  completed_phases: 3
  total_plans: 10
  completed_plans: 8
---

# Session State

## Project Reference

See: .planning/PROJECT.md (updated 2026-03-16)

**Core value:** Simple, fast, predictable -- composable features wired through code, zero hidden magic.
**Current focus:** v1.3 Asset Pipeline -- Phase 23 Builder (1/3 plans done)

## Position

**Milestone:** v1.3 Asset Pipeline (Phases 20-28)
**Current phase:** Phase 23 -- Builder (1/3 plans done)
**Current Plan:** Plan 2 of 3
**Status:** Executing
**Last activity:** 2026-03-17 -- Completed 23-01 (builder infrastructure, cgltf/stb vendors, pack writer); 10min

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
