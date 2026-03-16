# Roadmap: Neotolis Engine

## Milestones

- v1.0 Foundation Runtime (shipped 2026-03-09) - Phases 1-2
- v1.1 Modular Build (shipped 2026-03-10) - Phases 3-11
- v1.2 Runtime Renderer (in progress) - Phases 12-19

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

### v1.2 Runtime Renderer (In Progress)

**Milestone Goal:** First rendering runtime -- from empty main() to a spinning cube in the browser with game loop, input, and shape-based debug drawing.

- [x] **Phase 12: Frame Lifecycle** - Engine frame loop with game callbacks, fixed timestep, and delta time (completed 2026-03-10)
- [x] **Phase 13: Web Platform Layer** - Canvas initialization, DPR handling, resize detection, JS bridge (completed 2026-03-11)
- [x] **Phase 14: Input System** - Keyboard and mouse polling with coordinate mapping (completed 2026-03-12)
- [x] **Phase 15: WebGL 2 Renderer** - Shader pipeline, buffer management, draw state, context loss handling (completed 2026-03-12)
- [x] **Phase 16: Shape Renderer** - Immediate-mode shape drawing with batching and per-vertex color (completed 2026-03-12)
- [x] **Phase 17: Demo Integration** - Spinning cube with camera and input control (completed 2026-03-13)
- [x] **Phase 18: Desktop Build** - Vendor glad, GLFW native window, GL backend, native input — full desktop parity with web (completed 2026-03-13)
- [x] **Phase 19: Isolate GLFW behind nt_window API** - Consolidate GLFW into nt_window, expand public API, clean module boundaries (completed 2026-03-16)

## Phase Details

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
- [ ] 12-01-PLAN.md -- nt_time module: platform timer + accumulator API + unit tests
- [ ] 12-02-PLAN.md -- nt_app module: swappable frame loop backends + frame timing + unit tests

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
- [ ] 13-01-PLAN.md -- nt_window contract: header, shared DPR math, unit tests (TDD)
- [ ] 13-02-PLAN.md -- Platform backends: web EM_JS canvas queries, native stubs, CMake wiring, quality gates

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
- [ ] 14-01-PLAN.md -- nt_input contract: header, shared logic (edge detection, pointer management, coord mapping), stub backend, TDD unit tests
- [ ] 14-02-PLAN.md -- Platform backends: web EM_JS event listeners, native stubs, CMake platform wiring, quality gates

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
- [ ] 18-01-PLAN.md -- Vendor glad + GLFW, update API headers (window title/resizable, vsync enum), GL backend platform ifdefs + native context
- [ ] 18-02-PLAN.md -- GLFW window + app loop, GLFW input backend, example CMake wiring, desktop rendering verification

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

## Progress

**Execution Order:**
Phases execute in numeric order: 12 -> 13 -> 14 -> 15 -> 16 -> 17 -> 18 -> 19
Note: Phase 14 (Input) and Phase 15 (Renderer) both depend on Phase 13 and could execute in parallel.
Note: Phase 18 (Desktop GL) depends on Phase 15 and can execute in parallel with 16/17.
Note: Phase 19 (Window Isolation) depends on Phase 18.

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
| 19. Isolate GLFW behind nt_window API | v1.2 | 1/1 | Complete | 2026-03-16 |
