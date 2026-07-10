# Rendering Architecture

The engine provides the renderer backend and draw primitives; the game owns the
render pipeline — pass order, tags, sort policy, batching choice. Defines the
backend API shape and the three renderer complexity classes: building blocks,
batched dynamic, and specialized.

Related: [Items, Sorting, Batching](items-sorting-batching.md), [Render Components](render-components.md), [Shader System](shader.md)

## Engine/game boundary

Renderer backend and render primitives belong to engine. Render pipeline belongs to game.

### Engine provides

- renderer begin/end frame
- begin/end pass
- draw mesh primitive
- draw sprite primitive
- GPU resource creation and binding
- material/shader binding helpers

### Game decides

- pass order
- which tags are used
- sort policy for a pass
- whether a pass sorts by depth or material
- whether a given list uses batching or not

## Renderer backend API shape

Engine-oriented, not WebGL-mirror and not full WebGPU abstraction:

```c
renderer_begin_frame();
renderer_end_frame();

renderer_begin_pass(&desc);
renderer_end_pass();

renderer_set_camera(&camera);

renderer_draw_mesh(...);
renderer_draw_sprite(...);
```

### Render targets

Render targets are a general backend capability for offscreen passes, not a
text-only path and not a shadow-map subsystem. Typical users include post-fx,
glow or bloom-like effects, minimaps, portals, and depth-aware rendering.

The game still owns pass order. Each pass selects its destination through
`nt_pass_desc_t.target`: zero selects the default framebuffer, and a valid
`nt_render_target_t` selects an offscreen target. `nt_gfx` binds the matching
backend framebuffer internally during `nt_gfx_begin_pass`; public code does not
bind or unbind render-target state outside the pass descriptor.

Render-target color and sampleable depth attachments are exposed as normal
`nt_texture_t` handles for later sampling. Backend FBO/renderbuffer ids stay
private to the concrete graphics implementation.

`nt_render_target_desc_t` explicitly selects the color format and default sampler
state, plus depth storage (`NONE`, `BUFFER`, or `TEXTURE`) and depth format. The
current render-target color format is `RGBA8`. `NONE`
requires `NT_TEXTURE_FORMAT_INVALID`; `BUFFER` creates a non-sampleable
renderbuffer in the requested depth format; `TEXTURE` creates a sampleable
texture in the requested depth format with its own filter and wrap state. The
descriptor is retained as the single source for creation, resize, and context
restore. A backend must not substitute its own attachment format or default
sampler state.

The supported depth formats are `DEPTH16`, `DEPTH24`, and `DEPTH32F`. The current
sampler contract has no depth-comparison mode, so WebGL 2 texture completeness
requires `NEAREST` minification and magnification for depth textures. Wrap state
remains explicit and may use clamp, repeat, or mirrored repeat. Binding a
separate sampler does not relax the depth filtering restriction.

This capability supplies low-level targets and depth textures only. It does not
define light cameras, PCF, cascades, shadow atlases, material shadow integration,
or a shadow-map system.

## Renderer complexity classes

Not all renderers carry the same weight. The engine ships three classes; copying patterns across classes is a common mistake.

**Building blocks** — direct GPU primitives (`nt_gfx_draw_indexed`, `nt_mesh_renderer`). Single pipeline, fixed pattern, one draw call per item. Use for 3D meshes, custom geometry, anything where the game owns batching strategy. Stay minimal.

**Batched dynamic** — high-throughput accumulation renderers (`nt_sprite_renderer`; future particles). Cmd queue, state-delta tracking, overflow recovery via snapshot/replay, multi-page atlas resolution, SIMD path. Optimized for many small draws per frame (1k–60k items). Complex by necessity — the 580 LOC of `nt_sprite_renderer.c` are paid for by measured throughput on bunnymark. Don't simplify away the cmd queue or snapshot recovery without a measured replacement plan.

**Specialized** — domain-specific layout (`nt_text_renderer` glyph atlas + line layout; future debug-line/IM-GUI). Sit between the two — more state than primitives, less throughput pressure than batched dynamic.

When adding a new renderer, classify first:

- One pipeline, fixed pattern → **building block** (model after `nt_mesh_renderer`)
- 1k+ items/frame with dynamic state → **batched dynamic** (study `nt_sprite_renderer`, but only copy what your throughput demands)
- Domain-specific layout/data → **specialized**

`nt_sprite_renderer.c` is not a renderer template. Its complexity earns its keep at 60k items/frame; a 100-item UI overlay doesn't need any of it.
