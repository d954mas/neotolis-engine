# Roadmap: Neotolis Engine

## Milestones

- v1.0 Foundation Runtime (shipped 2026-03-09) - Phases 1-2
- v1.1 Modular Build (shipped 2026-03-10) - Phases 3-11
- v1.2 Runtime Renderer (in progress) - Phases 12-17

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
- [ ] **Phase 13: Web Platform Layer** - Canvas initialization, DPR handling, resize detection, JS bridge
- [ ] **Phase 14: Input System** - Keyboard and mouse polling with coordinate mapping
- [ ] **Phase 15: WebGL 2 Renderer** - Shader pipeline, buffer management, draw state, context loss handling
- [ ] **Phase 16: Shape Renderer** - Immediate-mode shape drawing with batching and per-vertex color
- [ ] **Phase 17: Demo Integration** - Spinning cube with camera and input control

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
**Plans**: TBD

Plans:
- [ ] 14-01: TBD

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
**Plans**: TBD

Plans:
- [ ] 15-01: TBD
- [ ] 15-02: TBD

### Phase 16: Shape Renderer
**Goal**: Game code can draw debug shapes (lines, rectangles, circles, triangles, cubes, spheres) in immediate mode with fill, wireframe, and per-vertex color
**Depends on**: Phase 15
**Requirements**: SHAPE-01, SHAPE-02, SHAPE-03, SHAPE-04, SHAPE-05, SHAPE-06, SHAPE-07, SHAPE-08
**Success Criteria** (what must be TRUE):
  1. All six shape types (line, rect, circle, triangle, cube, sphere) render correctly in both fill and wireframe modes
  2. Per-vertex color is supported and visually distinguishable across different vertices of the same shape
  3. Shapes are batched into a single dynamic VBO per frame using a preallocated CPU buffer with no heap allocation in the draw path
  4. Wireframe rendering uses triangle-based geometry (not GL_LINES) since glLineWidth is a no-op in WebGL
**Plans**: TBD

Plans:
- [ ] 16-01: TBD
- [ ] 16-02: TBD

### Phase 17: Demo Integration
**Goal**: A spinning cube demo in the browser proves all v1.2 subsystems work together end-to-end
**Depends on**: Phase 14, Phase 16
**Requirements**: DEMO-01, DEMO-02
**Success Criteria** (what must be TRUE):
  1. Demo renders a spinning cube with perspective projection using cglm, visible in the browser at the promo site
  2. Keyboard and mouse input controls the cube (rotation speed, camera, or wireframe toggle) with responsive feedback
  3. All v1.2 subsystems (nt_app, nt_platform, nt_input, nt_gfx, nt_shape) are integrated and the demo runs without errors on both WASM presets
**Plans**: TBD

Plans:
- [ ] 17-01: TBD

## Progress

**Execution Order:**
Phases execute in numeric order: 12 -> 13 -> 14 -> 15 -> 16 -> 17
Note: Phase 14 (Input) and Phase 15 (Renderer) both depend on Phase 13 and could execute in parallel.

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
| 12. Frame Lifecycle | 2/2 | Complete    | 2026-03-10 | - |
| 13. Web Platform Layer | 1/2 | In Progress|  | - |
| 14. Input System | v1.2 | 0/? | Not started | - |
| 15. WebGL 2 Renderer | v1.2 | 0/? | Not started | - |
| 16. Shape Renderer | v1.2 | 0/? | Not started | - |
| 17. Demo Integration | v1.2 | 0/? | Not started | - |
