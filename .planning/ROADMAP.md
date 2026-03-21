# Roadmap: Neotolis Engine

## Milestones

- v1.0 Foundation Runtime (shipped 2026-03-09) - Phases 1-2
- v1.1 Modular Build (shipped 2026-03-10) - Phases 3-11
- v1.2 Runtime Renderer (shipped 2026-03-16) - Phases 12-19
- v1.3 Asset Pipeline (shipped 2026-03-21) - Phases 20-29
- v1.4 Rendering & Textures (in progress) - Phases 30-35

## Phases

<details>
<summary>v1.0 Foundation Runtime (Phases 1-2) - SHIPPED 2026-03-09</summary>

See `.planning/milestones/v1.0-ROADMAP.md` for full history.

Phase 1: Project Scaffold -- repo structure, CMake presets, CI pipeline
Phase 2: Build Hardening -- warnings, sanitizers, clang-tidy, test framework

</details>

<details>
<summary>v1.1 Modular Build (Phases 3-11) - SHIPPED 2026-03-10</summary>

See `.planning/milestones/v1.1-ROADMAP.md` for full history.

- [x] Phase 3: Build Infrastructure (2/2 plans)
- [x] Phase 4: Module Split (1/1 plan)
- [x] Phase 5: Swappable Backends (2/2 plans)
- [x] Phase 6: Build Verification (2/2 plans)
- [x] Phase 7: VSCode Setup (2/2 plans)
- [x] Phase 8: Track Output Game Size (2/2 plans)
- [x] Phase 9: Size Reporting (2/2 plans)
- [x] Phase 10: HTML Shell (3/3 plans)
- [x] Phase 11: Promo Website (3/3 plans)

</details>

<details>
<summary>v1.2 Runtime Renderer (Phases 12-19) - SHIPPED 2026-03-16</summary>

See `.planning/milestones/v1.2-ROADMAP.md` for full history.

- [x] Phase 12: Frame Lifecycle (2/2 plans)
- [x] Phase 13: Web Platform Layer (2/2 plans)
- [x] Phase 14: Input System (2/2 plans)
- [x] Phase 15: WebGL 2 Renderer (2/2 plans)
- [x] Phase 16: Shape Renderer (2/2 plans)
- [x] Phase 17: Demo Integration (1/1 plan)
- [x] Phase 18: Desktop Build (2/2 plans)
- [x] Phase 19: Isolate GLFW behind nt_window API (1/1 plan)

</details>

<details>
<summary>v1.3 Asset Pipeline (Phases 20-29) - SHIPPED 2026-03-21</summary>

See `.planning/milestones/v1.3-ROADMAP.md` for full history.

- [x] Phase 20: Shared Format Headers (2/2 plans)
- [x] Phase 21: Texture Support (2/2 plans)
- [x] Phase 22: Entity System (3/3 plans)
- [x] Phase 23: Builder (3/3 plans)
- [x] Phase 24: Asset Registry (3/3 plans)
- [x] Phase 25: Asset Loading (3/3 plans)
- [x] Phase 26: Material System (2/2 plans)
- [x] Phase 27: Mesh Rendering Pipeline (2/2 plans)
- [x] Phase 28: Demo Integration (5/5 plans)
- [x] Phase 29: nt_hash module (3/3 plans)

</details>

### v1.4 Rendering & Textures (In Progress)

- [ ] **Phase 30: Logging Infrastructure** - Variadic printf-style log with auto module domains via CMake
- [ ] **Phase 31: Material & Shape Fixes** - Runtime material param updates and uint32 shape indices
- [ ] **Phase 32: Radix Sort** - LSD radix sort replacing qsort for render items
- [ ] **Phase 33: Compact Instance Data** - mat4x3 transform with configurable color mode
- [ ] **Phase 34: Basis Universal Compression** - GPU texture compression pipeline (builder encoder + runtime transcoder)
- [ ] **Phase 35: Builder Shader Validation** - Headless GL compile check for shaders in builder

## Phase Details

