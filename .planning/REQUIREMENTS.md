# Requirements: Neotolis Engine -- v1.1 Modular Build

**Defined:** 2026-03-09
**Core Value:** Simple, fast, predictable engine runtime -- composable features wired by game code with zero hidden magic

## v1.1 Requirements

Requirements for modular build milestone. Each maps to roadmap phases.

### Build Infrastructure

- [x] **INFRA-01**: `nt_add_module()` CMake helper creates STATIC library target with include dirs, warning/sanitizer flags, and output directory in a single call
- [x] **INFRA-02**: `nt_config` INTERFACE target provides platform definitions (NT_PLATFORM_WEB, NT_PLATFORM_NATIVE, NT_CONFIG_DEBUG) to all modules via explicit linking
- [x] **INFRA-03**: `nt::` namespace ALIAS targets for each module (nt::core, nt::log, etc.)

### Module Split

- [x] **SPLIT-01**: `nt_core` as a separate STATIC library target with its own CMakeLists.txt
- [x] **SPLIT-02**: cglm as standalone CMake target, not bundled into any engine module
- [x] **SPLIT-03**: Umbrella `nt_engine` target removed -- no single aggregate target
- [x] **SPLIT-04**: All consumers (examples/hello, tests, builder) link individual modules explicitly

### Swappable Backends

- [x] **SWAP-01**: INTERFACE target for API contract (header-only) of a module with multiple implementations
- [x] **SWAP-02**: At least two implementations of the same module (e.g., nt_log and nt_log_empty) sharing the same API
- [x] **SWAP-03**: Example/test demonstrates linking different implementations of the same API

### Build Quality

- [ ] **QUAL-01**: Optimized WASM build (-O2/-Os) does not strip required symbols after module split
- [ ] **QUAL-02**: Engine works as git submodule via add_subdirectory() -- external project links individual modules
- [ ] **QUAL-03**: CI passes for all presets (wasm-debug, wasm-release, native-debug, native-release)

### Developer Experience

- [x] **DX-01**: `.vscode/extensions.json` recommends all required extensions (C/C++, CMake Tools, CMake Language, clang-format)
- [x] **DX-02**: VSCode build tasks for all 4 presets with native-debug as default (Ctrl+Shift+B)
- [x] **DX-03**: Launch configurations for debugging native targets with F5 using cppvsdbg
- [x] **DX-04**: WASM serve task via emrun to open examples in browser

### Size Tracking

- [x] **SIZE-01**: `scripts/size.sh` measures raw and gzip sizes of wasm-release build outputs for a named target, prints formatted table and supports `--json` output
- [x] **SIZE-02**: `scripts/benchmark-flags.sh` benchmarks Emscripten optimization flag combinations against baseline and interactively offers to apply winning flags
- [x] **SIZE-03**: CI size-track job on master push measures sizes and persists per-target JSON history to gh-pages branch
- [x] **SIZE-04**: GitHub Pages chart page visualizes per-target size history over time using Chart.js
- [x] **SIZE-05**: CI warns on >10% size regression without failing the build

### Size Reporting

- [x] **REPORT-01**: `scripts/size-report.sh` generates a detailed WASM binary analysis report (WASM sections, top functions, data segments, per-module contributions) for a named target and prints to stdout (CI handles persistence to gh-pages)
- [x] **REPORT-02**: Report output is stable and diff-friendly -- deterministic sorting, no timestamps in body, fixed-width formatting
- [x] **REPORT-03**: CI size-track job generates the size report on master push and commits updated report file to gh-pages
- [x] **REPORT-04**: CI pr-size-comment job generates and logs the size report on PRs for visibility

### HTML Shell

- [x] **SHELL-01**: Default minimal shell template (`shell.html.in`) exists for WASM builds with full-viewport canvas and `@VAR@` placeholders processed by CMake `configure_file(@ONLY)`
- [x] **SHELL-02**: Canvas fills the full browser viewport with no scrollbars, no decorative chrome, and zero border/padding (Emscripten mouse coordinate compatibility)
- [x] **SHELL-03**: Game can replace the default shell entirely by passing a custom `SHELL_FILE` path to `nt_configure_shell()`
- [x] **SHELL-04**: Shell can be extended with new template options by adding `@VAR@` placeholders to `shell.html.in` and parameters to `nt_configure_shell()` -- no architectural changes needed
- [x] **SHELL-05**: Solution uses only CMake `configure_file` and Emscripten `--shell-file` -- no external templating dependencies

### Promo Website

