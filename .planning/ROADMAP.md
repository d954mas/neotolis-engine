# Roadmap: Neotolis Engine

## Milestones

- v1.0 Foundation Runtime (shipped 2026-03-09) - Phases 1-2
- v1.1 Modular Build (in progress) - Phases 3-11

## Phases

<details>
<summary>v1.0 Foundation Runtime (Phases 1-2) - SHIPPED 2026-03-09</summary>

See `.planning/milestones/v1.0-ROADMAP.md` for full history.

Phase 1: Project Scaffold -- repo structure, CMake presets, CI pipeline
Phase 2: Build Hardening -- warnings, sanitizers, clang-tidy, test framework

</details>

### v1.1 Modular Build (In Progress)

**Milestone Goal:** Split monolithic nt_engine into granular CMake targets so games link only the modules they need

- [x] **Phase 3: Build Infrastructure** - CMake helper function and config target that all future modules depend on
- [ ] **Phase 4: Module Split** - Extract nt_core, decouple cglm, remove umbrella target, migrate all consumers
- [ ] **Phase 5: Swappable Backends** - INTERFACE API contracts, multiple implementations, namespace ALIAS targets
- [ ] **Phase 6: Build Verification** - Optimized WASM validation, submodule consumption, full CI green
- [ ] **Phase 7: VSCode Setup** - Recommended extensions, build/run/debug tasks, launch configurations
- [x] **Phase 8: Track Output Game Size** - Size measurement scripts, CI tracking, GitHub Pages chart, flag benchmarking
- [x] **Phase 9: Size Reporting** - Diff-friendly WASM binary analysis report with CI integration
- [x] **Phase 10: HTML Shell** - Flexible minimal HTML shell system for WASM builds (gap closure in progress) (completed 2026-03-10)
- [ ] **Phase 11: Promo Website** - Astro static site with engine overview, features, and example showcase with live WASM demos

## Phase Details

### Phase 3: Build Infrastructure
**Goal**: Every engine module can be created with consistent flags, definitions, and include paths through a single helper call
**Depends on**: Phase 2 (build hardening -- warning flags, sanitizer config exist)
**Requirements**: INFRA-01, INFRA-02
**Success Criteria** (what must be TRUE):
  1. A new engine module can be added by writing one `nt_add_module()` call and listing its sources -- no manual flag or include setup
  2. Any module including `core/nt_platform.h` receives correct platform definitions (NT_PLATFORM_WEB on Emscripten, NT_PLATFORM_WIN on Windows, NT_PLATFORM_NATIVE on other) without explicit `-D` flags
  3. Both WASM and native preset builds succeed with the new infrastructure in place (existing code still compiles)
**Plans**: 2 plans

Plans:
- [x] 03-01-PLAN.md -- CMake helper, platform header, directory restructuring, consumer include path migration
- [x] 03-02-PLAN.md -- Platform detection unit tests and CI artifact check updates

### Phase 4: Module Split
**Goal**: The monolithic nt_engine target is replaced by independent per-module targets that consumers link explicitly
**Depends on**: Phase 3 (nt_add_module helper and nt_config must exist)
**Requirements**: SPLIT-01, SPLIT-02, SPLIT-03, SPLIT-04
**Success Criteria** (what must be TRUE):
  1. `nt_core` exists as an independent STATIC library target with its own CMakeLists.txt, built by `nt_add_module()`
  2. cglm is a standalone CMake target -- not bundled into any engine module, linked directly by consumers that need it
  3. No `nt_engine` target exists anywhere in the build system (cmake --build produces no libnt_engine artifact)
  4. examples/hello, all tests, and builder each link only the specific modules they use -- no transitive engine dependencies
  5. Both WASM and native builds succeed with all consumers linking granular modules
**Plans**: 1 plan

Plans:
- [ ] 04-01-PLAN.md -- Remove nt_engine shim, migrate all consumers to explicit module linking

