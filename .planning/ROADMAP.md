# Roadmap: Neotolis Engine

## Milestones

- v1.0 Foundation Runtime (shipped 2026-03-09) - Phases 1-2
- v1.1 Modular Build (shipped 2026-03-10) - Phases 3-11
- v1.2 Runtime Renderer (shipped 2026-03-16) - Phases 12-19
- v1.3 Asset Pipeline (in progress) - Phases 20-28

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

### v1.3 Asset Pipeline (In Progress)

**Milestone Goal:** Full asset pipeline from source files to rendered model -- builder packs .glb/.png/.glsl into NEOPAK, runtime loads packs, entity/component system renders textured 3D model with custom shaders.

- [ ] **Phase 20: Shared Format Headers** - Binary format definitions shared between builder and runtime, CRC32 utility
- [ ] **Phase 21: Texture Support** - nt_gfx texture pool with create/bind/destroy following existing slot/generation pattern
- [ ] **Phase 22: Entity System** - Entity handles with generations, sparse+dense component storage, transform + render components
- [ ] **Phase 23: Builder** - Native C binary reads .glb/.png/.glsl source assets and writes NEOPAK packs
- [ ] **Phase 24: Asset Registry** - nt_resource module with NEOPAK parser, asset metadata, typed handles, pack stacking
- [ ] **Phase 25: Asset Loading** - Async pack loading via JS fetch bridge, synchronous native loading, per-frame asset activation
- [ ] **Phase 26: Material System** - Runtime material objects combining shader, textures, params, and render state
- [ ] **Phase 27: Mesh Rendering Pipeline** - Entity iteration with component matching, material binding, indexed mesh drawing
- [ ] **Phase 28: Demo Integration** - Textured 3D model from asset pack with custom shaders and camera control

## Phase Details

<details>
<summary>v1.2 Phase Details (Phases 12-19) - SHIPPED</summary>

### Phase 12: Frame Lifecycle
**Goal**: Game code runs inside an engine-owned frame loop with predictable timing and defined callback order
**Depends on**: Nothing (first phase of v1.2; builds on v1.1 module infrastructure)
**Requirements**: FRAME-01, FRAME-02, FRAME-03, FRAME-04, FRAME-05
**Success Criteria** (what must be TRUE):
  1. Game provides init/update/render/shutdown callbacks and engine calls them in the correct defined order every frame
  2. Fixed update loop runs at a configurable fixed_dt with accumulator, clamped to max_fixed_steps to prevent spiral-of-death
  3. Delta time is computed with high resolution and clamped on tab-switch recovery so the game never sees a multi-second dt
  4. Frame loop runs on both WASM (via emscripten_request_animation_frame_loop) and native (via stub platform) with the same game code
  5. Game configures engine settings (fixed_dt, max_fixed_steps, loop mode) through code before the loop starts
**Plans**: 2 plans

Plans:
- [x] 12-01-PLAN.md -- nt_time module: platform timer + accumulator API + unit tests
- [x] 12-02-PLAN.md -- nt_app module: swappable frame loop backends + frame timing + unit tests

### Phase 13: Web Platform Layer
**Goal**: Engine provides canvas management, display metrics, and platform abstraction so downstream modules never call browser APIs directly
**Depends on**: Phase 12
**Requirements**: PLAT-01, PLAT-02, PLAT-03, PLAT-07
**Success Criteria** (what must be TRUE):
  1. Platform initializes a canvas element and creates a WebGL 2 context that downstream modules can use
  2. Display info reports correct DPR, CSS canvas size, and framebuffer size, and all three stay consistent after resize
  3. Canvas resize is detected automatically and framebuffer dimensions update without game code intervention
  4. All JavaScript interop uses EM_JS exclusively -- no EM_ASM, no direct JS calls from engine modules
**Plans**: 2 plans

Plans:
- [x] 13-01-PLAN.md -- nt_window contract: header, shared DPR math, unit tests (TDD)
- [x] 13-02-PLAN.md -- Platform backends: web EM_JS canvas queries, native stubs, CMake wiring, quality gates

### Phase 14: Input System
**Goal**: Game code can poll keyboard and mouse state each frame with correct coordinate mapping
**Depends on**: Phase 13
**Requirements**: PLAT-04, PLAT-05, PLAT-06
**Success Criteria** (what must be TRUE):
  1. Game can poll any keyboard key for down/pressed/released state and edge detection works correctly (pressed fires once per keydown)
  2. Game can poll mouse position, button state, and per-frame delta
  3. Mouse coordinates are mapped from CSS space to framebuffer space accounting for DPR so positions match rendered pixels