- [x] **SITE-01**: Astro static site in `neotolis-engine-site/` generates pure HTML with zero client-side JS, styled with Tailwind CSS dark theme
- [x] **SITE-02**: Home page is a single scrollable page with Hero, Overview, Features, and Examples sections
- [x] **SITE-03**: Header has anchor links (Overview, Features, Examples) and a prominent GitHub Star button
- [x] **SITE-04**: Features section displays engine capabilities as a simple list with bold title + description per feature (no icons, no cards)
- [x] **SITE-05**: Example promo data (title, description, cover image) lives in `examples/<name>/site_promo/` and is loaded via Astro Content Collections
- [x] **SITE-06**: Example cards on home page show cover image (or auto-generated placeholder), title, total gzipped size, and collapsible per-file size breakdown
- [x] **SITE-07**: Size data on example cards comes from CI-generated JSON on gh-pages (not hardcoded)
- [x] **SITE-08**: Each example has a dedicated page with back button, title, and full-viewport iframe embedding the live WASM demo
- [ ] **SITE-09**: CI size-track job builds the Astro site and deploys it to gh-pages alongside existing size tracking data without deleting existing content
- [ ] **SITE-10**: WASM build outputs are deployed to gh-pages under `wasm/<name>/` so example page iframes can load them

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
| Auto-screenshot for cover images | Deferred -- headless browser in CI adds complexity |
| Light/dark theme toggle | Deferred -- dark theme only per user decision |
| Separate /examples listing page | Deferred -- not needed until examples grow beyond ~10 |
| Size history charts in site | Deferred -- currently separate Chart.js pages on gh-pages |

## Traceability

Which phases cover which requirements. Updated during roadmap creation.

| Requirement | Phase | Status |
|-------------|-------|--------|
| INFRA-01 | Phase 3: Build Infrastructure | Complete |
| INFRA-02 | Phase 3: Build Infrastructure | Complete |
| INFRA-03 | Phase 5: Swappable Backends | Complete |
| SPLIT-01 | Phase 4: Module Split | Complete |
| SPLIT-02 | Phase 4: Module Split | Complete |
| SPLIT-03 | Phase 4: Module Split | Complete |
| SPLIT-04 | Phase 4: Module Split | Complete |
| SWAP-01 | Phase 5: Swappable Backends | Complete |
| SWAP-02 | Phase 5: Swappable Backends | Complete |
| SWAP-03 | Phase 5: Swappable Backends | Complete |
| QUAL-01 | Phase 6: Build Verification | Pending |
| QUAL-02 | Phase 6: Build Verification | Pending |
| QUAL-03 | Phase 6: Build Verification | Pending |
| DX-01 | Phase 7: VSCode Setup | Complete |
| DX-02 | Phase 7: VSCode Setup | Complete |
| DX-03 | Phase 7: VSCode Setup | Complete |
| DX-04 | Phase 7: VSCode Setup | Complete |
| SIZE-01 | Phase 8: Track Output Game Size | Complete |
| SIZE-02 | Phase 8: Track Output Game Size | Complete |
| SIZE-03 | Phase 8: Track Output Game Size | Complete |
| SIZE-04 | Phase 8: Track Output Game Size | Complete |
| SIZE-05 | Phase 8: Track Output Game Size | Complete |
| REPORT-01 | Phase 9: Size Reporting | Complete |
| REPORT-02 | Phase 9: Size Reporting | Complete |
| REPORT-03 | Phase 9: Size Reporting | Complete |
| REPORT-04 | Phase 9: Size Reporting | Complete |
| SHELL-01 | Phase 10: HTML Shell | Complete |
| SHELL-02 | Phase 10: HTML Shell | Complete |
| SHELL-03 | Phase 10: HTML Shell | Complete |
| SHELL-04 | Phase 10: HTML Shell | Complete |
| SHELL-05 | Phase 10: HTML Shell | Complete |
| SITE-01 | Phase 11: Promo Website | Complete |
| SITE-02 | Phase 11: Promo Website | Complete |
| SITE-03 | Phase 11: Promo Website | Complete |
| SITE-04 | Phase 11: Promo Website | Complete |
| SITE-05 | Phase 11: Promo Website | Complete |
| SITE-06 | Phase 11: Promo Website | Complete |
| SITE-07 | Phase 11: Promo Website | Complete |
| SITE-08 | Phase 11: Promo Website | Complete |
| SITE-09 | Phase 11: Promo Website | Pending |
| SITE-10 | Phase 11: Promo Website | Pending |

**Coverage:**
- v1.1 requirements: 41 total
- Mapped to phases: 41
- Unmapped: 0

---
*Requirements defined: 2026-03-09*
*Last updated: 2026-03-10 after Phase 11 planning*
