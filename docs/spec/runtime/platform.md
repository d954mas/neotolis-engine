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

## Process probes

`core/nt_platform.h` declares process probes; `engine/platform` implements them
(`native/nt_platform_native.c`, `web/nt_platform_mem_web.c`). Modules and games
read process figures only through these calls, never through the OS directly.

- `nt_platform_memory_usage()` returns `nt_platform_mem_t`. Native: `used` is the
  resident set and `reserved` mirrors it. Web: `used` is allocator in-use bytes
  (not RSS) and `reserved` is the heap size.
- `nt_platform_cpu_usage()` returns `nt_platform_cpu_t`: user and system CPU time
  in nanoseconds, cumulative since process start and summed over all threads.
  Web builds do not declare it, because the browser exposes no process CPU time.
  Guard callers with `!defined(NT_PLATFORM_WEB)`; `NT_PLATFORM_NATIVE` excludes
  Windows.
- `nt_platform_cpu_percent(prev, cur, wall_ns)` turns two readings into load over
  the interval with a one-core base: 100 is one core fully busy, and several busy
  threads read above 100. It keeps no state; the caller stores the previous
  reading and picks the window, measured with `nt_time_nanos()`. Windows counters
  tick every ~15.6 ms, so use a window of at least ~0.5 s. Only the user+system
  total must grow: older Linux kernels can move time between the two fields. A
  zero `wall_ns` or a total going backwards asserts.

A failing OS call inside a probe asserts. Native builds support Windows, Linux
and macOS; any other OS fails to compile. Machine-wide counters, core count and
per-thread CPU time are out of scope.

## WASM bridge allocation

Allocate in C: JS reports a size, C allocates and passes the buffer for JS to
fill, as `engine/http/web/nt_http_web.c` does. If EM_JS must allocate, only
`wasmExports['malloc']` works (`Module['_malloc']` does not work with the
engine's emmalloc configuration, and bare `_malloc` fails Closure compilation);
list `malloc` in `EM_JS_DEPS`.

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