**Plans**: 2 plans

Plans:
- [x] 14-01-PLAN.md -- nt_input contract: header, shared logic (edge detection, pointer management, coord mapping), stub backend, TDD unit tests
- [x] 14-02-PLAN.md -- Platform backends: web EM_JS event listeners, native stubs, CMake platform wiring, quality gates

### Phase 15: WebGL 2 Renderer
**Goal**: Engine provides a minimal but complete WebGL 2 rendering API -- shaders, buffers, state, and frame/pass structure
**Depends on**: Phase 13
**Requirements**: GFX-01, GFX-02, GFX-03, GFX-04, GFX-05, PLAT-08, PLAT-09
**Success Criteria** (what must be TRUE):
  1. Shaders compile and link with error messages reported on failure, including GLSL 300 es precision handling
  2. Vertex and index buffers can be created and bound through VAOs, and draw calls render geometry to screen
  3. Per-draw-call uniforms (matrices, colors) are set correctly and affect rendering output
  4. Frame begin/end and pass begin/end provide clean state boundaries with depth test, culling, blend, and viewport control
  5. WebGL context loss is detected and rendering pauses gracefully; context restoration recreates GPU resources
**Plans**: 2 plans

Plans:
- [x] 15-01-PLAN.md -- nt_gfx module foundation: API contracts, pool infrastructure with generation-counted handles, stub/native backends, CMake wiring, unit tests
- [x] 15-02-PLAN.md -- Unified GL backend: context creation, shader compile/link, buffer/pipeline/VAO management, uniforms, draw calls, frame/pass lifecycle, context loss handling

### Phase 16: Shape Renderer
**Goal**: Game code can draw debug shapes (lines, rectangles, circles, triangles, cubes, spheres) in immediate mode with fill, wireframe, and per-vertex color
**Depends on**: Phase 15
**Requirements**: SHAPE-01, SHAPE-02, SHAPE-03, SHAPE-04, SHAPE-05, SHAPE-06, SHAPE-07, SHAPE-08
**Success Criteria** (what must be TRUE):
  1. All six shape types (line, rect, circle, triangle, cube, sphere) render correctly in both fill and wireframe modes
  2. Per-vertex color is supported and visually distinguishable across different vertices of the same shape
  3. Shapes are batched into a single dynamic VBO per frame using a preallocated CPU buffer with no heap allocation in the draw path
  4. Wireframe rendering uses triangle-based geometry (not GL_LINES) since glLineWidth is a no-op in WebGL
**Plans**: 2 plans

Plans:
- [x] 16-01-PLAN.md -- nt_shape module foundation: polygon_offset extension to nt_gfx, shape module skeleton (init/shutdown/flush/setters/batch), line/rect/triangle shapes with fill + wireframe + per-vertex color, unit tests
- [x] 16-02-PLAN.md -- Complex shapes: circle/cube/sphere tessellation (fill + wireframe + rotation variants), cylinder/capsule shapes, mesh draw, remaining unit tests

### Phase 17: Demo Integration
**Goal**: A spinning cube demo in the browser proves all v1.2 subsystems work together end-to-end
**Depends on**: Phase 14, Phase 16
**Requirements**: DEMO-01, DEMO-02
**Success Criteria** (what must be TRUE):
  1. Demo renders a spinning cube with perspective projection using cglm, visible in the browser at the promo site
  2. Keyboard and mouse input controls the cube (rotation speed, camera, or wireframe toggle) with responsive feedback
  3. All v1.2 subsystems (nt_app, nt_platform, nt_input, nt_gfx, nt_shape) are integrated and the demo runs without errors on both WASM presets
**Plans**: 1 plan

Plans:
- [x] 17-01-PLAN.md -- Build infrastructure, full demo implementation (trackball rotation, shape cycling, render modes, zoom), and visual verification

