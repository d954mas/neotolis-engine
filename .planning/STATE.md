---
gsd_state_version: 1.0
milestone: v1.4
milestone_name: Rendering & Textures
current_phase: 30
current_plan: 3
status: phase-complete
last_updated: "2026-03-21T18:45:14Z"
last_activity: 2026-03-21
progress:
  total_phases: 6
  completed_phases: 1
  total_plans: 3
  completed_plans: 3
  percent: 17
---

# Session State

## Project Reference

See: .planning/PROJECT.md (updated 2026-03-21)

**Core value:** Simple, fast, predictable -- composable features wired through code, zero hidden magic.
**Current focus:** Phase 30 — logging-infrastructure

## Position

**Milestone:** v1.4 Rendering & Textures
**Current phase:** 30
**Current Plan:** 3
**Status:** Phase 30 complete
**Last activity:** 2026-03-21

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

## Performance Metrics

| Phase | Plan | Duration | Tasks | Files |
|-------|------|----------|-------|-------|
| 30 | 01 | 13min | 2 | 28 |
| 30 | 03 | 12min | 2 | 11 |

Phase 30-01 decisions:

- NT_PRINTF_ATTR omitted from plain functions during migration (existing callers pass non-literal strings). Restored in Plan 02/03.
- Pragma-based -Wformat-nonliteral suppression in nt_log.c TU; format safety at API boundary via _impl attributes.

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
