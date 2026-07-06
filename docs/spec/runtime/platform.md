# Platform Architecture

Web/WASM + WebGL 2 is the sole initial target; platform is a subsystem, not a
component. The platform layer owns startup/shutdown hooks, canvas/window
integration, timing, input event forwarding, JS interop bridges, and
DPR/resize/viewport handling. Other platform backends can be added later behind
the same abstractions.

Related: [Frame Lifecycle](frame-lifecycle.md), [Module Layout](../core/module-layout.md), [Input](../io/input.md)

## Initial platform target

```
WEB / WASM + WebGL 2
```

Meaning:

- application runs in browser
- engine compiled to WASM
- rendering through WebGL 2 backend
- debugging via browser console and overlays

WebGL 2 is the sole baseline. WebGL 1 is not supported. Rationale: WebGL 2 coverage is 95%+ of devices; the remaining 5% cannot run the target content (large 3D worlds) regardless; WebGL 2 gives native instancing, UBO, NPOT textures, gl_VertexID without extension management overhead; cleaner migration path to future WebGPU.

Windows / desktop platform is not required in v0.1 but architecture must allow adding `platform_win32` or other platform backends later. All platform-specific code (window creation, input, audio, etc.) works through engine abstractions.

Platform backends use the module-composition model (interface / impl / stub); see [Module Layout](../core/module-layout.md).

## Platform layer

Platform is a **subsystem/module**, not an ECS component.

```text
platform/
    platform.h
    platform_web.c
    platform_web.h
```

Future optional additions:

```text
platform_win32.c
platform_linux.c
```

## Platform responsibilities

Platform module handles:

- application startup/shutdown hooks
- canvas/window integration
- timing and frame delta
- input event forwarding
- browser-specific bridges (JS interop)
- file/network helpers (async fetch)
- frame scheduling hook
- canvas resize / device pixel ratio handling
- orientation change handling

Platform does **not** handle:

- gameplay
- scene logic
- render passes
- material logic
- resource manifests

## Canvas, DPR, and Viewport

Platform layer must handle:

- **Device pixel ratio (DPR):** mobile devices may have DPR 2-3x. Rendering at native resolution on high-DPR is often prohibitive. Platform should expose current DPR and allow render resolution scaling.
- **Canvas resize:** window/orientation changes must update framebuffer size. Platform detects resize events and notifies engine.
- **Input coordinate mapping:** pointer positions arrive in CSS pixels, must be mapped to canvas/framebuffer coordinates.

```c
typedef struct PlatformDisplayInfo {
    uint32_t canvas_width;      // CSS pixels
    uint32_t canvas_height;     // CSS pixels
    uint32_t framebuffer_width; // actual render pixels
    uint32_t framebuffer_height;
    float dpr;          // device pixel ratio
    float render_scale; // game-controlled quality scaling
} PlatformDisplayInfo;
```
