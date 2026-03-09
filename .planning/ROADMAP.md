# Roadmap: Neotolis Engine

## Milestones

- v1.0 Foundation Runtime (shipped 2026-03-09) - Phases 1-2
- v1.1 Modular Build (in progress) - Phases 3-6

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
**Plans**: TBD

Plans:
- [ ] 05-01: TBD
- [ ] 05-02: TBD

### Phase 6: Build Verification
**Goal**: The modular build is validated under production conditions -- optimized WASM, submodule consumption, and full CI
**Depends on**: Phase 5 (all module targets and patterns must be in place)
**Requirements**: QUAL-01, QUAL-02, QUAL-03
**Success Criteria** (what must be TRUE):
  1. Optimized WASM build (-O2 or -Os) produces a working .wasm/.js that does not silently drop required symbols from granular static libraries
  2. An external CMake project can consume the engine via add_subdirectory() (git submodule pattern), linking individual modules without errors
  3. CI pipeline passes green for all four presets: wasm-debug, wasm-release, native-debug, native-release
**Plans**: TBD

Plans:
- [ ] 06-01: TBD
- [ ] 06-02: TBD

## Progress

**Execution Order:**
Phases execute in numeric order: 3 -> 4 -> 5 -> 6

| Phase | Milestone | Plans Complete | Status | Completed |
|-------|-----------|----------------|--------|-----------|
| 1. Project Scaffold | v1.0 | 2/2 | Complete | 2026-03-09 |
| 2. Build Hardening | v1.0 | 2/2 | Complete | 2026-03-09 |
| 3. Build Infrastructure | v1.1 | 2/2 | Complete | 2026-03-09 |
| 4. Module Split | v1.1 | 0/1 | Not started | - |
| 5. Swappable Backends | v1.1 | 0/? | Not started | - |
| 6. Build Verification | v1.1 | 0/? | Not started | - |
