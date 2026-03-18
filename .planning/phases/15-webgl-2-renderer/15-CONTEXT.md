# Phase 15: WebGL 2 Renderer - Context

**Gathered:** 2026-03-12
**Status:** Ready for planning

<domain>
## Phase Boundary

Minimal but complete WebGL 2 rendering API — shaders, buffers, draw state, frame/pass structure, context loss handling. Module name: nt_gfx. Own renderer inspired by sokol_gfx architecture but simpler API thanks to single-backend (WebGL 2). Covers GFX-01..05, PLAT-08, PLAT-09.

</domain>

<decisions>
## Implementation Decisions

### Module: nt_gfx
- New module at `engine/graphics/` (directory already exists)
- Lightweight RHI (Render Hardware Interface) — portable API, swappable backend per platform
- WebGL 2 backend now, WebGPU/Vulkan backends as future milestones
- `g_nt_gfx` global extern struct for config and state
- Swappable backend pattern: `nt_gfx.h` (API) + `nt_gfx.c` (shared) + `webgl/nt_gfx_webgl.c` + `native/nt_gfx_native.c`

### Shader source approach
- Inline GLSL strings — game passes `const char*` vertex/fragment source
- Game writes full GLSL 300 es header (`#version 300 es`, `precision mediump float;`) — no engine auto-prepend
- No built-in shaders in nt_gfx — shape renderer (Phase 16) embeds its own as `static const char[]`
- Shader compile/link failure = fatal crash with GL error log in crash overlay (programming error, not recoverable)
- Future: builder validates shaders offline, packs into binary ShaderAsset

### Shader handles — separate stages
- nt_shader_t = one compiled shader stage (vertex OR fragment)
- Pipeline links vertex + fragment shaders at creation
- Reuse: one vertex shader in multiple pipelines without recompiling
- Maps to Vulkan VkShaderModule, WebGPU GPUShaderModule, Metal MTLFunction
- Future: compute shaders as separate stage type

### Fixed attribute location convention
- Standard locations for common attributes, shaders declare only what they use
- Locations 0-3 defined for v1.2: position(0), normal(1), color(2), texcoord0(3)
- Locations 4-15: free for shader-specific custom data (texcoord1, joints, weights, etc.)
- 16 locations guaranteed by WebGL 2 (MAX_VERTEX_ATTRIBS)
- Zero overhead — locations compiled into shader, no runtime lookup
- Exact numbering at Claude's discretion, reviewed in plan

### Pipeline abstraction
- nt_pipeline_t = shader stages + vertex layout + render state (depth, blend, cull)
- Internally creates VAO on WebGL 2, maps to VkPipeline/GPURenderPipeline on future backends
- Game never touches VAO directly — pipeline is the abstraction
- Immutable after creation (modern API pattern — GPU can optimize)
- One pipeline shared by many objects — objects differ by uniforms (color, matrix, texture)
- Material concept (pipeline + parameters) is a future milestone — in v1.2 game sets uniforms manually

### Typed opaque handles
- Distinct struct per resource type: nt_shader_t, nt_pipeline_t, nt_buffer_t
- Internally uint32 index into pool — compile-time type safety, zero runtime overhead
- Prevents passing buffer where shader expected (compile error)

### Resource pools — runtime config at init
- Pool sizes configured at init via desc struct, not compile-time #define
- One malloc per pool at initialization (not hot path)
- Consistent pattern for all modules: GPU resources now, ECS components in future milestone
- Game sets pool sizes in nt_gfx_init():
  ```c
  nt_gfx_init(&(nt_gfx_desc_t){
      .max_shaders = 32,
      .max_pipelines = 16,
      .max_buffers = 128,
  });
  ```

### Uniform API — named uniforms
- Set uniforms by name: `nt_gfx_set_uniform_mat4(shader, "u_mvp", matrix)`
- GL introspection (glGetUniformLocation) — no manual interface description needed
- Simpler than sokol's byte-block approach, sufficient for v1.2 draw call volume
- Possible optimization later: cache locations at pipeline creation

### Frame + Pass API
- begin_frame/end_frame wraps the entire frame
- begin_pass/end_pass for each render pass — ready for multi-pass (shadows, post-processing, WebGPU)
- Pass descriptor: clear color, clear depth, render target (NULL = screen)
- In v1.2: one pass, target = screen. Structure ready for future multi-pass
- Maps to WebGPU GPURenderPassEncoder, Vulkan VkRenderPass
  ```c
  nt_gfx_begin_frame();
  nt_gfx_begin_pass(&(nt_pass_desc_t){
      .clear_color = {0.1, 0.1, 0.1, 1.0},
      .clear_depth = 1.0f,
      .target = NULL,
  });
  // bind, set uniforms, draw...
  nt_gfx_end_pass();
  nt_gfx_end_frame();
  ```