### Phase 18: Desktop Build
**Goal:** Make desktop build fully functional: vendor glad, integrate GLFW for native window with real GL context, enable nt_gfx GL backend on desktop, implement native input backend (keyboard + mouse via GLFW). Both web and desktop builds should work with the same game code. Spinning cube demo runs on both platforms.
**Depends on:** Phase 15
**Requirements**: DESK-01, DESK-02, DESK-03, DESK-04, DESK-05, DESK-06
**Success Criteria** (what must be TRUE):
  1. glad (GL 3.3 Core loader) and GLFW (windowing library) are vendored in deps/ and build as CMake targets
  2. GL backend compiles and runs on both WebGL 2 (Emscripten) and OpenGL 3.3 Core (desktop) with platform-conditional headers and shader version prepend
  3. Desktop window creates via GLFW with GL 3.3 Core context, supports resize, fullscreen, and DPR scaling
  4. Desktop input provides keyboard and mouse polling via GLFW callbacks matching the web API surface
  5. Desktop app loop integrates vsync control via glfwSwapInterval and window-close detection via glfwWindowShouldClose
  6. Spinning cube demo builds and runs on both web and desktop from the same main.c without any game-side ifdefs
**Plans**: 2 plans

Plans:
- [x] 18-01-PLAN.md -- Vendor glad + GLFW, update API headers (window title/resizable, vsync enum), GL backend platform ifdefs + native context
- [x] 18-02-PLAN.md -- GLFW window + app loop, GLFW input backend, example CMake wiring, desktop rendering verification

### Phase 19: Isolate GLFW behind nt_window API
**Goal:** Consolidate all GLFW usage into nt_window_native.c (plus single glfwGetProcAddress in nt_gfx_gl_ctx_native.c). Expand nt_window public API with swap_buffers, set_vsync, should_close, request_close. nt_app and nt_input become GLFW-free with clean module boundaries.
**Depends on:** Phase 18
**Requirements**: WINISO-01, WINISO-02, WINISO-03, WINISO-04, WINISO-05
**Success Criteria** (what must be TRUE):
  1. GLFW usage confined to exactly 2 engine .c files: nt_window_native.c and nt_gfx_gl_ctx_native.c
  2. nt_window provides swap_buffers, set_vsync, should_close, request_close as public API with native/web/stub implementations
  3. nt_window_poll() calls nt_input_poll() internally -- one call per frame handles both
  4. Game explicitly calls nt_window_swap_buffers() at end of frame -- nt_app does not auto-swap
  5. All existing tests pass, all examples build and run on both platforms
**Plans**: 1 plan

Plans:
- [x] 19-01-PLAN.md -- Move GLFW callbacks/functions to nt_window, add 4 new API functions, update all backends + CMake + examples + tests

</details>

### Phase 20: Shared Format Headers
**Goal**: Builder and runtime share bit-identical binary format definitions so NEOPAK packs written on any native platform are readable by the WASM runtime
**Depends on**: Nothing (first phase of v1.3; builds on v1.2 renderer infrastructure)
**Requirements**: PACK-01, PACK-02, PACK-03, PACK-04, MESH-01, MESH-03
**Success Criteria** (what must be TRUE):
  1. PackHeader struct with magic, version, asset_count, header_size, total_size, CRC32 is defined and _Static_assert verifies its size is identical across native and WASM compilers
  2. AssetEntry struct with resource_id, asset_type, format_version, offset, size is defined and size-verified
  3. MeshAssetHeader, TextureAssetHeader, ShaderAssetHeader structs are defined with documented field layouts
  4. CRC32 computation produces identical results on native builder and WASM runtime for the same input bytes
  5. Asset data alignment to 4-byte boundaries is enforced by pack layout constants
**Plans**: 2 plans

Plans:
- [ ] 20-01-PLAN.md -- Pack format core: PackHeader + AssetEntry structs, CRC32 utility, alignment constants, unit tests
- [ ] 20-02-PLAN.md -- Asset format headers: MeshAssetHeader + TextureAssetHeader + ShaderAssetHeader, attribute mask, unit tests

### Phase 21: Texture Support
**Goal**: nt_gfx provides GPU texture lifecycle (create from RGBA8 data, bind to sampler slot, destroy) following the existing slot/generation pool pattern
**Depends on**: Phase 20 (texture format header)
**Requirements**: TEX-01, TEX-02, TEX-03, TEX-04, TEX-05
**Success Criteria** (what must be TRUE):
  1. nt_gfx_make_texture() creates a GPU texture from raw RGBA8 pixel data with configurable filter and wrap modes
  2. nt_gfx_bind_texture() binds a texture handle to a numbered sampler slot for use in rendering
  3. nt_gfx_destroy_texture() releases the GPU texture and returns the pool slot for reuse
  4. Texture pool uses the same slot/generation pattern as existing shader, pipeline, and buffer pools
  5. glGenerateMipmap() is called when requested in the texture descriptor, after all parameters are set
**Plans**: 2 plans