### Phase 30: Logging Infrastructure
**Goal**: Engine modules emit formatted, domain-tagged diagnostic output through a variadic printf-style API
**Depends on**: Nothing (first phase of v1.4)
**Requirements**: LOG-01, LOG-02, LOG-03, LOG-04, LOG-05, LOG-06, LOG-07, LOG-08
**Success Criteria** (what must be TRUE):
  1. Calling `nt_log_info("value=%d", 42)` from any module prints `INFO: value=42` with printf formatting
  2. Calling `NT_LOG_INFO("started")` from a module with `NT_LOG_DOMAIN_DEFAULT` set prints `INFO:module_name: started` with the domain auto-injected
  3. Defining `#define NT_LOG_DOMAIN "custom"` before including `nt_log.h` overrides the CMake-injected domain for that file
  4. A source file that uses `NT_LOG_INFO` without any domain defined (neither CMake nor `#define`) fails to compile with a `#error` explaining how to fix it
  5. WARN level works alongside existing INFO and ERROR levels in both function and macro variants
**Plans**: 3 plans

Plans:
- [x] 30-01-PLAN.md -- New variadic logging API, CMake LOG_DOMAIN infrastructure, stub update, unit tests
- [ ] 30-02-PLAN.md -- Engine module + example call site migration to domain macros / variadic functions
- [x] 30-03-PLAN.md -- Builder printf/fprintf migration to nt_log domain macros

### Phase 31: Material & Shape Fixes
**Goal**: Materials support runtime parameter changes and the shape renderer handles arbitrarily large meshes
**Depends on**: Phase 30
**Requirements**: MAT-01, MAT-02, MAT-03, SHAPE-01, SHAPE-02
**Success Criteria** (what must be TRUE):
  1. Calling `nt_material_set_param(mat, "color", value)` updates the named uniform and the renderer picks up the change on the next frame
  2. `nt_material_set_param_float` and `nt_material_set_param_vec4` convenience variants work for common types
  3. The shape renderer draws meshes with more than 65535 vertices without flushing or corruption
  4. Shape renderer index buffers use uint32 throughout (no uint16 code paths remain)
**Plans**: TBD

Plans:
- [ ] 31-01: TBD
- [ ] 31-02: TBD

### Phase 32: Radix Sort
**Goal**: Render item sorting runs in O(n) via radix sort with zero heap allocation in the hot path
**Depends on**: Phase 30
**Requirements**: REND-04, REND-05, REND-06, REND-07
**Success Criteria** (what must be TRUE):
  1. Render items are sorted by 64-bit sort key using LSD radix sort instead of qsort
  2. The sort uses only preallocated static scratch memory -- no malloc/calloc/realloc during sort
  3. Passes where all items share the same digit value are skipped (histogram optimization)
  4. Benchmark on Sponza scene data demonstrates measurable improvement over qsort baseline
**Plans**: TBD

Plans:
- [ ] 32-01: TBD
- [ ] 32-02: TBD

### Phase 33: Compact Instance Data
**Goal**: Instance buffer bandwidth reduced 20-40% via mat4x3 transforms and configurable color precision
**Depends on**: Phase 32
**Requirements**: REND-01, REND-02, REND-03
**Success Criteria** (what must be TRUE):
  1. Instance struct uses mat4x3 (3 rows of vec4) instead of full mat4 -- vertex shader reconstructs the fourth row
  2. Materials can specify color mode NONE (no color), RGBA8 (4 bytes packed), or FLOAT4 (16 bytes) per material
  3. Instance size is 48 bytes (NONE), 52 bytes (RGBA8), or 64 bytes (FLOAT4) -- verified by static assert
  4. Sponza demo renders identically before and after the instance layout change
**Plans**: TBD

Plans:
- [ ] 33-01: TBD
- [ ] 33-02: TBD

### Phase 34: Basis Universal Compression
**Goal**: Textures are GPU-compressed offline by the builder and transcoded at runtime to the best available GPU format
**Depends on**: Phase 33
**Requirements**: TEX-01, TEX-02, TEX-03, TEX-04, TEX-05, TEX-06, TEX-07, TEX-08, TEX-09, TEX-10, TEX-11
**Success Criteria** (what must be TRUE):
  1. Builder produces ETC1S-compressed textures with full offline mip chains from PNG/JPG sources
  2. Builder produces UASTC-compressed textures for normal maps when configured via texture options
  3. Runtime detects GPU compressed texture support (ASTC, BC7, BC1/BC3, ETC2) at init via EM_JS and caches the result
  4. Runtime transcodes Basis data to the best available GPU format and uploads via `glCompressedTexImage2D` per mip level
  5. Packs containing v1 raw textures continue to load and render correctly alongside v2 compressed packs
  6. Sponza demo runs with compressed texture packs and renders correctly
