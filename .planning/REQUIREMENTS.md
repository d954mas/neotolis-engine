# Requirements: Neotolis Engine v1.4

**Defined:** 2026-03-21
**Core Value:** Simple, fast, predictable -- composable features wired through code, zero hidden magic.

## v1.4 Requirements

### Logging (LOG)

- [x] **LOG-01**: `nt_log_info(fmt, ...)` / `nt_log_warn(fmt, ...)` / `nt_log_error(fmt, ...)` variadic functions with printf-style format args (no domain, direct call)
- [x] **LOG-02**: `NT_LOG_INFO(fmt, ...)` / `NT_LOG_WARN(fmt, ...)` / `NT_LOG_ERROR(fmt, ...)` macros that auto-inject `NT_LOG_DOMAIN` into output
- [x] **LOG-03**: `NT_LOG_DOMAIN_DEFAULT` auto-injected by `nt_add_module()` via CMake `target_compile_definitions`
- [x] **LOG-04**: Per-file domain override via `#define NT_LOG_DOMAIN "tag"` before `#include <nt_log.h>`
- [x] **LOG-05**: `#error` with clear usage instructions when `NT_LOG_DOMAIN` not defined and `NT_LOG_DOMAIN_DEFAULT` not set
- [x] **LOG-06**: WARN level added (current API has only INFO and ERROR)
- [x] **LOG-07**: Output format: `LEVEL:domain: message` (with domain) or `LEVEL: message` (without domain)
- [x] **LOG-08**: Stub backend updated for new variadic API signatures

### Texture Compression (TEX)

- [ ] **TEX-01**: Builder encodes PNG/JPG to ETC1S via Basis Universal encoder with full mip chain
- [ ] **TEX-02**: Builder supports UASTC mode for normal maps (per-texture option via `nt_tex_opts_t`)
- [ ] **TEX-03**: `NtTextureAssetHeader` v2 with compressed format enum, per-mip size metadata, backward compatible version field
- [ ] **TEX-04**: v1 raw texture packs continue to load alongside v2 compressed packs (runtime detects header version)
- [ ] **TEX-05**: Runtime transcoder module (`engine/basisu/`) with `extern "C"` wrapper over `basisu_transcoder.cpp`
- [ ] **TEX-06**: GPU compressed format detection at `nt_gfx_init()` via EM_JS / `emscripten_webgl_enable_extension()` (not `glGetStringi`), cached in `nt_gfx_gpu_caps_t`
- [ ] **TEX-07**: Transcoding target priority: ASTC > BC7 > BC1/BC3 > ETC2 > RGBA8 fallback
- [ ] **TEX-08**: `glCompressedTexImage2D` per-mip upload loop for compressed textures (replaces `glTexImage2D` + `glGenerateMipmap`)
- [ ] **TEX-09**: Unused Basis transcoder targets disabled (PVRTC1, PVRTC2, FXT1, ATC, KTX2) to minimize WASM size
- [ ] **TEX-10**: Basis transcoder compiled with `-fno-strict-aliasing`
- [ ] **TEX-11**: Sponza demo updated to use compressed texture packs

### Renderer (REND)

- [ ] **REND-01**: Compact instance struct -- `mat4x3` (3 rows of vec4) replacing `mat4`, shader reconstructs `(0,0,0,1)` row
- [ ] **REND-02**: Configurable color mode per material: NONE / RGBA8 / FLOAT4
- [ ] **REND-03**: Instance size reduced from 80 bytes to 48-64 bytes depending on color mode
- [ ] **REND-04**: LSD radix sort replacing `qsort` in `nt_render_items.c`
- [ ] **REND-05**: Full 64-bit sort key, 8-bit digits, histogram-based pass-skip optimization
- [ ] **REND-06**: Preallocated static scratch buffer for radix sort (no heap allocation in hot path)
- [ ] **REND-07**: Benchmark before/after on Sponza scene data to validate sort gains

### Material (MAT)

- [x] **MAT-01**: `nt_material_set_param(mat, name, value)` with hash lookup on existing `param_name_hashes[]`
- [x] **MAT-02**: ~~Material version bump on param change for renderer change detection~~ -- Superseded by D-05: renderer re-reads params every frame when material is bound, no version bump needed
- [x] **MAT-03**: `nt_material_set_param` writes all 4 components, `nt_material_set_param_component` writes a single component (per D-13, replaces original `set_param_float`/`set_param_vec4` convenience variants)