Plans:
- [ ] 21-01-PLAN.md -- Texture pool API: handle type, enums, descriptor, make/bind/destroy, stub backend, 14+ unit tests
- [ ] 21-02-PLAN.md -- GL backend texture implementation, textured quad demo with checkerboard pixel data, visual verification

### Phase 22: Entity System
**Goal**: Game code can create entities with generational handles and attach typed components (Transform, Mesh, Material, RenderState) using cache-friendly sparse+dense storage
**Depends on**: Nothing (independent of Phases 20-21; depends only on nt_core from v1.2)
**Requirements**: ENT-01, ENT-02, ENT-03, ENT-04, COMP-01, COMP-02, COMP-03, COMP-04, XFORM-01, XFORM-02, XFORM-03, RCOMP-01, RCOMP-02, RCOMP-03
**Success Criteria** (what must be TRUE):
  1. Entity handles use uint16 index + uint16 generation, and is_alive correctly detects stale handles after destroy
  2. Entity pool has compile-time MAX_ENTITIES limit with create/destroy/is_alive API and per-entity alive+enabled flags
  3. Each component type has its own sparse+dense storage with typed add/get/has/remove API (no generic void*)
  4. Swap-and-pop removal keeps dense arrays packed, and sparse arrays are initialized to INVALID_INDEX
  5. TransformComponent stores local pos/rot/scale + world matrix + dirty flag, and transform_update() recomputes world matrices for dirty transforms only
**Plans**: 3 plans

Plans:
- [x] 22-01-PLAN.md -- Entity pool core: generational handles, free queue stack, storage registration, NT_ASSERT_ALWAYS, unit tests
- [ ] 22-02-PLAN.md -- Transform component: sparse+dense storage, typed API, swap-and-pop, TRS world matrix, dirty flag, unit tests
- [ ] 22-03-PLAN.md -- Render components: MeshComponent, MaterialComponent, RenderStateComponent with typed APIs, unit tests

### Phase 23: Builder
**Goal**: A native C binary reads .glb mesh files, .png textures, and .glsl shader source, validates inputs, and writes a NEOPAK binary pack that the runtime can load
**Depends on**: Phase 20 (shared format headers)
**Requirements**: BUILD-01, BUILD-02, BUILD-03, BUILD-04, BUILD-05, BUILD-06, BUILD-07
**Success Criteria** (what must be TRUE):
  1. Builder reads .glb files via cgltf and outputs runtime mesh binary with correct vertex attributes (POSITION required, NORMAL/UV/COLOR optional)
  2. Builder reads .png files via stb_image (forced 4-channel RGBA8) and outputs raw pixel data
  3. Builder reads .glsl vertex/fragment shader source files as text blobs
  4. Builder writes a valid NEOPAK pack with PackHeader, AssetEntry array, and 4-byte aligned data blobs that passes CRC32 verification
  5. Builder exposes a code-first API (start_pack/add_mesh/add_texture/add_shader/finish_pack) and supports glob patterns for batch addition
**Plans**: TBD

### Phase 24: Asset Registry
**Goal**: Runtime can parse NEOPAK packs, register asset metadata, and provide typed handle-based access to assets with pack stacking and priority override
**Depends on**: Phase 20 (shared format headers), Phase 21 (texture handles for typed refs)
**Requirements**: REG-01, REG-02, REG-03, REG-04, REG-05, REG-06, LOAD-04
**Success Criteria** (what must be TRUE):
  1. NEOPAK parser validates magic, version, sizes, and CRC32 before registering any assets from a pack
  2. Asset registry tracks resources by ResourceId with metadata (type, pack_index, offset, size, state) and state transitions REGISTERED -> LOADING -> READY / FAILED
  3. Pack stacking with priority works: higher pack_index overrides lower for the same ResourceId
  4. Game can mount and unmount packs and change priority order at runtime
  5. Runtime-created resources can be registered in the registry by name (two-level system: GFX handles direct + optional registry layer)
**Plans**: TBD

