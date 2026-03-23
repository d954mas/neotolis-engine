# Phase 33: Compact Instance Data - Context

**Gathered:** 2026-03-23
**Status:** Ready for planning

<domain>
## Phase Boundary

Instance buffer bandwidth reduced 20-40% via mat4x3 transforms and configurable color precision. Mesh renderer instance layout changes from 80 bytes (mat4 + float4 color) to 48-64 bytes depending on color mode. Shape renderer is NOT in scope -- already compact (mat3x3 + RGBA8).

</domain>

<decisions>
## Implementation Decisions

### Color mode API
- **D-01:** New enum `nt_color_mode_t` in `nt_render_defs.h`: `NT_COLOR_MODE_NONE` (0), `NT_COLOR_MODE_RGBA8`, `NT_COLOR_MODE_FLOAT4`
- **D-02:** Color mode is a field in `nt_material_create_desc_t`, set at creation time -- **immutable**, consistent with blend_mode/depth_test/depth_write/cull_mode (all pipeline-affecting properties are immutable)
- **D-03:** Default color mode is `NT_COLOR_MODE_NONE` (48 bytes). Explicit opt-in for color via RGBA8/FLOAT4. Rationale: code-first philosophy, most materials (Sponza) don't use per-instance color
- **D-04:** Separate enum (not reusing `nt_vertex_format_t`) to prevent invalid values -- only 3 valid modes for color, `nt_vertex_format_t` has 15 entries most of which are nonsensical for color

### Color storage
- **D-05:** Color stays in `nt_drawable_comp` (`float[4]` per entity). Renderer reads it only when `material.color_mode != NT_COLOR_MODE_NONE`. Avoids extra component lookup in hot path -- drawable comp is already accessed for visibility check
- **D-06:** Mismatch (entity has drawable color but material is NONE): renderer silently ignores color. No warning, no error. Drawable stores color on CPU (cheap), GPU sees it only when material opts in

### Shader strategy
- **D-07:** One shader works for all color modes. Shader declares `layout(location=7) in vec4 i_color` if it uses per-instance color. Pipeline system enables/disables the attribute based on instance layout
- **D-08:** `glVertexAttrib4f(7, 1.0, 1.0, 1.0, 1.0)` at init -- sets generic attribute to white. When color attribute is disabled (NONE mode), shader reads (1,1,1,1) = identity for multiplication. Prevents black-screen bug
- **D-09:** Shaders that don't use per-instance color (e.g., Sponza) simply don't declare `i_color`. Attribute enabled on GL side but shader ignores it -- valid in OpenGL/WebGL

### Pipeline cache
- **D-10:** Color mode encoded in `state_bits` (bits 8-9, 2 bits for 3 values) in pipeline cache key. Follows existing pattern: `state_bits = blend | depth_test<<4 | depth_write<<5 | cull<<6 | color_mode<<8`
- **D-11:** Different color_mode = different instance layout = different pipeline. Pipeline cache creates lazily on first use, reuses on subsequent frames
- **D-12:** Forward-compatible with Vulkan/WebGPU: immutable pipeline-affecting properties mean backends can pre-compile pipelines without runtime stalls

### Instance data layout
- **D-13:** mat4 -> mat4x3: transmit first 3 columns of mat4 as 3 x vec4 (48 bytes). Vertex shader reconstructs 4th column as `vec4(0, 0, 0, 1)`. Column-major (cglm convention), CPU copies first 48 bytes of mat4
- **D-14:** Instance sizes: NONE = 48B (mat4x3 only), RGBA8 = 52B (mat4x3 + uint8[4]), FLOAT4 = 64B (mat4x3 + float[4]). Verified by static assert
- **D-15:** Attribute locations shift: transform at locations 4-6 (was 4-7), color at location 7 (was 8)

### Instance buffer
- **D-16:** CPU staging = `uint8_t[]` byte buffer, not typed struct. Renderer packs at correct stride per draw group. Enables real bandwidth savings (48B upload for NONE vs 64B for FLOAT4)
- **D-17:** Buffer sized as `max_instances * 64` (worst-case stride). User specifies `max_instances` in desc (unchanged API). Runtime capacity = buffer_bytes / actual_stride -- NONE mode fits more instances in same memory
- **D-18:** `nt_mesh_instance_t` struct removed or kept only for documentation. Instance data is packed manually via memcpy into byte buffer

### Shape renderer
- **D-19:** Not in scope. Shape renderer already compact: `nt_shape_instance_t` = 44B (mat3x3 + RGBA8), `nt_shape_line_instance_t` = 28B. No changes needed