**Plans**: TBD

Plans:
- [ ] 34-01: TBD
- [ ] 34-02: TBD
- [ ] 34-03: TBD
- [ ] 34-04: TBD

### Phase 35: Builder Shader Validation
**Goal**: Builder validates GLSL shaders at build time by compiling them against a real GL context
**Depends on**: Phase 30
**Requirements**: BUILD-01, BUILD-02, BUILD-03, BUILD-04
**Success Criteria** (what must be TRUE):
  1. Builder creates a headless GLFW window (invisible) with GL 3.3 context for shader compilation
  2. GLSL ES 3.00 shaders are automatically converted to GL 3.30 dialect before compile check
  3. Shader compile errors include the source file path in the error message
  4. When GL context creation fails (headless CI), validation is skipped gracefully with a warning instead of a hard error
**Plans**: TBD

Plans:
- [ ] 35-01: TBD
- [ ] 35-02: TBD

## Progress

| Phase | Milestone | Plans Complete | Status | Completed |
|-------|-----------|----------------|--------|-----------|
| 1. Project Scaffold | v1.0 | 2/2 | Complete | 2026-03-09 |
| 2. Build Hardening | v1.0 | 2/2 | Complete | 2026-03-09 |
| 3. Build Infrastructure | v1.1 | 2/2 | Complete | 2026-03-09 |
| 4. Module Split | v1.1 | 1/1 | Complete | 2026-03-09 |
| 5. Swappable Backends | v1.1 | 2/2 | Complete | 2026-03-09 |
| 6. Build Verification | v1.1 | 2/2 | Complete | 2026-03-10 |
| 7. VSCode Setup | v1.1 | 2/2 | Complete | 2026-03-10 |
| 8. Track Output Game Size | v1.1 | 2/2 | Complete | 2026-03-10 |
| 9. Size Reporting | v1.1 | 2/2 | Complete | 2026-03-10 |
| 10. HTML Shell | v1.1 | 3/3 | Complete | 2026-03-10 |
| 11. Promo Website | v1.1 | 3/3 | Complete | 2026-03-10 |
| 12. Frame Lifecycle | v1.2 | 2/2 | Complete | 2026-03-10 |
| 13. Web Platform Layer | v1.2 | 2/2 | Complete | 2026-03-11 |
| 14. Input System | v1.2 | 2/2 | Complete | 2026-03-11 |
| 15. WebGL 2 Renderer | v1.2 | 2/2 | Complete | 2026-03-12 |
| 16. Shape Renderer | v1.2 | 2/2 | Complete | 2026-03-12 |
| 17. Demo Integration | v1.2 | 1/1 | Complete | 2026-03-13 |
| 18. Desktop Build | v1.2 | 2/2 | Complete | 2026-03-13 |
| 19. Window Isolation | v1.2 | 1/1 | Complete | 2026-03-16 |
| 20. Shared Format Headers | v1.3 | 2/2 | Complete | 2026-03-16 |
| 21. Texture Support | v1.3 | 2/2 | Complete | 2026-03-16 |
| 22. Entity System | v1.3 | 3/3 | Complete | 2026-03-16 |
| 23. Builder | v1.3 | 3/3 | Complete | 2026-03-17 |
| 24. Asset Registry | v1.3 | 3/3 | Complete | 2026-03-17 |
| 25. Asset Loading | v1.3 | 3/3 | Complete | 2026-03-18 |
| 26. Material System | v1.3 | 2/2 | Complete | 2026-03-19 |
| 27. Mesh Rendering Pipeline | v1.3 | 2/2 | Complete | 2026-03-19 |
| 28. Demo Integration | v1.3 | 5/5 | Complete | 2026-03-21 |
| 29. nt_hash module | v1.3 | 3/3 | Complete | 2026-03-19 |
| 30. Logging Infrastructure | v1.4 | 2/3 | In Progress|  |
| 31. Material & Shape Fixes | v1.4 | 0/? | Not started | - |
| 32. Radix Sort | v1.4 | 0/? | Not started | - |
| 33. Compact Instance Data | v1.4 | 0/? | Not started | - |
| 34. Basis Universal Compression | v1.4 | 0/? | Not started | - |
| 35. Builder Shader Validation | v1.4 | 0/? | Not started | - |
