# Render Improvements

Research-based list of rendering improvements for Neotolis Engine.
Inspired by Sebastian Aaltonen's work on HypeHype renderer (SIGGRAPH 2023/2024, REAC 2023).

Context: C17, WebGL 2, WASM, mobile web target.

---

## Tier 1 — Near-term

### 1. Sprite/UI Texture Atlas

**Status:** planned
**Scope:** builder + runtime

Builder packs sprites and UI elements into atlas textures, recomputes UV coordinates.
Runtime binds one texture per atlas, draws all sprites in a single instanced draw call.

- For sprites and UI only — mesh textures stay individual (unique per material)
- Builder responsibility: packing, UV rewrite, margin generation (prevent bleeding)
- Runtime: standard `texture(sampler, uv)`, no special shader logic

### 2. Compact Instance Data (mat4x3 + configurable color)

**Status:** done
**Scope:** mesh renderer, material, shaders

Instance data reduced from 80 bytes to 48-64 bytes depending on color mode.

**Implementation:**
- `nt_color_mode_t` enum: `NT_COLOR_MODE_NONE` (48B), `NT_COLOR_MODE_RGBA8` (56B), `NT_COLOR_MODE_FLOAT4` (64B)
- Color mode set per material at creation time (immutable, pipeline-affecting)
- mat4x3: 3 rows extracted from column-major mat4, shader reconstructs 4th row `(0,0,0,1)`
- CPU staging: `uint8_t` byte buffer with variable stride per draw group
- Pipeline cache key includes `color_mode` at bits 8-9
- `glVertexAttrib4f(7, 1,1,1,1)` at init — identity for shaders that multiply by color when attribute disabled
- Sponza shaders use NONE mode (no per-instance color, no v_color varying)
- Shape renderer unchanged (already compact: 44B)

**Savings:** 80 → 48 bytes (no color) = 40% reduction. 1000 instances = 32KB less upload per frame.

### 3. Radix Sort for Render Items

**Status:** done
**Scope:** `nt_render_items.c`

Replace `qsort` with LSD radix sort (8-bit, 8 passes for full uint64 sort_key).

- Full 64-bit sort_key — game controls content, engine sorts all bits
- Preallocated temp buffer (16 bytes * max_items), no heap allocation
- No function pointer calls, no branch mispredictions
- Predictable linear time — important for real-time frame budget
- Single implementation for all N (no hybrid/insertion sort fallback)

**Requires:** benchmark on real scene data before/after to validate gains.


---

## Tier 2 — Medium-term

### 5. AABB Frustum Culling

**Status:** planned
**Scope:** new module + AABB component

CPU-side frustum culling: AABB vs 6 frustum planes (Gribb-Hartmann p-vertex method).
~50 ops per object, sufficient for thousands of objects on mobile.

**Architecture:**
- AABB component stores `min[3], max[3], visible` per entity
- World-space AABBs updated from local AABB + world matrix before culling
- Two API levels:
  - **Batch:** `nt_frustum_cull(frustum, aabb_storage, count)` — iterates dense storage, writes `visible` flag into each AABB component
  - **Single:** `bool nt_frustum_test_aabb(frustum, min, max)` — returns bool, for shadow pass and special cases
- Game checks `visible` flag when building draw list
- Shadow pass uses single-test function with light frustum per object (random access OK — few shadow casters)

**Per-frame order:**
1. Update world AABBs (dense iteration)
2. Batch frustum cull (dense iteration, writes visibility)
3. Game builds draw_list (checks visibility)
4. Shadow pass: single test per shadow caster with light frustum

**Not needed:** spatial structures (octree/BVH) — flat iteration faster for < 5000 objects.
**Later optimization:** plane masking, WASM SIMD batch (`-msimd128`).

### 6. Shadow Mapping

**Status:** planned
**Scope:** new render pass, shaders

Single directional shadow map with PCF filtering.

- One additional depth-only render pass
- Depth texture (WebGL 2 `GL_DEPTH_COMPONENT` native support)
- PCF sampling in main fragment shader
- Large visual quality improvement for low rendering cost

### 7. Basis Universal (Compressed Textures)

**Status:** planned
**Scope:** builder + runtime activator

GPU texture compression via Basis Universal / KTX2 with Basis supercompression.

- Builder: source textures → .basis (or KTX2 with Basis)
- Pack: basis blob stored in asset pack
- Runtime: transcode to GPU-native format at texture activation:
  - Desktop: BC1/BC3 (S3TC via `WEBGL_compressed_texture_s3tc`)
  - iOS: ASTC/PVRTC (via `WEBGL_compressed_texture_astc`)
  - Android: ETC2 (WebGL 2 core) / ASTC
- Transcoder: ~200KB WASM (Binomial C library)

**Benefits:**
- 4-8x GPU memory reduction per texture
- 4-8x bandwidth reduction (main mobile bottleneck)
- Smaller download size

---

## Evaluated and Skipped

### Buffer Suballocation (OffsetAllocator)

**Why considered:** Sebastian Aaltonen's OffsetAllocator — single VBO/IBO for all meshes, offset-based draws.
**Why skipped:** Enables GPU-driven rendering (compute + indirect draws) which WebGL 2 doesn't have. Without `glMultiDrawElementsIndirect`, suballocation doesn't reduce draw calls. Meshes have heterogeneous vertex layouts (different stride, streams, index type) making shared buffers impractical. `glBindBuffer` is cheap on WebGL 2 — not the bottleneck.

### WEBGL_multi_draw Extension

**Why considered:** batch multiple draw calls into one API call.
**Why skipped:** instancing already merges same-material+mesh runs into single draws. Multi-draw can't batch across different VBOs without suballocation. No practical gain over current instancing architecture.

### Frame Scratch Allocator

**Why considered:** zero-alloc frame-temporary memory for render data.
**Why skipped:** already no heap allocation in hot path. Instance data preallocated at init, render items on stack in game code, shape renderer uses static arrays.

### GPU-Driven Rendering

**Why considered:** Aaltonen's core technique — compute culling + indirect draws.
**Why skipped:** requires compute shaders, SSBO, `glMultiDrawElementsIndirect` — none available in WebGL 2. This is a WebGPU/Vulkan/Metal technique.

### PBR Mobile

**Why considered:** Aaltonen's SIGGRAPH 2024 — physically-based rendering on mobile.
**Why skipped for now:** depends on game visual style. Large feature, not needed until art direction is decided.

---

## References

- [Sebastian Aaltonen — No Graphics API](https://www.sebastianaaltonen.com/blog/no-graphics-api)
- [SIGGRAPH 2023 — HypeHype Advances](https://advances.realtimerendering.com/s2023/AaltonenHypeHypeAdvances2023.pdf)
- [SIGGRAPH 2024 — PBR on Mobile](https://community.arm.com/cfs-file/__key/communityserver-blogs-components-weblogfiles/00-00-00-20-66/siggraph_5F00_mmg_5F00_2024_5F00_HypeHype.pdf)
- [REAC 2023 — Modern Mobile Rendering](https://enginearchitecture.realtimerendering.com/downloads/reac2023_modern_mobile_rendering_at_hypehype.pdf)
- [OffsetAllocator — GitHub](https://github.com/sebbbi/OffsetAllocator)
- [GPU-Driven Rendering — SIGGRAPH 2015](https://advances.realtimerendering.com/s2015/aaltonenhaar_siggraph2015_combined_final_footer_220dpi.pdf)
