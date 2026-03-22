---
gsd_state_version: 1.0
milestone: v1.4
milestone_name: Rendering & Textures
current_phase: 31
current_plan: 1
status: executing
last_updated: "2026-03-22T13:08:09.538Z"
last_activity: 2026-03-22
progress:
  total_phases: 6
  completed_phases: 2
  total_plans: 5
  completed_plans: 5
  percent: 17
---

# Session State

## Project Reference

See: .planning/PROJECT.md (updated 2026-03-21)

**Core value:** Simple, fast, predictable -- composable features wired through code, zero hidden magic.
**Current focus:** Phase 31 — material-shape-fixes

## Position

**Milestone:** v1.4 Rendering & Textures
**Current phase:** 31
**Current Plan:** 1
**Status:** Executing Phase 31
**Last activity:** 2026-03-22

Progress: [▓▓░░░░░░░░] 17%

## Decisions

v1.4 key decisions (from discussion):

- Logging: variadic functions + domain macros. Domain auto-injected by CMake via NT_LOG_DOMAIN_DEFAULT. Per-file override via #define. #error if neither defined. WARN level added.
- Basis Universal: encoder in builder (C++, native only), transcoder in runtime (C++ with extern "C", WASM). Offline mip chain generation. GPU format detection via EM_JS.
- Material as pack asset permanently out of scope.
- nt_gfx already supports uint32 indices -- shape renderer just needs to use it.

Carried from v1.3 (relevant to v1.4):

- Two-level resource system: GFX handles direct + optional asset registry layer
- Pipeline cache key: (layout_hash << 32 | resolved_vs) for lazy pipeline creation
- Instance layout uses attribute locations 4-8 (mat4 columns + color) with vertex divisor=1
- pragma pack(push,1) for format structs (native + WASM layout guarantee)
- [Phase 30]: Removed unused stdio.h from nt_resource.c after snprintf elimination; em-dash replaced with ASCII double-dash in shape_renderer
- [Phase 31]: Unified vec4 uniform for all material params; no version bump on param change
- [Phase 31]: Compile-time index type (uint16/uint32) via nt_shape_index_t typedef; instanced templates keep uint16
- [Phase 31]: Compile-time index type (uint16/uint32) via nt_shape_index_t typedef; instanced templates keep uint16

## Performance Metrics

| Phase | Plan | Duration | Tasks | Files |
|-------|------|----------|-------|-------|
| 30 | 01 | 13min | 2 | 28 |
| 30 | 03 | 12min | 2 | 11 |

Phase 30-01 decisions:

- NT_PRINTF_ATTR omitted from plain functions during migration (existing callers pass non-literal strings). Restored in Plan 02/03.
- Pragma-based -Wformat-nonliteral suppression in nt_log.c TU; format safety at API boundary via _impl attributes.

| Phase 30 P02 | 14min | 2 tasks | 9 files |
| Phase 31 P01 | 3min | 2 tasks | 5 files |
| Phase 31 P02 | 4min | 2 tasks | 3 files |

## Accumulated Context

### Pending Todos

- Stats overlay module (FPS, draw calls) -- requires text rendering, use nt_log until then

### Blockers/Concerns

None yet.

## Session Log

- 2026-03-21: v1.4 milestone started, requirements defined (35 across 6 categories)
- 2026-03-21: Research completed -- HIGH confidence, 6 recommended phases
- 2026-03-21: Roadmap created -- 6 phases (30-35), 35/35 requirements mapped
- 2026-03-21: Completed 30-01-PLAN.md -- logging API + domain injection + tests (13min, 2 tasks, 28 files)
- 2026-03-21: Completed 30-03-PLAN.md -- builder printf/fprintf migration to nt_log domain macros (12min, 2 tasks, 11 files)
