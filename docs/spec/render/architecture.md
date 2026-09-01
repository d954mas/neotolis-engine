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
- shader-stage compilation and program linking
- material/shader binding helpers

Each successful `nt_gfx_make_program` creates a new program; there is no
deduplication. The game shares its handle across materials that use the same
linked stages; see [Shader System](shader.md).

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

### Vertex inputs

Vertex-input state is an **owned public object**, `nt_vertex_input_t`: a
vertex layout, an optional per-instance layout, and the vertex buffer
[+ index buffer] baked together (GL: a VAO). One
`nt_gfx_bind_vertex_input` selects the whole geometry for the following
draws; per mesh switch that is a single `glBindVertexArray` instead of
buffer re-binds plus per-attribute `glVertexAttribPointer` rewrites. The
object's *static* half — vertex attributes and the index binding — is
immutable after creation; its *instance* attribute pointers are re-specified
into the bound object by each `nt_gfx_bind_instance_buffer` (WebGL2 has no
baseInstance, so per-draw instance re-pointing stays). An empty layout with
no buffers is the attribute-less `gl_VertexID` path; every draw asserts a
bound vertex input.

A vertex attribute is the raw GL triple `(type, count 1-4,
normalized)` plus location and byte offset (`nt_vertex_attr_t`) — no enum of
allowed combinations; the float/half/byte/short subset of the
`vertexAttribPointer` space (no int32 or 2_10_10_2 packed types) is available
to game-built layouts and mesh-pack streams alike. The pack's on-disk stream
type enum stays separate from the gfx vertex type enum in the vertex-layout
API (the mesh renderer maps between them totally; the mesh-activation side
table `nt_gfx_mesh_info_t` still stores raw pack descs — a known, contained
exception). Vertex-input creation asserts the WebGL2 alignment rules
(attribute offset and stride multiples of the attribute's type size,
locations within the WebGL2-guaranteed 16): game-declared layouts never pass
through the builder's validator, and desktop GL accepts what the browser
rejects. Pack data never reaches those asserts: mesh activation hard-rejects
invalid per-stream type/count/normalized, duplicate name hashes, and
misaligned offsets/strides before any vertex input exists. Vertex inputs are
created once and cached by their owners, so the asserts are off the hot path.

**Lifetime and the destroy cascade.** `nt_gfx_destroy_buffer` destroys every
live vertex input referencing that buffer as its vertex or index buffer
(precedent: destroying a program destroys its pipelines). Mesh deactivation
destroys the mesh's buffers and reaches no renderer, so this cascade is what
keeps renderer-cached vertex inputs from outliving mesh buffers; mesh caches
revalidate handles with `nt_gfx_vertex_input_valid` on lookup. Because the
cascade makes stale handles routine, `nt_gfx_destroy_vertex_input` tolerates
stale and INVALID handles as no-ops. The dynamically captured instance
buffer is *not* cascade-destroyed, but destroying one clears the dependents'
pointed flag: their next instanced draw asserts until
`nt_gfx_bind_instance_buffer` re-points them, and the GL attachment's
storage lingers until that re-point or the vertex input's death.
Buffer *contents* may change freely — `update`/`orphan`
keep the GL name, so baked attachments survive per-flush orphaning — and
index-buffer data ops run inside a service upload VAO in the backend,
because the element-array binding is VAO state — it would otherwise be
silently rewired into whichever vertex input is bound, and core-profile GL
rejects the bind with VAO 0. Vertex inputs die with a lost context
and are not auto-restored: loss itself frees their pool slots, so a handle
held across a loss goes stale and `nt_gfx_vertex_input_valid` reports false.
Renderer restore paths recreate them; caches validate on lookup and
self-heal. This is the rule for baked objects — pipelines and vertex
inputs, both assembled from other handles with no re-fill path: a context
loss frees their pool slots outright. Primary resources (buffers, textures,
shaders, programs) instead survive a loss as husks — pool slot alive,
backend gone — because per-frame code keeps operating on them through the
loss window and the restore recipe has their owners destroy the old handles
explicitly.