### Shape Renderer (SHAPE)

- [x] **SHAPE-01**: uint32 index buffer replacing uint16 in shape renderer batch
- [x] **SHAPE-02**: Remove 65535 vertex ceiling -- no more auto-flush at uint16 limit

### Builder (BUILD)

- [ ] **BUILD-01**: Headless GL context (GLFW hidden window, `GLFW_VISIBLE=GLFW_FALSE`) for shader compile validation
- [ ] **BUILD-02**: GLSL ES 3.00 -> GL 3.30 dialect swap (`#version 300 es` -> `#version 330 core`) before compile check
- [ ] **BUILD-03**: File-path-prefixed error messages on shader compile failure
- [ ] **BUILD-04**: Graceful skip when GL context creation fails (CI headless runners)

## v2 Requirements

### Deferred

- **TEX-D01**: Zstd supercompression on Basis data inside packs -- HTTP gzip/brotli sufficient for now
- **LOG-D01**: Log callback hook for game-side sinks (custom log destinations)
- **LOG-D02**: Log level compile-time filtering via `NT_LOG_LEVEL` CMake define (strip debug logs from release)
- **BUILD-D01**: Full shader program link validation (VS+FS pair) -- requires material defs at build time
- **TEX-D02**: Texture streaming / partial mip loading -- pack-level granularity sufficient for web
- **REND-D01**: GPU-side radix sort -- WebGL 2 has no compute shaders

## Out of Scope

| Feature | Reason |
|---------|--------|
| Material as pack asset | Runtime-only materials are sufficient, not needed |
| KTX2 container format | Basis .basis data stored directly in NEOPAK, no container needed |
| Runtime texture encoding | Builder does heavy work offline, violates engine philosophy |
| `glGenerateMipmap` for compressed textures | Does not work in WebGL 2 -- must use offline mips from builder |
| `##__VA_ARGS__` GNU extension in logging | Project uses `-Wpedantic -Werror`, C17 only |
| Per-call log domain override | Domain is per-file; use message text for sub-categorization |

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| LOG-01 | Phase 30 | Complete |
| LOG-02 | Phase 30 | Complete |
| LOG-03 | Phase 30 | Complete |
| LOG-04 | Phase 30 | Complete |
| LOG-05 | Phase 30 | Complete |
| LOG-06 | Phase 30 | Complete |
| LOG-07 | Phase 30 | Complete |
| LOG-08 | Phase 30 | Complete |
| MAT-01 | Phase 31 | Complete |
| MAT-02 | Phase 31 | Superseded (D-05) |
| MAT-03 | Phase 31 | Complete |
| SHAPE-01 | Phase 31 | Complete |
| SHAPE-02 | Phase 31 | Complete |
| REND-04 | Phase 32 | Pending |
| REND-05 | Phase 32 | Pending |
| REND-06 | Phase 32 | Pending |
| REND-07 | Phase 32 | Pending |
| REND-01 | Phase 33 | Pending |
| REND-02 | Phase 33 | Pending |
| REND-03 | Phase 33 | Pending |
| TEX-01 | Phase 34 | Pending |
| TEX-02 | Phase 34 | Pending |
| TEX-03 | Phase 34 | Pending |
| TEX-04 | Phase 34 | Pending |
| TEX-05 | Phase 34 | Pending |
| TEX-06 | Phase 34 | Pending |
| TEX-07 | Phase 34 | Pending |
| TEX-08 | Phase 34 | Pending |
| TEX-09 | Phase 34 | Pending |
| TEX-10 | Phase 34 | Pending |
| TEX-11 | Phase 34 | Pending |
| BUILD-01 | Phase 35 | Pending |
| BUILD-02 | Phase 35 | Pending |
| BUILD-03 | Phase 35 | Pending |
| BUILD-04 | Phase 35 | Pending |

**Coverage:**
- v1.4 requirements: 35 total
- Mapped to phases: 35
- Unmapped: 0

---
*Requirements defined: 2026-03-21*
*Last updated: 2026-03-22 -- MAT-02 superseded by D-05, MAT-03 updated per D-13*