### Phase 25: Asset Loading
**Goal**: Packs load asynchronously on WASM via JS fetch bridge (and synchronously on native), and resource_step() activates loaded assets into GPU resources each frame
**Depends on**: Phase 24 (asset registry), Phase 21 (texture GPU upload), Phase 23 (builder produces test packs)
**Requirements**: LOAD-01, LOAD-02, LOAD-03, LOAD-05, SHDR-01, SHDR-02, MESH-02
**Success Criteria** (what must be TRUE):
  1. Pack loading on WASM uses JS fetch bridge (platform_request_fetch / on_fetch_complete) with state machine NONE -> REQUESTED -> LOADED -> READY / FAILED
  2. Pack loading on native uses synchronous fread() with the same state transitions
  3. resource_step() processes loaded packs each frame: shader assets create nt_gfx shader + pipeline, texture assets upload to GPU, mesh assets create VBO + IBO
  4. Shader asset activation creates a pipeline with vertex layout derived from the mesh format's attribute mask
  5. Pack blob persists in memory for the lifetime of the pack (zero-copy asset data access)
**Plans**: TBD

### Phase 26: Material System
**Goal**: Game code creates material objects at runtime from loaded shaders and textures, combining them with render state for use in the rendering pipeline
**Depends on**: Phase 25 (loaded shader and texture assets)
**Requirements**: MAT-01, MAT-02, MAT-03, MAT-04
**Success Criteria** (what must be TRUE):
  1. Material is a runtime object containing shader handle, texture handle array, vec4 param array, and render state
  2. Game creates materials in code by referencing loaded shader and texture assets
  3. Material render state includes blend mode, depth test/write, and cull mode
  4. All numeric material parameters are stored as vec4 array (per spec section 16.2)
**Plans**: TBD

### Phase 27: Mesh Rendering Pipeline
**Goal**: Engine iterates entities with the right component combination and draws indexed mesh geometry with correct material binding and per-entity uniforms
**Depends on**: Phase 22 (entity/component system), Phase 25 (loaded mesh/shader/texture assets), Phase 26 (material system)
**Requirements**: REND-01, REND-02, REND-03
**Success Criteria** (what must be TRUE):
  1. Render loop iterates all entities that have Transform + Mesh + Material + RenderState components, skipping entities where visible is false
  2. For each renderable entity: material pipeline is bound, material textures are bound to sampler slots, and uniforms (MVP matrix, color, params) are set
  3. Indexed mesh geometry draws via nt_gfx_draw_indexed() with correct vertex buffer and index buffer from the mesh asset
**Plans**: TBD

### Phase 28: Demo Integration
**Goal**: A working demo loads a textured 3D model from a NEOPAK asset pack and renders it with custom shaders and interactive camera control, proving the full v1.3 pipeline end-to-end
**Depends on**: Phase 27 (mesh rendering pipeline)
**Requirements**: DEMO-01, DEMO-02, DEMO-03, DEMO-04, DEMO-05
**Success Criteria** (what must be TRUE):
  1. Demo loads a .neopak pack containing at least one mesh, one texture, and vertex/fragment shaders
  2. Demo creates a material at runtime from the loaded shader and texture
  3. Demo creates an entity with Transform + Mesh + Material + RenderState components and renders a textured 3D model
  4. Demo includes interactive camera control (reusing/adapting the trackball from v1.2)
  5. Demo runs on both WASM and desktop from the same game code
**Plans**: TBD

## Progress

**Execution Order:**
Phases 20-28 execute in dependency order: 20 -> 21 -> 22/23 (parallel) -> 24 -> 25 -> 26 -> 27 -> 28
Note: Phase 22 (Entity System) and Phase 23 (Builder) have no mutual dependencies and can execute in parallel.
Note: Phase 24 (Asset Registry) depends on Phase 20 and Phase 21.
Note: Phase 25 (Asset Loading) depends on Phase 24, Phase 21, and Phase 23.

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
| 20. Shared Format Headers | 2/2 | Complete   | 2026-03-16 | - |
| 21. Texture Support | 2/2 | Complete    | 2026-03-16 | - |
| 22. Entity System | 3/3 | Complete   | 2026-03-16 | - |
| 23. Builder | v1.3 | 0/TBD | Not started | - |
| 24. Asset Registry | v1.3 | 0/TBD | Not started | - |
| 25. Asset Loading | v1.3 | 0/TBD | Not started | - |
| 26. Material System | v1.3 | 0/TBD | Not started | - |
| 27. Mesh Rendering Pipeline | v1.3 | 0/TBD | Not started | - |
| 28. Demo Integration | v1.3 | 0/TBD | Not started | - |

### Phase 29: nt_hash module — unified CRC32 + string hash, resource label hashing

**Goal:** [To be planned]
**Requirements**: TBD
**Depends on:** Phase 28
**Plans:** 3/3 plans complete

Plans:
- [ ] TBD (run /gsd:plan-phase 29 to break down)
