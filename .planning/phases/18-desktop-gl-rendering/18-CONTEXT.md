# Phase 18: Desktop Build - Context

**Gathered:** 2026-03-13
**Status:** Ready for planning

<domain>
## Phase Boundary

Make desktop build fully functional: vendor glad, integrate GLFW for native window with real GL context, enable nt_gfx GL backend on desktop, implement native input backend (keyboard + mouse via GLFW). Both web and desktop builds should work with the same game code. Spinning cube demo runs on both platforms.

</domain>

<decisions>
## Implementation Decisions

### Shader source strategy
- Engine prepends `#version` line per platform: `#version 300 es` on web, `#version 330 core` on desktop
- Game and engine modules write shader body only (no `#version` directive in source strings)
- `precision mediump float;` stays in shader source -- valid on both platforms (desktop GL ignores it)
- Prepend happens at GL backend level (`nt_gfx_backend_create_shader`), not shared API level
- No special validation for accidental `#version` in game source -- GL compiler catches naturally
- All engine-internal shaders (nt_shape) follow the same body-only convention
- GL headers: `#ifdef NT_PLATFORM_WEB` uses `GLES3/gl3.h`, non-web uses `glad/gl.h`

### Desktop window behavior
- Game must set `g_nt_window.width` and `g_nt_window.height` before `nt_window_init()` -- crash with error if not set
- Window resizable by default, game can configure via `g_nt_window.resizable` flag
- Fullscreen via game code only (`nt_window_set_fullscreen(true/false)`) -- no built-in keyboard shortcut
- `g_nt_window.title` field -- game sets before init, default "Neotolis"
- Title works on both platforms: desktop = GLFW window title, web = `document.title` via EM_JS
- Shell.html `<title>` serves as pre-load placeholder on web

### Dependency vendoring
- GLFW vendored in `deps/glfw` (same pattern as cglm)
- glad vendored in `deps/glad` -- generated for GL 3.3 Core profile
- GLFW is the platform backend for desktop: window creation, GL context, input events
- Each dependency keeps its own LICENSE file in its deps/ folder
- Web: own EM_JS backends (minimal size). Desktop: GLFW backends (portable, practical)

### Desktop input
- GLFW mouse maps into same unified pointer model -- pointer[0] with type=NT_POINTER_MOUSE
- Same nt_input API surface on both platforms -- game code works unchanged
- Clear all pressed keys/buttons on window focus loss (prevents stuck-key bugs)
- Desktop DPR support: GLFW content scale used for high-DPI coordinate mapping
- Keyboard active only when window focused (same behavior as web canvas focus)
- Alt+F4 / window close button sets quit flag -- game loop exits cleanly, runs shutdown functions
- Standard GLFW window should-close pattern maps to existing `nt_app_quit()` mechanism

### Vsync
- `g_nt_app.vsync` enum: `NT_VSYNC_OFF` / `NT_VSYNC_ON` / `NT_VSYNC_ADAPTIVE`
- Default: `NT_VSYNC_ON`
- Maps to `glfwSwapInterval(0/1/-1)` on desktop, ignored on web (browser controls via rAF)
- Coexists with `target_dt` frame cap -- effective rate is whichever limit is more restrictive

### Demo parity
- Spinning cube demo builds for both web and desktop from same main.c
- Demo shader strings updated to body-only (remove `#version` lines) as part of this phase
- Proves "same game code" goal end-to-end

### VSCode desktop configs
- launch.json: Debug Desktop / Run Desktop (release) configs for each example (hello, bench_shapes, spinning_cube)
- Uses `cppvsdbg` debugger with preLaunchTask for native-debug/native-release builds
- No server tasks needed -- runs exe directly

### Claude's Discretion
- glad generation options (extensions to include, loader style)
- GLFW CMake integration specifics (GLFW_BUILD_EXAMPLES=OFF, etc.)
- Exact `glfwWindowShouldClose` integration into nt_app_native.c loop
- Desktop `glClearDepth` (double) vs web `glClearDepthf` (float) handling
- CMake preset additions for desktop GL builds
- GLFW callback registration details and event buffering
- Native DPR change detection (monitor switch, scale change)

</decisions>

<specifics>
## Specific Ideas

- GLFW is the OS layer for desktop, not a framework -- thin wrappers in native backends
- "Same game code" is the key constraint -- everything must compile and run on both platforms without ifdefs in game code
- Engine handles all platform differences internally (shader version, GL headers, window API, input events)
- Frame pacing: vsync (GPU) and target_dt (CPU) are orthogonal -- the slower one wins

</specifics>

<code_context>
## Existing Code Insights

### Reusable Assets
- `nt_gfx_gl_ctx.h`: Platform abstraction for GL context -- native stub ready for glad/GLFW implementation
- `nt_gfx_gl.c`: Unified GL backend already structured for WebGL 2 + OpenGL 3.3 Core
- `nt_window_native.c`: Stub with 800x600 defaults -- replace with GLFW window
- `nt_input_native.c`: No-op stubs -- replace with GLFW callbacks
- `nt_app_native.c`: Working frame loop with rate cap -- add glfwPollEvents + glfwSwapBuffers + should-close check
- `nt_platform.h`: NT_PLATFORM_WEB / NT_PLATFORM_WIN / NT_PLATFORM_NATIVE defines

### Established Patterns
- Swappable backend: shared .c + per-platform implementations (nt_app, nt_window, nt_input)
- Consumer-provides-backend CMake pattern (nt_shape links nt_core, consumer provides nt_gfx or stub)
- Global extern struct: `g_nt_` prefix for module config and state
- `#ifdef NT_PLATFORM_WEB` for platform-specific code in shared files
- `nt_add_module()` CMake helper for new modules

### Integration Points
- `nt_gfx` CMakeLists.txt: currently only builds GL backend for Emscripten -- needs non-web GL target with glad
- `nt_gfx_gl.c`: uses `#include <GLES3/gl3.h>` -- needs ifdef for glad headers on desktop
- `nt_gfx_gl.c`: `nt_gl_clear_depth` macro needs desktop variant (`glClearDepth` with double)
- Demo `examples/spinning_cube/main.c`: shader strings have `#version 300 es` -- need body-only update
- `engine/renderers/` nt_shape module: embedded shaders have `#version 300 es` -- need body-only update

</code_context>

<deferred>
## Deferred Ideas

Migrated to GitHub issues — see label `deferred`.

</deferred>

---

*Phase: 18-desktop-gl-rendering*
*Context gathered: 2026-03-13*