### Context loss — detect + game-driven recovery
- Engine detects webglcontextlost → sets `g_nt_gfx.context_lost = true`
- All draw calls become no-op while context_lost — game loop continues (logic updates run)
- Engine detects webglcontextrestored → new GL context available
- Engine does NOT store copies of source data — zero extra memory
- Game checks context_lost flag each frame, triggers async asset reload if needed
- For v1.2 (inline shaders): recovery is trivial — strings already in program memory
- Future (asset packs): resource_id/tag system for non-blocking restoration with polling:
  - Each resource stores a tag (asset identifier)
  - Engine iterates pool, calls game-provided loader with tag
  - Loader returns data (sync) or NULL (async, not ready yet)
  - Engine polls pending resources each frame until all restored
  - No frame stutter — render skipped, async loading in background

### Context attributes (from Phase 13 decisions)
- Configurable in g_nt_gfx: depth=true, stencil=true, antialias=false, alpha=false
- Renderer creates WebGL 2 context via emscripten_webgl_create_context()
- Fatal crash with clear message on context creation failure (uses shell crash overlay)

### Claude's Discretion
- Exact attribute location numbering and enum names
- Internal pool data structures and slot layout
- GL state caching strategy (avoid redundant state changes)
- glGetUniformLocation caching approach
- nt_gfx_desc_t exact fields and default values
- Native stub implementation for testing
- CMakeLists.txt specifics (link flags, platform conditionals)
- Whether begin_frame checks context_lost automatically or game checks manually

</decisions>

<specifics>
## Specific Ideas

- "Simplified sokol" — same architecture (typed handles, pipeline, pools), simpler API due to single backend + named uniforms + GL introspection
- Think about WebGPU/Vulkan from the start: pipeline, separate shader stages, pass API
- Writing own renderer for learning + control, not using sokol as dependency
- Performance and minimum memory are priorities — no data duplication, reuse shader stages
- Runtime pool config (not #define) — consistent with future ECS component pools
- Resource ID / tag system for context loss recovery designed for async asset loading (future)
- Pass API included now even though v1.2 only needs one pass — trivial to add, prevents API change later

</specifics>

<code_context>
## Existing Code Insights

### Reusable Assets
- `nt_web.h`: EM_JS + inline no-op fallback pattern — reuse for GL context creation, context loss listeners
- `nt_platform.h`: NT_PLATFORM_WEB / NT_PLATFORM_NATIVE defines for ifdef-based platform split
- `nt_window.h` / `g_nt_window`: surface info (fb_width, fb_height, dpr) — renderer reads for viewport, context creation
- `nt_types.h`: nt_result_t for error returns, standard type includes
- `shell.html.in`: canvas with webglcontextlost handler (e.preventDefault), crash overlay for fatal errors
- `engine/graphics/` directory already exists (empty)

### Established Patterns
- Swappable backend: shared .c + per-platform implementations (nt_app, nt_window, nt_input, nt_log)
- Global extern struct: `g_nt_` prefix (g_nt_app, g_nt_window, g_nt_input → g_nt_gfx)
- File-scope statics (`s_`) for internal module state
- EM_JS over EM_ASM for JS bridge (avoids C17 variadic macro warning)
- `#ifdef NT_PLATFORM_WEB` with typedef fallback for native clang-tidy
- nt_add_module() CMake helper for new modules

### Integration Points
- nt_gfx reads g_nt_window for surface info (fb_width, fb_height for viewport)
- nt_gfx creates WebGL 2 context on canvas provided by nt_window
- Shape renderer (Phase 16) uses nt_gfx to create shaders, pipelines, buffers, draw shapes
- Demo (Phase 17) uses nt_gfx for rendering spinning cube
- Game's frame function calls nt_gfx_begin_frame/end_frame around render code
- nt_gfx links to nt_core (types, platform defines)

</code_context>

<deferred>
## Deferred Ideas

Migrated to GitHub issues — see label `deferred`.

</deferred>

---

*Phase: 15-webgl-2-renderer*
*Context gathered: 2026-03-12*