### Claude's Discretion
- Instance layout selection logic in renderer (switch on color_mode -> layout table)
- `pack_rgba8` helper implementation details
- Pipeline desc construction with variable instance layout
- Test structure and coverage approach
- Whether to keep `nt_mesh_instance_t` as documentation-only type or remove entirely
- Exact location of `glVertexAttrib4f` call (init vs first pipeline bind)

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Instance data (to be changed)
- `engine/render/nt_render_defs.h` -- `nt_mesh_instance_t` (80B struct), `nt_color_mode_t` enum (new), static asserts
- `engine/renderers/nt_mesh_renderer.c` lines 41-54 -- `s_instance_layout` (5 attrs, stride=80), attribute locations 4-8

### Mesh renderer (main changes)
- `engine/renderers/nt_mesh_renderer.c` lines 24-39 -- module state: `instance_buf`, `instance_data`, `max_instances`
- `engine/renderers/nt_mesh_renderer.c` lines 104-112 -- `find_or_create_pipeline()` key construction with `state_bits`
- `engine/renderers/nt_mesh_renderer.c` lines 290-390 -- `draw_list()` instance fill + upload + draw loop

### Material system (color_mode field)
- `engine/material/nt_material.h` lines 70-76 -- `nt_material_create_desc_t` (add `color_mode` field)
- `engine/material/nt_material.h` lines 109-115 -- `nt_material_info_t` (add `color_mode` field for renderer access)

### GFX backend (glVertexAttrib4f)
- `engine/graphics/nt_gfx.h` -- vertex layout, buffer, pipeline API
- `engine/graphics/nt_gfx_internal.h` -- backend function signatures

### Shaders (location update)
- `assets/shaders/sponza.vert` -- instance attribute declarations (locations 4-7 -> 4-6)
- `assets/shaders/sponza_alpha.vert` -- same location update

### Requirements
- `.planning/REQUIREMENTS.md` -- REND-01, REND-02, REND-03

### Spec
- `docs/neotolis_engine_spec_1.md` -- Authoritative architecture reference

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `pack_color()` in shape renderer (`nt_shape_renderer.c:378`) -- float[4] to uint8[4] packing, can be reused for RGBA8 mode
- Pipeline cache in mesh renderer -- already handles lazy creation, just needs color_mode in key
- `nt_vertex_layout_t` struct -- supports variable attr_count and stride, ready for 3-4 attribute layouts

### Established Patterns
- Compile-time limits via `#ifndef`/`#define` with `#ifndef` guard
- `_Static_assert` for binary layout validation
- Immutable pipeline-affecting material properties (blend, depth, cull)
- Stream buffer (`NT_USAGE_STREAM`) with full rewrite each frame
- `do { } while(0)` macro wrapping, `NT_ASSERT` for programmer errors

### Integration Points
- `nt_material_create_desc_t` needs `color_mode` field + defaults function update
- `nt_material_info_t` needs `color_mode` field for renderer access
- `nt_mesh_renderer.c` instance fill loop: switch from struct access to byte buffer + stride packing
- All vertex shaders using instance attributes: location 4-7(mat4) -> 4-6(mat4x3), location 8(color) -> 7
- Examples (Sponza, textured_quad): update instance-related code for new API
- Pipeline cache key: add color_mode to state_bits

</code_context>

<specifics>
## Specific Ideas

- Sponza demo uses NONE by default (no per-instance color, color from textures) -- should work without changes to material creation if default is NONE
- `glVertexAttrib4f(7, 1, 1, 1, 1)` set once at mesh renderer init -- prevents black screen when shader multiplies by instance color but material is NONE
- Buffer capacity bonus: with NONE (48B stride) vs FLOAT4 (64B stride), same memory fits 33% more instances
- Immutable color_mode is forward-compatible: Vulkan/WebGPU pipelines are immutable GPU objects, changing pipeline state at runtime causes expensive recompilation

</specifics>

<deferred>
## Deferred Ideas

- Mutable pipeline-affecting material properties (blend, depth, cull, color_mode) -- architecture supports it via stream buffer + lazy pipeline cache, but inconsistent with current immutable design and problematic for Vulkan/WebGPU (pipeline compilation stalls)
- Render state separated from material into dedicated pipeline state object -- relevant when adding Vulkan/WebGPU backend
- HALF4 color mode (8 bytes, HDR color) -- add `NT_COLOR_MODE_HALF4` to enum when needed, trivial extension
- Shape renderer RGBA4 color (2 bytes) -- GL vertex attributes minimum is byte-sized, would require manual packing/unpacking in shader for negligible savings on debug geometry

</deferred>

---

*Phase: 33-compact-instance-data*
*Context gathered: 2026-03-23*