### Phase 5: Swappable Backends
**Goal**: A module can have multiple implementations sharing the same API, and consumers choose which implementation to link
**Depends on**: Phase 4 (at least one real module target must exist to demonstrate the pattern)
**Requirements**: INFRA-03, SWAP-01, SWAP-02, SWAP-03
**Success Criteria** (what must be TRUE):
  1. An INTERFACE target exists that defines a module API (headers only, no compiled sources)
  2. At least two STATIC targets implement the same API (e.g., nt_log with real output and nt_log_empty as no-op stub)
  3. A test or example can switch between implementations by changing one target_link_libraries line -- no other code changes needed
  4. All engine modules have `nt::` namespace ALIAS targets (nt::core, nt::log, etc.) that consumers can use
**Plans**: 2 plans

Plans:
- [ ] 05-01-PLAN.md -- ALIAS auto-creation in nt_add_module(), nt_log module with real and stub implementations
- [ ] 05-02-PLAN.md -- Swap demo test targets (same source, different link) and CI verification updates

### Phase 6: Build Verification
**Goal**: The modular build is validated under production conditions -- optimized WASM, submodule consumption, and full CI
**Depends on**: Phase 5 (all module targets and patterns must be in place)
**Requirements**: QUAL-01, QUAL-02, QUAL-03
**Success Criteria** (what must be TRUE):
  1. Optimized WASM build (-O2 or -Os) produces a working .wasm/.js that does not silently drop required symbols from granular static libraries
  2. An external CMake project can consume the engine via add_subdirectory() (git submodule pattern), linking individual modules without errors
  3. CI pipeline passes green for all four presets: wasm-debug, wasm-release, native-debug, native-release
**Plans**: 2 plans

Plans:
- [ ] 06-01-PLAN.md -- Parameterize WASM smoke test for preset selection, create submodule consumption test project
- [ ] 06-02-PLAN.md -- Update CI with wasm-release smoke test and submodule test steps, verify all builds pass

## Progress

**Execution Order:**
Phases execute in numeric order: 3 -> 4 -> 5 -> 6 -> 7 -> 8 -> 9 -> 10 -> 11

| Phase | Milestone | Plans Complete | Status | Completed |
|-------|-----------|----------------|--------|-----------|
| 1. Project Scaffold | v1.0 | 2/2 | Complete | 2026-03-09 |
| 2. Build Hardening | v1.0 | 2/2 | Complete | 2026-03-09 |
| 3. Build Infrastructure | v1.1 | 2/2 | Complete | 2026-03-09 |
| 4. Module Split | v1.1 | 0/1 | Not started | - |
| 5. Swappable Backends | v1.1 | 0/2 | Not started | - |
| 6. Build Verification | v1.1 | 0/2 | Not started | - |
| 7. VSCode Setup | v1.1 | 1/2 | In Progress | - |
| 8. Track Output Game Size | v1.1 | 2/2 | Complete | 2026-03-10 |
| 9. Size Reporting | v1.1 | 2/2 | Complete | 2026-03-10 |
| 10. HTML Shell | v1.1 | 3/3 | Complete | 2026-03-10 |
| 11. Promo Website | 2/3 | In Progress|  | - |

### Phase 7: VSCode Setup
**Goal**: Developer can open the repo in VSCode and immediately build, run, and debug any example or game target with zero manual setup
**Depends on**: Phase 3 (CMake presets and module structure must exist)
**Requirements**: DX-01, DX-02, DX-03, DX-04
**Success Criteria** (what must be TRUE):
  1. `.vscode/extensions.json` recommends all required extensions (C/C++, CMake, clang-format, etc.)
  2. VSCode tasks allow building any preset (native-debug, native-release, wasm-debug, wasm-release) via Ctrl+Shift+B
  3. Launch configurations allow running and debugging native examples/games with F5
  4. WASM examples can be served and opened in browser via a task
**Plans**: 2 plans

Plans:
- [x] 07-01-PLAN.md -- Create all VSCode config files (extensions, settings, tasks, launch) and update .gitignore
- [ ] 07-02-PLAN.md -- Human verification of all 4 DX requirements in live VSCode session

### Phase 8: Track Output Game Size
**Goal**: WASM release build sizes are measured, tracked over time in CI, visualized on GitHub Pages, and Emscripten optimization flags are benchmarked
**Depends on**: Phase 7
**Requirements**: SIZE-01, SIZE-02, SIZE-03, SIZE-04, SIZE-05
**Success Criteria** (what must be TRUE):
  1. `scripts/size.sh hello` prints a formatted table of raw and gzip sizes for all wasm-release output files
  2. `scripts/size.sh hello --json` produces valid JSON consumable by CI
  3. `scripts/benchmark-flags.sh` tests Emscripten flag combinations and shows results before applying any changes
  4. CI size-track job runs on master push, measures sizes, stores per-target JSON on gh-pages branch
  5. GitHub Pages chart page visualizes size history with commit-level granularity
  6. CI warns on >10% size growth without failing the build
