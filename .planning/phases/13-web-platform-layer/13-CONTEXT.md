# Phase 13: Web Platform Layer - Context

**Gathered:** 2026-03-11
**Status:** Ready for planning

<domain>
## Phase Boundary

Engine provides canvas/window management, display metrics, and platform abstraction so downstream modules never call browser APIs directly. Covers PLAT-01 (canvas + WebGL 2 context), PLAT-02 (display info with DPR), PLAT-03 (resize detection), PLAT-07 (EM_JS exclusively). Context creation itself moves to the renderer (Phase 15) -- nt_window provides the surface only.

</domain>

<decisions>
## Implementation Decisions

### Module: nt_window
- New module at `engine/window/` (not under `engine/platform/`)
- Cross-platform window/canvas abstraction with `g_nt_window` global extern struct
- File layout matches nt_app: `nt_window.h`, `nt_window.c` (shared logic), `web/nt_window_web.c`, `native/nt_window_native.c`
- `engine/platform/` stays for platform-specific internals (shell, nt_web.h)

### DPR and render resolution
- `max_dpr` config field in g_nt_window, default 2.0
- Effective DPR = `min(device_dpr, max_dpr)`
- No render_scale -- max_dpr handles quality control. Set max_dpr=1 for no DPR scaling
- Changes to max_dpr auto-detected next frame (no explicit apply call)

### g_nt_window struct
- Config (game writes before init): `max_dpr` (float, default 2.0)
- State (engine writes, game reads): `width`, `height` (uint32_t, CSS pixels), `fb_width`, `fb_height` (uint32_t, framebuffer pixels), `dpr` (float, effective)
- fb fields pre-computed for consistency (rounded once, desktop may differ from width*dpr)
- Context attributes (depth, stencil, antialias, alpha) NOT in nt_window -- moved to renderer (Phase 15)

### Resize behavior
- Poll per frame via `nt_window_poll()` -- check canvas CSS size, if changed update framebuffer
- Also detects max_dpr runtime changes automatically
- ResizeObserver event-driven can be added later trivially (same internal logic, different trigger)
- nt_window updates canvas backing store (canvas.width/height = CSS * effective DPR)
- nt_window updates g_nt_window fields only -- renderer handles glViewport in its own frame begin

### Window vs graphics context separation
- nt_window does NOT create GL/WebGPU context -- renderer (Phase 15) owns context creation
- nt_window provides the canvas/window surface
- Renderer reads g_nt_window for surface info, creates context with its own config (g_nt_gfx)
- Clean separation for future WebGPU/Vulkan: window = surface, renderer = graphics API

### Context attributes (deferred to Phase 15)
- depth, stencil, antialias -- configurable in g_nt_gfx (Phase 15)
- web_canvas_alpha -- bool under `#ifdef NT_PLATFORM_WEB` in renderer struct
- Defaults: depth=true, stencil=true, antialias=false, alpha=false
- Renderer creates context via `emscripten_webgl_create_context()` with these attributes

### Init/shutdown
- Explicit: game calls `nt_window_init()` in main() before nt_app_run()
- Game sets g_nt_window.max_dpr before init
- `nt_window_shutdown()` for symmetry (no-op on web now, future desktop window cleanup)
- No hidden auto-init -- game controls what and when

### Error handling
- WebGL 2 context creation failure handled by renderer (Phase 15), not nt_window
- Crash with clear message: technical + user hint ("WebGL 2 context creation failed. Try updating your browser or enabling hardware acceleration.")
- Uses existing crash overlay from shell template

### Naming conventions
- `nt_window_init()` / `nt_window_shutdown()` -- lifecycle
- `nt_window_poll()` -- per-frame size/DPR polling
- Internal shared: `nt_window_apply_sizes()` or similar -- DPR math in nt_window.c

### Native build
- Fake defaults for testing: 800x600, dpr=1.0, depth=true, stencil=true
- TODO comments indicating desktop implementation is future work
- Shared DPR math from nt_window.c available for native unit tests

### JS bridge
- All JS interop via EM_JS exclusively (PLAT-07)
- EM_JS with inline no-op fallback pattern (from nt_web.h) for non-web builds
- Canvas queries (CSS size, device DPR) via EM_JS in nt_window_web.c

### Claude's Discretion
- Exact EM_JS function signatures for canvas queries
- nt_window_apply_sizes() internal API details
- CMakeLists.txt specifics (link flags, platform conditionals)
- How poll detects DPR changes (compare against last applied value)
- Native stub exact field values and structure

</decisions>

<specifics>
## Specific Ideas

- "DPR is not only web -- it's a window/screen concept across platforms"
- Module should be nt_window (not nt_display) because it's window/canvas size, not physical display size
- Think about desktop/WebGPU from the start: window = surface, renderer = graphics context
- "Game developer does everything explicitly" -- no hidden init, no auto-magic

</specifics>

<code_context>
## Existing Code Insights

### Reusable Assets
- `nt_web.h`: EM_JS + inline no-op fallback pattern -- reuse for canvas query functions
- `nt_platform.h`: NT_PLATFORM_WEB / NT_PLATFORM_WIN / NT_PLATFORM_NATIVE defines
- `nt_app.h` / `g_nt_app`: global extern struct pattern, per-module config convention
- `shell.html.in`: already has `<canvas id="canvas">`, `Module.canvas`, webglcontextlost handler, crash overlay

### Established Patterns
- Swappable backend: shared .c + per-platform implementations (nt_app.c + nt_app_web.c + nt_app_native.c)
- nt_add_module() CMake helper for new modules
- `#ifdef NT_PLATFORM_WEB` with typedef fallback for native clang-tidy
- File-scope statics (`s_`) for module state, `g_nt_` for extern globals
- EM_JS over EM_ASM (avoids C17 variadic macro warning)

### Integration Points
- nt_window links to nt_core (platform defines, types)
- Renderer (Phase 15) reads g_nt_window for surface info and creates GL context
- Input (Phase 14) reads g_nt_window for coordinate mapping (CSS -> framebuffer)
- nt_app_run() does NOT auto-init nt_window -- game calls nt_window_init() explicitly
- Shell template provides canvas element that nt_window_web.c queries

</code_context>

<deferred>
## Deferred Ideas

Migrated to GitHub issues — see label `deferred`.

</deferred>

---

*Phase: 13-web-platform-layer*
*Context gathered: 2026-03-11*
