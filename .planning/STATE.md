---
gsd_state_version: 1.0
milestone: v1.1
milestone_name: Modular Build
status: in-progress
stopped_at: Completed 11-02-PLAN.md
last_updated: "2026-03-10T15:51:00Z"
last_activity: 2026-03-10 -- Completed 11-02 Example showcase system
progress:
  total_phases: 10
  completed_phases: 5
  total_plans: 19
  completed_plans: 14
  percent: 74
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-03-09)

**Core value:** Simple, fast, predictable engine runtime -- composable features wired by game code with zero hidden magic
**Current focus:** Phase 11 Promo Website (2 of 3 plans done)

## Current Position

Phase: 11 of 11 (Create a promo website for Neotolis Engine) -- ninth phase of v1.1 milestone
Plan: 2 of 3
Status: Plan 02 complete, continuing to Plan 03
Last activity: 2026-03-10 -- Completed 11-02 Example showcase system

Progress: [███████░░░] 74%

## Performance Metrics

**Velocity:**
- Total plans completed: 16 (4 from v1.0 + 12 from v1.1)
- Average duration: --
- Total execution time: --

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 1. Project Scaffold | 2 | -- | -- |
| 2. Build Hardening | 2 | -- | -- |
| 3. Build Infrastructure | 2 | 5min | 2.5min |
| 4. Module Split | 1 | 2min | 2min |
| 5. Swappable Backends | 2/2 | 4min | 2min |

*Updated after each plan completion*
| Phase 07 P01 | 2min | 2 tasks | 5 files |
| Phase 08 P01 | 2min | 2 tasks | 2 files |
| Phase 08 P02 | 2min | 2 tasks | 2 files |
| Phase 09 P01 | 2min | 2 tasks | 4 files |
| Phase 09 P02 | 2min | 2 tasks | 1 files |
| Phase 10 P01 | 2min | 3 tasks | 4 files |
| Phase 10 P02 | 1min | 1 tasks | 2 files |
| Phase 10 P03 | 3min | 2 tasks | 3 files |
| Phase 11 P01 | 6min | 2 tasks | 13 files |
| Phase 11 P02 | 9min | 2 tasks | 8 files |

## Accumulated Context

### Decisions

Decisions from v1.0 archived. See `.planning/milestones/v1.0-ROADMAP.md`.

Key context for v1.1:
- C17 (not C23) due to Emscripten compatibility
- cglm v0.9.6 and Unity v2.6.1 vendored in deps/
- CI already configured with 4 presets (wasm-debug, wasm-release, native-debug, native-release)

Phase 3 decisions:
- nt_add_module() uses positional args only -- no cmake_parse_arguments
- Platform detection via nt_platform.h header, not CMake target_compile_definitions
- NT_PLATFORM_NATIVE as catch-all for non-Emscripten, non-Windows (covers CI on Linux)
- NT_ENABLE_ASSERTS included in nt_platform.h gated on NT_DEBUG
- nt_engine kept as INTERFACE shim forwarding to nt_core (migration in Phase 4)
- Used #ifdef (not #if) for platform macro checks to avoid -Wundef warnings
- CI artifact checks updated from libnt_engine.a to libnt_core.a (nt_engine is INTERFACE, produces no .a)

Phase 4 decisions:
- Removed nt_engine INTERFACE shim with no deprecation period per user decision
- hello links cglm_headers for intentional future-proofing (zero binary impact as INTERFACE)
- Removed redundant target_include_directories from all 3 test targets (nt_core provides via PUBLIC)

Phase 5 decisions:
- ALIAS uses string(REPLACE) to strip nt_ prefix -- simple and predictable
- Both implementations are fully independent STATIC libs sharing only the header
- No INTERFACE CMake target for API -- contract enforced at source level via shared .h

Phase 7 decisions:
- Un-ignore .vscode/settings.json and launch.json for zero-setup onboarding
- Use cppvsdbg (not cppdbg) since Clang on Windows emits CodeView/PDB debug info
- WASM configure uses shell task with emcmake wrapper (cmake task type cannot handle Emscripten)
- files.associations *.h -> c to prevent VSCode treating headers as C++ in pure C17 project