**Plans**: 2 plans

Plans:
- [x] 08-01-PLAN.md -- Create size.sh measurement script and benchmark-flags.sh optimization benchmark
- [x] 08-02-PLAN.md -- Add CI size-track job to workflow and create GitHub Pages chart page

### Phase 9: Size Reporting
**Goal**: A detailed, diff-friendly WASM binary analysis report is generated for every target, stored on gh-pages for change tracking, and produced automatically in CI on master push and PR builds
**Depends on**: Phase 8 (size.sh and CI size-track job must exist)
**Requirements**: REPORT-01, REPORT-02, REPORT-03, REPORT-04
**Success Criteria** (what must be TRUE):
  1. `scripts/size-report.sh hello` generates a text report with WASM section sizes, top 30 functions by size, per-module contributions, and data segments summary (no artifact sizes -- covered by size.sh)
  2. The report output is stable and diff-friendly -- same build produces identical report, deterministic sorting, no timestamps in body
  3. CI size-track job generates the report on master push, prints to CI log, and commits the report file to gh-pages branch
  4. CI pr-size-comment job generates the report on PRs, prints to CI log, and posts a three-column diff comment (Master | PR | Delta) with new/removed markers
**Plans**: 2 plans

Plans:
- [x] 09-01-PLAN.md -- Create wasm-analysis CMake preset and size-report.sh analysis script
- [x] 09-02-PLAN.md -- Extend CI size-track and pr-size-comment jobs with size report integration

### Phase 10: Flexible Minimal HTML Shell for WASM Builds
**Goal**: The static hardcoded HTML shell is replaced by a flexible, minimal template system where games configure shell options via CMake and can fully override the shell
**Depends on**: Phase 9
**Requirements**: SHELL-01, SHELL-02, SHELL-03, SHELL-04, SHELL-05
**Success Criteria** (what must be TRUE):
  1. A minimal `shell.html.in` template exists with `@VAR@` placeholders processed by CMake `configure_file(@ONLY)` at configure time and `{{{ SCRIPT }}}` resolved by Emscripten at link time
  2. The default shell renders a full-viewport canvas with zero chrome (no headings, no output div, no decorative borders)
  3. `nt_configure_shell()` CMake function configures the shell for a target, with TITLE and SHELL_FILE parameters
  4. A game can bypass the engine shell entirely by passing `SHELL_FILE` to `nt_configure_shell()`
  5. New template variables can be added by editing `shell.html.in` and `nt_shell.cmake` only -- no architectural changes needed
**Plans**: 3 plans

Plans:
- [x] 10-01-PLAN.md -- Create shell template, CMake configuration function, migrate hello example
- [x] 10-02-PLAN.md -- Fix @@keyframes CSS escape bug in shell template (gap closure)
- [ ] 10-03-PLAN.md -- Fix spinner hide interop, crash overlay, and context menu (gap closure from UAT)

### Phase 11: Promo Website
**Goal:** Build a promo website with engine overview, features section, and example showcase (cards on home page + dedicated page per example). Example promo data (title, description, cover) lives in `examples/<name>/site_promo/` next to example source code, not in a centralized store.
**Source:** [d954mas/neotolis-engine#10](https://github.com/d954mas/neotolis-engine/issues/10)
**Requirements**: SITE-01, SITE-02, SITE-03, SITE-04, SITE-05, SITE-06, SITE-07, SITE-08, SITE-09, SITE-10
**Depends on:** Phase 10
**Plans:** 2/3 plans executed

Plans:
- [ ] 11-01-PLAN.md -- Astro project scaffold with Tailwind dark theme, layouts, Hero/Overview/Features sections
- [ ] 11-02-PLAN.md -- Example promo data, content collection, example cards, and per-example WASM demo pages
- [ ] 11-03-PLAN.md -- CI integration to build Astro site and deploy to gh-pages with WASM artifacts
