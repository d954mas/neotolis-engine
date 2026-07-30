# Scope

What the baseline includes (Web/WASM + WebGL 2, entities, transform, resources,
NTPACK, shader/material, mesh/sprite/text rendering, builder) and what is
explicitly out of scope (editor, scripting, physics, WebGPU backend).

Related: [Core Principles](principles.md), [Module Layout](module-layout.md), [nt_ui Module](../ui/nt-ui.md), [Rich Text](../ui/rich-text.md), [Radial Widgets](../ui/radial-widgets.md)

## Included in baseline

- Web platform
- WASM runtime
- WebGL 2 renderer backend (sole baseline, no WebGL 1 fallback)
- component-based entity architecture
- hierarchy
- transform system
- resource system with async loading
- custom binary pack format (NTPACK)
- custom runtime formats
- shader + material system
- mesh rendering
- sprite rendering
- text rendering (later-ready)
- fixed update + frame update
- builder in C
- input polling + pointer capture
- render items + sorting + batching policy
- mesh instancing planned
- sprite CPU batching planned
- audio system (platform-agnostic, handle-based) — **planned, no `engine/audio` yet**

## Explicitly not in scope

- editor
- Lua scripting
- full physics engine
- material graph editor
- plugin architecture
- WebGPU backend implementation
- scene editor / authoring editor
- full UI framework — `nt_ui` is a minimal immediate-mode layout + render
  bridge over vendored Clay, not a framework: no widget authoring tools, no
  styling pipeline beyond inline calls, no UI-definition hot-reload, no
  scene-graph integration. Its full contract is [ui/nt-ui.md](../ui/nt-ui.md).
- hot reload of compiled native/WASM code
- generic reflection-heavy system architecture
- WebGL 1 support
- a command/RPC API in the shipped runtime (NOTE: the `nt_devapi` milestone has shipped a
  dev-only, self-describing JSON command surface for engine introspection and
  automation, over both a native loopback-TCP transport and a web `ccall` bridge. It is gated
  by `NT_DEVAPI_ENABLED` (OFF by default) and the `engine/devapi` subdir is compile-excluded
  from release, so it remains **not** part of the shipped runtime or the asset/format
  pipeline. See [Logging, Errors, Debugging — devapi](../debug/logging-errors-debugging.md).)