Phase 8 decisions:
- Plain %d format (no locale-dependent comma formatting) for Git Bash/MSYS cross-platform compatibility
- JSON built manually with printf -- no jq dependency for local script usage
- Benchmark injects flags by appending new if(EMSCRIPTEN) block rather than editing existing block
- tr -d space after wc -c to handle Git Bash whitespace in output
- Independent WASM build in size-track job (no needs: on wasm-build) for self-containment and reduced wall-clock time
- Category axis (commit hashes) instead of time axis to avoid Chart.js date adapter dependency
- Per-file chart lines hidden by default, toggleable via Chart.js legend clicks
- git worktree approach in init script to avoid disturbing working tree

Phase 9 decisions:
- NT_WASM_ANALYSIS checked before Release/Debug in CMakeLists.txt preset routing
- --profiling-funcs added inside existing if(EMSCRIPTEN) block, not as separate block
- Engine lib dir resolved from build/_cmake/$PRESET/engine for .a files
- size-report.sh outputs to stdout only -- caller handles file writing for composability
- wabt installed via apt-get independently in each CI job (no shared setup step)
- Size report comment uses "## WASM Size Report" marker distinct from Phase 8's "## WASM Build Size"
- Unified diff view in collapsible details block plus inline section/function delta tables

Phase 10 decisions:
- NT_SHELL_KEYFRAMES variable injects literal @keyframes -- CMake configure_file(@ONLY) does not transform @@ to @
- FULLSCREEN_BUTTON block includes inline <style> override for canvas height calc(100% - 32px)
- Custom SHELL_FILE also routed through configure_file(@ONLY) for @VAR@ resolution
- cmake_parse_arguments used for nt_configure_shell() (optional named params, different from nt_add_module positional style)
- Module object contains only canvas property -- no print/printErr overrides (Emscripten defaults)
- EM_JS instead of EM_ASM for nt_web_loading_complete() -- avoids C17 variadic macro warning with -Wpedantic -Werror
- Header-only nt_web.h with static inline no-op on native -- no .c file or new CMake module needed
- Context menu blocked on body element instead of canvas for full page coverage
- Crash overlay background #1a1a1a (dark gray) distinguishable from #000 page background

Phase 11 decisions:
- Manual Astro scaffold instead of create-astro CLI due to Node v20.15 compatibility
- Tailwind v4 @theme block for dark theme tokens (bg-primary, bg-secondary, text-primary, text-secondary, accent)
- Features rendered as bold-title + em-dash + description (simple list, no cards/icons per user decision)
- Overview section titled Philosophy with 5 core principles from engine spec
- GitHub Star button via ghbtns.com iframe in both Header and Hero section
- Glob loader with base: '../examples' resolves promo data from outside Astro project root
- Size data dual-source: local gh-pages-data/ in CI, fetch from GitHub Pages URL locally
- Native HTML details/summary for collapsible size breakdown (zero JS, accessible)
- ExampleLayout is standalone HTML document (not BaseLayout) for full-viewport WASM demos

### Pending Todos

None.

### Roadmap Evolution

- Phase 7 added: VSCode Setup — recommended extensions, build/run/debug tasks, launch configurations
- Phase 8 added: Track output game size
- Phase 9 added from issue #7: Add a diff-friendly WASM binary size report
- Phase 10 added from issue #9: Flexible Minimal HTML Shell for WASM Builds
- Phase 11 added from issue #10: Create a promo website for Neotolis Engine with feature overview and dedicated example pages

### Blockers/Concerns

None.

### Quick Tasks Completed

| # | Description | Date | Commit | Directory |
|---|-------------|------|--------|-----------|
| 1 | Add PR size comment job to CI | 2026-03-10 | 423cc2f | [1-add-pr-size-comment-job-to-ci](./quick/1-add-pr-size-comment-job-to-ci/) |

## Session Continuity

Last session: 2026-03-10T15:51:00Z
Stopped at: Completed 11-02-PLAN.md
Resume file: .planning/phases/11-create-a-promo-website-for-neotolis-engine-with-feature-overview-and-dedicated-example-pages/11-03-PLAN.md
