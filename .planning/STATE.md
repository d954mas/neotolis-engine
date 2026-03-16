---
gsd_state_version: 1.0
milestone: v1.3
milestone_name: Asset Pipeline
current_phase: 21
current_plan: Plan 1 of Phase 21
status: phase-complete
last_updated: "2026-03-16"
progress:
  total_phases: 9
  completed_phases: 1
  total_plans: 2
  completed_plans: 2
---

# Session State

## Project Reference

See: .planning/PROJECT.md (updated 2026-03-16)

**Core value:** Simple, fast, predictable -- composable features wired through code, zero hidden magic.
**Current focus:** v1.3 Asset Pipeline -- Phase 20 complete, Phase 21 next

## Position

**Milestone:** v1.3 Asset Pipeline (Phases 20-28)
**Current phase:** Phase 20 -- Shared Format Headers (complete)
**Current Plan:** Phase 20 complete (2/2 plans done), ready for Phase 21
**Status:** Phase 20 complete
**Last activity:** 2026-03-16 -- Completed 20-02 (asset format headers + 26 total unit tests)

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

## Performance Metrics

| Phase | Plan | Duration | Tasks | Files |
|-------|------|----------|-------|-------|
| 20 | 01 | 6min | 2 | 7 |
| 20 | 02 | 6min | 2 | 6 |

## Accumulated Context

### Pending Todos

- Stats overlay module (FPS, draw calls) -- requires text rendering, use nt_log until then

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