**Program / pipeline split.** A program is the linked (vertex, fragment) pair
and owns everything that follows from linking: uniform locations, uniform
values, and global UBO block bindings. A pipeline is fixed-function render
state that *borrows* a program handle — it owns no vertex-input state, and
pipeline and vertex-input binding are orthogonal: either may change without
re-binding the other. Two pipelines on one program
share every uniform value, and binding one does not reset what the other set —
each consumer sets every uniform it needs on every draw. A uniform a material
does not declare retains the value last written on that program; this applies
to sampler units and material params. Planned fixes are fixed sampler-unit
assignments per program (#359) and per-material param UBOs bound at material
transitions (#133). Until both land, two materials sharing one program must
declare the same params and texture slots. Destroying a program destroys its
pipelines, and a context loss frees every pipeline slot; renderers remove
dead cache records during insertion after a miss or when resetting their
caches.

**Cache identity.** Renderers key their pipeline caches on a 64-bit
hash of the pipeline signature — program handle and render
state — and treat that hash as identity: descriptors are never compared on a
hit. The cached pipelines borrow programs the game owns, so a cache entry never
extends a program's lifetime.
The hashed population is distinct material states, tens of values
in a real game, so a 64-bit collision is not a practical risk, and a fixed
array with a linear scan stays cheaper than a hash map at that scale. Every
`nt_pipeline_desc_t` field a renderer varies must be folded into its key.

Every renderer now keys pipelines on `program.id` folded with
`nt_material_info_t.render_state_hash` (the `nt_text_renderer` pattern) —
layouts moved to the vertex-input objects, so materials differing only in
layout share one pipeline. `render_state_hash` folds `color_mode`, so mesh
pipelines still split per color mode even though nothing in the slimmed
pipeline depends on it — an accepted over-split (removing it would touch the
material module and every renderer).

Vertex-input caches follow the same hash-as-identity standard for *derived*
layouts. The mesh renderer keeps a per-mesh versions table
(`[nt_gfx_max_meshes()][nt_mesh_renderer_desc_t.max_mesh_layouts]`). Each row
stores its mesh's full generation-checked handle. A different generation
clears the entire row, including bufferless vertex inputs that have no
destroy-cascade hook. Within the row, entry identity is the hash of the
derived layout (mesh streams × material attr_map — attr_map entries
matching no stream do not split; a material
mapping none of the streams derives an empty layout and takes the
attribute-less gl_VertexID path) plus color mode. Handles are revalidated on
lookup because buffer destruction can invalidate cached versions.
Exhausting a mesh's version row asserts, naming the knob — silent eviction would hide VAO re-creation
thrash as an invisible perf regression.

The sprite renderer owns its vertex/index buffers and clears its entire
vertex-input cache on shutdown or GPU restore before replacing those buffers.
Cache entries are weak: a hit validates the handle, and an entry whose
vertex input died (context loss) is recreated in place, so repeated losses
cannot grow the cache. A miss creates the vertex input and caches it only on
success; recoverable creation failures leave the cache unchanged so the next
lookup retries.

### Render targets

Render targets are a general backend capability for offscreen passes, not a
text-only path and not a shadow-map subsystem. Typical users include post-fx,
glow or bloom-like effects, minimaps, portals, and depth-aware rendering.

The game still owns pass order. Each pass selects its destination through
`nt_pass_desc_t.target`: zero selects the default framebuffer, and a valid
`nt_render_target_t` selects an offscreen target. `nt_gfx` binds the matching
backend framebuffer internally during `nt_gfx_begin_pass`; public code does not
bind or unbind render-target state outside the pass descriptor.

Pass color and depth clears are pass-owned operations. In particular,
`clear_depth` is applied independently of the previous pipeline's `depth_write`
state; pipeline write masks affect draws, not the next pass initialization.

Render-target color and sampleable depth attachments are exposed as normal
`nt_texture_t` handles for later sampling. Backend FBO/renderbuffer ids stay
private to the concrete graphics implementation.

`nt_render_target_desc_t` explicitly selects the color format and default sampler
state, plus depth storage (`NONE`, `BUFFER`, or `TEXTURE`) and depth format. The
supported render-target color formats are `RGBA8` and `RGBA16F`. `NONE`
requires `NT_TEXTURE_FORMAT_INVALID`; `BUFFER` creates a non-sampleable
renderbuffer in the requested depth format; `TEXTURE` creates a sampleable
texture in the requested depth format with its own filter and wrap state. The
descriptor is retained as the single source for creation, resize, and context
restore. A backend must not substitute its own attachment format or default
sampler state.

`RGBA16F` is the HDR color path: it carries values above 1.0, so a tone-mapping
or bright-pass stage has headroom instead of a buffer already clamped at write
time. It stays filterable in WebGL 2 core, so `LINEAR` on the color attachment
remains valid. `RGBA32F` is not supported for render targets — it additionally
needs `OES_texture_float_linear` to be filtered and `EXT_float_blend` to be
blended into, and both are absent on roughly half of iOS devices.

Creation does not consult `gpu_caps.has_float_render_target`. A device that
cannot render to half-float fails the backend completeness check, and creation
returns invalid — the fallback path a caller needs regardless. The capability
bit exists so a caller can choose its format without paying for a failed
attempt.

The supported depth formats are `DEPTH16`, `DEPTH24`, and `DEPTH32F`. A depth
attachment's own texture state stays `NEAREST` for minification and
magnification: WebGL 2 texture completeness rejects filtered depth unless
comparison is enabled, and comparison is not texture state. Wrap state remains
explicit and may use clamp, repeat, or mirrored repeat.

Depth comparison lives on the sampler object (`nt_sampler_desc_t.compare_func`),
not on the texture, because one depth target is read two ways: through a
comparison sampler for the shadow lookup, and through a plain sampler for a
raw-depth debug view. The field is a single tri-state — `NONE`, `LEQUAL`,
`LESS` — so a zero-filled descriptor is a plain sampler and there is exactly one
spelling of "no comparison". Sampler state supersedes texture state, so a
comparison sampler makes `LINEAR` legal on that binding while the attachment
description is untouched. A comparison sampler is rejected on non-depth storage,
where the comparison would make every lookup undefined; a sampler without one
still cannot filter depth.

Sampler binds follow the texture bind for a unit, because
`nt_gfx_bind_texture` installs the texture's own default sampler and discards
whatever the unit held. A comparison sampler bound to a unit with no live
texture is therefore rejected rather than silently replaced.

With comparison on, `LINEAR` filters the 0/1 comparison results instead of the
raw depths — the ordering a shadow edge needs, since averaging depths first
compares against a depth that exists in no texel. Both the desktop GL and the
GLES specifications leave the blend implementation-dependent and promise only a
value proportional to the passing comparisons, so consumers may rely on the
proportionality but not on specific weights. Comparison itself is core in
GLES 3.0, WebGL 2, and desktop GL 3.0+, so it needs no capability bit.

Sampler overrides with a mipmap minification filter require complete mip
storage for the bound texture. A 1x1 base level is already a complete chain.

This capability supplies low-level targets and depth textures only. It does not
define light cameras, PCF, cascades, shadow atlases, material shadow integration,
or a shadow-map system.

## Renderer complexity classes

Not all renderers carry the same weight. The engine ships three classes; copying patterns across classes is a common mistake.

**Building blocks** — direct GPU primitives (`nt_gfx_draw_indexed`, `nt_mesh_renderer`). Single pipeline, fixed pattern, one or more instanced draws per batch_key run — split at max_instances chunk boundaries (see items-sorting-batching.md). Use for 3D meshes, custom geometry, anything where the game owns batching strategy. Stay minimal.

**Batched dynamic** — high-throughput accumulation renderers (`nt_sprite_renderer`; future particles). Cmd queue, state-delta tracking, overflow recovery via snapshot/replay, multi-page atlas resolution, SIMD path. Optimized for many small draws per frame (1k–60k items). Complex by necessity — the 580 LOC of `nt_sprite_renderer.c` are paid for by measured throughput on bunnymark. Don't simplify away the cmd queue or snapshot recovery without a measured replacement plan.

**Specialized** — domain-specific layout (`nt_text_renderer` glyph atlas + line layout; future debug-line/IM-GUI). Sit between the two — more state than primitives, less throughput pressure than batched dynamic.

When adding a new renderer, classify first:

- One pipeline, fixed pattern → **building block** (model after `nt_mesh_renderer`)
- 1k+ items/frame with dynamic state → **batched dynamic** (study `nt_sprite_renderer`, but only copy what your throughput demands)
- Domain-specific layout/data → **specialized**

`nt_sprite_renderer.c` is not a renderer template. Its complexity earns its keep at 60k items/frame; a 100-item UI overlay doesn't need any of it.
