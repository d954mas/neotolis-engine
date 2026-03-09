# Requirements: Neotolis Engine -- v1.1 Modular Build

**Defined:** 2026-03-09
**Core Value:** Simple, fast, predictable engine runtime -- composable features wired by game code with zero hidden magic

## v1.1 Requirements

Requirements for modular build milestone. Each maps to roadmap phases.

### Build Infrastructure

- [x] **INFRA-01**: `nt_add_module()` CMake helper creates STATIC library target with include dirs, warning/sanitizer flags, and output directory in a single call
- [x] **INFRA-02**: `nt_config` INTERFACE target provides platform definitions (NT_PLATFORM_WEB, NT_PLATFORM_NATIVE, NT_CONFIG_DEBUG) to all modules via explicit linking
- [ ] **INFRA-03**: `nt::` namespace ALIAS targets for each module (nt::core, nt::log, etc.)

### Module Split

- [x] **SPLIT-01**: `nt_core` as a separate STATIC library target with its own CMakeLists.txt
- [x] **SPLIT-02**: cglm as standalone CMake target, not bundled into any engine module
- [x] **SPLIT-03**: Umbrella `nt_engine` target removed -- no single aggregate target
- [x] **SPLIT-04**: All consumers (examples/hello, tests, builder) link individual modules explicitly

### Swappable Backends

- [ ] **SWAP-01**: INTERFACE target for API contract (header-only) of a module with multiple implementations
- [ ] **SWAP-02**: At least two implementations of the same module (e.g., nt_log and nt_log_empty) sharing the same API
- [ ] **SWAP-03**: Example/test demonstrates linking different implementations of the same API

### Build Quality

- [ ] **QUAL-01**: Optimized WASM build (-O2/-Os) does not strip required symbols after module split
- [ ] **QUAL-02**: Engine works as git submodule via add_subdirectory() -- external project links individual modules
- [ ] **QUAL-03**: CI passes for all presets (wasm-debug, wasm-release, native-debug, native-release)

## v2 Requirements

Deferred to future milestones. Tracked but not in current roadmap.

### Future Modules

- **MOD-01**: nt_memory module with frame scratch allocator
- **MOD-02**: nt_platform module with web backend
- **MOD-03**: nt_entity module with ECS storage
- **MOD-04**: nt_render module with WebGL 2 backend

## Out of Scope

| Feature | Reason |
|---------|--------|
| find_package / install / export | Engine consumed via add_subdirectory, not system-installed |
| OBJECT libraries | STATIC is correct for independently linkable modules |
| FetchContent for vendored deps | Already vendored in deps/, no need for download |
| Automatic module discovery | Explicit is better than implicit per engine philosophy |
| Dynamic/shared libraries | WASM target, static linking only |
| CMake COMPONENTS support | Over-engineering for source-consumed library |

## Traceability

Which phases cover which requirements. Updated during roadmap creation.

| Requirement | Phase | Status |
|-------------|-------|--------|
| INFRA-01 | Phase 3: Build Infrastructure | Complete |
| INFRA-02 | Phase 3: Build Infrastructure | Complete |
| INFRA-03 | Phase 5: Swappable Backends | Pending |
| SPLIT-01 | Phase 4: Module Split | Complete |
| SPLIT-02 | Phase 4: Module Split | Complete |
| SPLIT-03 | Phase 4: Module Split | Complete |
| SPLIT-04 | Phase 4: Module Split | Complete |
| SWAP-01 | Phase 5: Swappable Backends | Pending |
| SWAP-02 | Phase 5: Swappable Backends | Pending |
| SWAP-03 | Phase 5: Swappable Backends | Pending |
| QUAL-01 | Phase 6: Build Verification | Pending |
| QUAL-02 | Phase 6: Build Verification | Pending |
| QUAL-03 | Phase 6: Build Verification | Pending |

**Coverage:**
- v1.1 requirements: 13 total
- Mapped to phases: 13
- Unmapped: 0

---
*Requirements defined: 2026-03-09*
*Last updated: 2026-03-09 after roadmap creation*
