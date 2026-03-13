# Requirements: Neotolis Engine

**Defined:** 2026-03-10
**Core Value:** Simple, fast, predictable engine runtime -- composable features wired by game code with zero hidden magic

## v1.2 Requirements

Requirements for v1.2 Runtime Renderer milestone. Each maps to roadmap phases.

### Frame Lifecycle

- [x] **FRAME-01**: Engine runs game callbacks (init, update, render, shutdown) in defined order
- [x] **FRAME-02**: Engine provides accumulator-based fixed update loop with configurable fixed_dt and max_fixed_steps
- [x] **FRAME-03**: Engine computes high-resolution delta time with clamp for tab-switch recovery
- [x] **FRAME-04**: Engine provides platform-agnostic frame loop with mode selection (vsync / uncapped)
- [x] **FRAME-05**: Game configures engine settings (fixed_dt, max_fixed_steps, loop mode) via code

### Platform

- [x] **PLAT-01**: Platform initializes canvas and creates WebGL 2 context
- [x] **PLAT-02**: Platform provides display info with DPR, canvas size, and framebuffer size
- [x] **PLAT-03**: Platform detects canvas resize and updates framebuffer dimensions
- [x] **PLAT-04**: Platform provides keyboard input polling (down, pressed, released per key)
- [x] **PLAT-05**: Platform provides mouse input polling (position, buttons, delta)
- [x] **PLAT-06**: Platform maps CSS coordinates to framebuffer coordinates for input
- [x] **PLAT-07**: Platform uses EM_JS for all JS bridge calls
- [x] **PLAT-08**: Platform detects WebGL context loss and pauses rendering
- [x] **PLAT-09**: Platform recovers from context loss by recreating GPU resources

### Renderer

- [x] **GFX-01**: Renderer compiles and links shaders with error reporting
- [x] **GFX-02**: Renderer creates and manages vertex/index buffers with VAOs
- [x] **GFX-03**: Renderer sets uniforms (matrices, colors) per draw call
- [x] **GFX-04**: Renderer provides frame begin/end and pass begin/end API
- [x] **GFX-05**: Renderer controls depth test, back-face culling, blend state, and viewport

### Shape Renderer

- [x] **SHAPE-01**: Shape renderer draws lines in 3D world space
- [x] **SHAPE-02**: Shape renderer draws rectangles (fill + wireframe)
- [x] **SHAPE-03**: Shape renderer draws circles with CPU tessellation (fill + wireframe)
- [x] **SHAPE-04**: Shape renderer draws triangles (fill + wireframe)
- [x] **SHAPE-05**: Shape renderer draws cubes (fill + wireframe)
- [x] **SHAPE-06**: Shape renderer draws spheres with UV tessellation (fill + wireframe)
- [x] **SHAPE-07**: Shape renderer supports per-vertex color
- [x] **SHAPE-08**: Shape renderer batches shapes into dynamic VBO with preallocated buffer

### Demo

- [x] **DEMO-01**: Demo provides camera with perspective projection and view matrix using cglm
- [x] **DEMO-02**: Demo renders spinning cube controlled by keyboard/mouse input

### Desktop Build

- [x] **DESK-01**: Desktop build vendors glad (GL 3.3 Core loader) and GLFW (windowing library)
- [x] **DESK-02**: GL backend compiles and runs on both WebGL 2 (Emscripten) and OpenGL 3.3 Core (desktop)
- [x] **DESK-03**: Desktop window creates via GLFW with GL 3.3 Core context, resize, fullscreen, and DPR
- [x] **DESK-04**: Desktop input provides keyboard and mouse polling via GLFW callbacks matching web API surface
- [x] **DESK-05**: Desktop app loop integrates vsync control and GLFW window close detection
- [x] **DESK-06**: Same game code (spinning_cube main.c) builds and runs on both web and desktop

## Future Requirements

Deferred to future milestones. Tracked but not in current roadmap.

### Memory

- **MEM-01**: Engine provides compile-time memory limits with preallocated storages
- **MEM-02**: Engine provides frame scratch memory allocator

### Components

- **COMP-01**: Engine provides transform component with position, rotation, scale, model matrix
- **COMP-02**: Engine provides material component with shader and parameter binding

### Debug

- **DBG-01**: Engine provides debug overlay with frame time, draw calls, vertex count
- **DBG-02**: Engine provides debug counters API for custom metrics

### Input

- **INPUT-01**: Platform provides touch input with unified pointer model
- **INPUT-02**: Input system provides pointer capture (input_try_capture, input_release_capture)

## Out of Scope

Explicitly excluded. Documented to prevent scope creep.

| Feature | Reason |
|---------|--------|
| ECS / component system | Next milestone -- runtime must be proven first |
| Memory policy / frame allocator | Needs real consumers (renderer, ECS) to validate |
| Text rendering | Separate complex feature (font, glyph atlas, layout) -- future milestone |
| Retained-mode shape API | Contradicts immediate-mode debug-draw philosophy |
| UBO for uniforms | Individual glUniform calls sufficient for single-shader v1.2 |
| Touch input | Keyboard + mouse sufficient for v1.2 demo |
| Shader hot-reload | Needs builder pipeline |
| sRGB / gamma-correct rendering | Needs texture pipeline |
| Multi-pass rendering (shadows) | Needs full render item system |
| WebGL 1 fallback | Spec locks WebGL 2 only, 95%+ coverage |
| Interpolation alpha | Trivial to add later when visual stutter noticed |

## Traceability

Which phases cover which requirements. Updated during roadmap creation.

| Requirement | Phase | Status |
|-------------|-------|--------|
| FRAME-01 | Phase 12 | Complete |
| FRAME-02 | Phase 12 | Complete |
| FRAME-03 | Phase 12 | Complete |
| FRAME-04 | Phase 12 | Complete |
| FRAME-05 | Phase 12 | Complete |
| PLAT-01 | Phase 13 | Complete |
| PLAT-02 | Phase 13 | Complete |
| PLAT-03 | Phase 13 | Complete |
| PLAT-04 | Phase 14 | Complete |
| PLAT-05 | Phase 14 | Complete |
| PLAT-06 | Phase 14 | Complete |
| PLAT-07 | Phase 13 | Complete |
| PLAT-08 | Phase 15 | Complete |
| PLAT-09 | Phase 15 | Complete |
| GFX-01 | Phase 15 | Complete |
| GFX-02 | Phase 15 | Complete |
| GFX-03 | Phase 15 | Complete |
| GFX-04 | Phase 15 | Complete |
| GFX-05 | Phase 15 | Complete |
| SHAPE-01 | Phase 16 | Complete |
| SHAPE-02 | Phase 16 | Complete |
| SHAPE-03 | Phase 16 | Complete |
| SHAPE-04 | Phase 16 | Complete |
| SHAPE-05 | Phase 16 | Complete |
| SHAPE-06 | Phase 16 | Complete |
| SHAPE-07 | Phase 16 | Complete |
| SHAPE-08 | Phase 16 | Complete |
| DEMO-01 | Phase 17 | Complete |
| DEMO-02 | Phase 17 | Complete |
| DESK-01 | Phase 18 | Planned |
| DESK-02 | Phase 18 | Planned |
| DESK-03 | Phase 18 | Planned |
| DESK-04 | Phase 18 | Planned |
| DESK-05 | Phase 18 | Planned |
| DESK-06 | Phase 18 | Planned |

**Coverage:**
- v1.2 requirements: 35 total
- Mapped to phases: 35
- Unmapped: 0

---
*Requirements defined: 2026-03-10*
*Last updated: 2026-03-13 after Phase 18 planning*
