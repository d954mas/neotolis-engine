# Neotolis Engine — Technical Specification

**Version:** v0.3-consolidated (split from a single file, 2026-07)
**Status:** Architectural baseline + implementation-oriented spec
**Language target:** C17 (vendored C++ allowed behind extern "C" boundary)
**Primary runtime target:** Web / WASM + WebGL 2
**Secondary future target:** WebGPU

This spec consolidates all architectural decisions from the v0.1 overview, the v0.2
technical spec, and subsequent design sessions into one authoritative reference,
organized as one chapter per subsystem.

Language baseline is C17 for broader compiler and Emscripten toolchain support. Vendored C++ dependencies (e.g. Basis Universal transcoder/encoder) are permitted when no C alternative exists, provided they are isolated behind `extern "C"` wrappers and `enable_language(CXX)` is scoped to their subdirectory CMakeLists — not the root.

## Overview

Neotolis is a minimalist, code-first game engine. The game controls the main loop:
it defines gameplay system order, render passes, and tags, and calls engine
subsystems explicitly — the engine gives building blocks, not a pipeline.

The engine runs the frame lifecycle, stores entities/components/resources, updates
transforms, loads runtime assets from NTPACK packs asynchronously, and provides the
WebGL 2 render backend, input and platform services, and audio playback. Data flows
through a small set of composable modules: entities own hierarchy, per-kind
components hold render state, thin render items are sorted and batched by
game-chosen policy, and generational handles resolve resources published from
priority-stacked packs.

The builder is a separate native binary that does the heavy work offline: it imports
source assets, validates compatibility, converts them to custom runtime formats, and
writes NTPACK packs with embedded manifests — the runtime loads prebuilt data with
no source-format parsers. The full picture is in
[meta/architecture-snapshot.md](meta/architecture-snapshot.md).

## Chapters

| File | Contents |
|---|---|
| [core/principles.md](core/principles.md) | Design philosophy and the strict engine/game ownership boundary |
| [core/scope.md](core/scope.md) | Baseline scope, explicit non-goals, and the `nt_ui` module note |
| [core/module-layout.md](core/module-layout.md) | Module directory layout and interface/impl/stub composition |
| [core/api-contracts.md](core/api-contracts.md) | Public API ownership, lifetime, and naming contract vocabulary |
| [runtime/platform.md](runtime/platform.md) | Platform layer: Web/WASM target, responsibilities, canvas/DPR handling |
| [runtime/frame-lifecycle.md](runtime/frame-lifecycle.md) | Frame order, game callbacks, fixed update loop |
| [runtime/memory.md](runtime/memory.md) | Memory categories, frame scratch arena, capacity policy |
| [data/entity.md](data/entity.md) | Entity handles, generations, entity table, hierarchy |
| [data/component-storage.md](data/component-storage.md) | Sparse/dense component storage and typed component APIs |
| [data/transform.md](data/transform.md) | Transform component, hierarchy inheritance, dirty propagation |
| [render/render-components.md](render/render-components.md) | Drawable, mesh, material, sprite, text, and shadow components |
| [render/architecture.md](render/architecture.md) | Engine/game render split, backend API shape, renderer classes |
| [render/items-sorting-batching.md](render/items-sorting-batching.md) | Render tags, 16-byte render items, sorting policy, batching/instancing |
| [render/shader.md](render/shader.md) | ShaderAsset interface and the four levels of shader data |
| [render/material.md](render/material.md) | Material model, vec4 params, render state ownership |
| [assets/resource.md](assets/resource.md) | Resource registry, handles, resolve/publication, blob pinning, asset types |
| [assets/async-loading.md](assets/async-loading.md) | Pack/asset state machines, async loading flow, retry policy |
| [assets/ntpack.md](assets/ntpack.md) | NTPACK flat binary pack format and parsing |
| [assets/runtime-formats.md](assets/runtime-formats.md) | Runtime-format rule, validation, mesh format strategy |
| [builder/builder.md](builder/builder.md) | Builder architecture, API, codegen, caches, atlas builder |
| [io/input.md](io/input.md) | Polling input model, pointer state, capture |
| [io/audio.md](io/audio.md) | Platform-agnostic audio module, clips, voices, JS bridge |
| [debug/logging-errors-debugging.md](debug/logging-errors-debugging.md) | Logging, asserts, errors, debug overlay, and the dev-only devapi |
| [ui/radial-widgets.md](ui/radial-widgets.md) | Radial widgets and the custom-attr image path rationale |
| [ui/rich-text.md](ui/rich-text.md) | Rich text: run-list, solver, decoration, effects, z-layers |
| [decisions/index.md](decisions/index.md) | Registry of locked decisions and the ADR convention |
| [decisions/0001-render-item-size.md](decisions/0001-render-item-size.md) | ADR: render item size and instance packing strategy |
| [decisions/0002-render-target-pass-property.md](decisions/0002-render-target-pass-property.md) | ADR: render target selection as a pass property |
| [meta/implementation-order.md](meta/implementation-order.md) | Suggested subsystem implementation order |
| [meta/open-questions.md](meta/open-questions.md) | Open, non-blocking future questions |
| [meta/architecture-snapshot.md](meta/architecture-snapshot.md) | Final architecture snapshot: game / engine / builder |

## Module → chapter map

Source directories under `engine/` (and the builder under `tools/builder/`) mapped to
the chapter that covers them:

Any public-header/API contract change also reads
[core/api-contracts.md](core/api-contracts.md) for the cross-cutting ownership,
lifetime, and naming vocabulary.

| Source dir | Chapter(s) |
|---|---|
| `engine/entity` | [data/entity.md](data/entity.md) |
| `engine/comp_storage` | [data/component-storage.md](data/component-storage.md) |
| `engine/transform_comp` | [data/transform.md](data/transform.md) |
| `engine/drawable_comp`, `engine/mesh_comp`, `engine/material_comp`, `engine/sprite_comp` | [render/render-components.md](render/render-components.md) |
| `engine/render`, `engine/renderers` | [render/architecture.md](render/architecture.md), [render/items-sorting-batching.md](render/items-sorting-batching.md) |
| `engine/graphics` | [render/architecture.md](render/architecture.md), [core/module-layout.md](core/module-layout.md) |
| `engine/sort` | [render/items-sorting-batching.md](render/items-sorting-batching.md) |
| `engine/material` | [render/material.md](render/material.md), [render/shader.md](render/shader.md) |
| `engine/resource` | [assets/resource.md](assets/resource.md), [assets/async-loading.md](assets/async-loading.md), [assets/ntpack.md](assets/ntpack.md) |
| `engine/atlas` | [assets/resource.md](assets/resource.md) (format), [builder/builder.md](builder/builder.md) (atlas builder) |
| `engine/font` | [assets/resource.md](assets/resource.md) (NT_ASSET_FONT), [ui/rich-text.md](ui/rich-text.md) (decoration) |
| `engine/hash` | [assets/resource.md](assets/resource.md) (identity hashing) |
| `engine/ui` | [core/scope.md](core/scope.md) (`nt_ui` note), [ui/radial-widgets.md](ui/radial-widgets.md), [ui/rich-text.md](ui/rich-text.md) |
| `engine/input` | [io/input.md](io/input.md); automation: [debug/logging-errors-debugging.md](debug/logging-errors-debugging.md) |
| *(audio — planned module, no dir yet)* | [io/audio.md](io/audio.md) |
| `engine/fs`, `engine/http` | [assets/async-loading.md](assets/async-loading.md) (pack I/O), [core/module-layout.md](core/module-layout.md) (swappable) |
| `engine/time` | [runtime/frame-lifecycle.md](runtime/frame-lifecycle.md) |
| `engine/window`, `engine/platform` | [runtime/platform.md](runtime/platform.md) |
| `engine/app` | [debug/logging-errors-debugging.md](debug/logging-errors-debugging.md) (time & render control), [runtime/frame-lifecycle.md](runtime/frame-lifecycle.md) |
| `engine/log`, `engine/devapi`, `engine/debug_overlay`, `engine/metrics`, `engine/introspect` | [debug/logging-errors-debugging.md](debug/logging-errors-debugging.md) |
| `engine/memory`, `engine/pool` | [runtime/memory.md](runtime/memory.md) |
| `engine/core` | [core/principles.md](core/principles.md), [core/api-contracts.md](core/api-contracts.md); assert policy: [debug/logging-errors-debugging.md](debug/logging-errors-debugging.md) |
| `engine/clipboard` | [core/module-layout.md](core/module-layout.md) (stub semantics example) |
| `engine/systems` | [runtime/frame-lifecycle.md](runtime/frame-lifecycle.md) (explicit system calls) |
| `engine/basisu`, `engine/fpng` | [builder/builder.md](builder/builder.md) (texture encode), [debug/logging-errors-debugging.md](debug/logging-errors-debugging.md) (frame capture) |
| `engine/math`, `engine/color`, `engine/utf8`, `engine/base64` | small utility modules — no dedicated chapter |
| `tools/builder` | [builder/builder.md](builder/builder.md) |
