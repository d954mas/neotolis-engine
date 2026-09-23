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

GPU segment timing is an optional producer selected by
`NT_GFX_GPU_TIMING_ENABLED`, independent of metrics. Its compile-time and runtime
OFF contracts are in [optional measurements](../debug/logging-errors-debugging.md#optional-measurements).

Native GL diagnostics are selected explicitly by `NT_GFX_NATIVE_GL_DEBUG`.
When enabled, the window requests a debug context and the backend installs a
synchronous KHR_debug callback if the driver supports it. The callback logs
messages and asserts on GL errors using the configured `NT_ASSERT_MODE`.

`nt_gfx_make_texture` rejects dimensions above the GPU limit and unsupported
compressed formats with an error log and an invalid handle before creating GPU
storage. These are recoverable capability failures in every assert mode.

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

Vertex-input state is a public gfx object referenced by `nt_vertex_input_t`.
The caller that creates it owns the handle and destroys it with
`nt_gfx_destroy_vertex_input`; the object borrows its vertex and optional
index buffer handles. It bakes those buffers together with a vertex layout
and an optional per-instance layout (GL: a VAO). One
`nt_gfx_bind_vertex_input` selects the whole geometry for the following
draws; per mesh switch that is a single `glBindVertexArray` instead of
buffer re-binds plus per-attribute `glVertexAttribPointer` rewrites. The
object's *static* half — vertex attributes and the index binding — is
immutable after creation; its *instance* attribute pointers are re-specified
by each `nt_gfx_bind_instance_buffer` into the vertex input the front-end
names explicitly to the backend (WebGL2 has no
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
misaligned offsets/strides before any vertex input exists. Renderer-owned
vertex inputs are created on a cache miss and then reused, so validation is
absent from the steady-state hot path.

**Lifetime and the destroy cascade.** `nt_gfx_destroy_buffer` destroys every
live vertex input referencing that buffer as its vertex or index buffer
(precedent: destroying a program destroys its pipelines). Mesh deactivation
destroys the mesh's buffers and reaches no renderer, so this cascade is what
keeps renderer-cached vertex inputs from outliving mesh buffers; mesh caches
revalidate handles with `nt_gfx_vertex_input_valid` on lookup. Because the
cascade makes stale handles routine, `nt_gfx_destroy_vertex_input` tolerates
stale and INVALID handles as no-ops. The dynamically captured instance
buffer is *not* cascade-destroyed, but destroying one clears the dependents'
pointed flag: their next draw using that vertex input asserts until
`nt_gfx_bind_instance_buffer` re-points it, and the GL attachment's storage
lingers until that re-point or the vertex input's death.
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
explicitly. Applying a texture set containing a husk reports once and issues no
texture or sampler bind, and the draws of that set are skipped; binding a husk
buffer, or writing into any husk, asserts.

**Program / pipeline split.** A program is the linked (vertex, fragment) pair
and owns everything that follows from linking: uniform locations, uniform
values, and global UBO block bindings. A uniform write requires a bound
pipeline and addresses that pipeline's program at the backend boundary; a
write with nothing bound asserts. A pipeline is fixed-function render
state that *borrows* a program handle — it owns no vertex-input state, and
pipeline and vertex-input binding are orthogonal: either may change without
re-binding the other. Two pipelines on one program
share every uniform value, and binding one does not reset what the other set.
Renderers replay declared material params on each material or pipeline transition
inside one `draw_list` call or flush. Renderer-tracked bound state is discarded at the
end of that call; across calls the GL backend deduplicates program, VAO,
pipeline state, texture, sampler, viewport and clear-value binds (scissor-enable is deduplicated by the
front-end mirror). Standalone float vec4 writes are skipped when their bytes
match the last submitted value for that program and uniform; other uniform
writes are issued unchanged. The
backend GL cache persists across passes and frames; ground state is issued once
at backend init and at context restore. Sampler uniforms are not written at all:
their units are fixed at link and belong to the program, so no material can
redirect another's texture. `nt_gfx_apply_texture_bindings` accepts a complete
name-keyed set for the bound program, resolves it into those units,
then publishes the logical set and issues backend binds together.
A missing or duplicate active name, invalid handle, or sampler-type mismatch
asserts before backend binds; inactive names are ignored before their handles
are inspected. Context loss, a texture husk, or failed sampler recreation
publishes no set and issues no backend bind: gfx reports the failure and skips
the following draws of that set. A vec4 param a material
does not declare still retains the value last written on that program.
Before each draw, the game and renderer must ensure the program holds every
numeric uniform value that draw needs. Materials supply only their declared
params; any required value they omit must be explicitly established by the game
or renderer. The vec4 cache neither validates material completeness nor resets
omitted values. Destroying a program destroys its
pipelines, and a context loss frees every pipeline slot; renderers remove
dead cache records during insertion after a miss or when resetting their
caches.

**Cache identity.** Renderers key their pipeline caches on the exact
identity of the descriptor, `nt_gfx_pipeline_key_t` from
`nt_gfx_pipeline_key(desc)`: every enum and bool field packed bit-exact into
one 64-bit word (program handle, depth, cull, polygon-offset enable, blend
factors and ops) plus the float payloads (blend constant, polygon-offset
factor/units) that cannot pack. Equal keys mean identical pipelines; a hit
compares the packed word and, only when it matches, the floats. No hash is
involved, so identity does not rest on a collision argument. Two
canonicalisations apply, both in the packer: a disabled blend packs as opaque
(factors, ops and constant ignored) and a disabled polygon offset packs its
factor/units as zero; nothing else is normalised, so depth lanes are exact even
when depth test is off. Lane inputs are range-asserted: material-owned lanes at
`nt_material_create` and again in the packer, renderer-owned lanes (`depth_func`,
polygon offset) in the packer alone — unconditionally, a disabled blend included:
canonicalisation is about identity, validity has no exceptions. So an
out-of-range value can never truncate onto a valid neighbour and turn into a
cache hit. gfx owns the lane widths, and pins the desc size and field offsets
with static asserts as a partial tripwire: a new field that moves a later offset
trips it, one that fits a padding hole does not and must be added by hand. The
packer is header-inline, so every gfx composition (real, stub, test fake) carries it
with no link-order dependency. The cached pipelines borrow
programs the game owns, so a cache entry never extends a program's lifetime.
The population is distinct material states, tens of values in a real game, so
a fixed array with a linear scan stays cheaper than a hash map at that scale.

A linear fold is not an identity: nested folds hand the handle and some state
lane the same coefficient (`program.id*K + ... + cull*K`), so `(p, cull)` and
`(p+1, cull-1)` collide exactly, and pool handles are sequential, so that is
the common case, not a corner case. No renderer cache key may fold handles or enums that way: pack exactly,
or hash the whole canonical identity with `nt_hash64` where the identity is
content rather than a handful of small fields.

**State transitions.** The run-based renderers (mesh, skinned mesh, sprite)
drive one shared state machine, `nt_renderer_bound_t` in
`engine/renderers/nt_renderer_shared.h`.
It separates four transitions, each with its own identity: pipeline (handle),
vertex input (handle), material uniforms — every vec4 param, keyed by material
id — and the per-run instance range plus draw. A run that changes only the mesh
therefore does no material work at all. The tracked state lives for exactly one `draw_list` call or flush: inside
that call the renderer is the only writer of GL draw state. Material uniforms
replay on a material change *or* a pipeline change, because uniform values are
program state and the new pipeline may sit on another program — one flush can
hold one material id on two programs when a game replaces the material's program
between an immediate-mode emit and an ECS `draw_list`. The mesh renderer pays
nothing for this: a pipeline change there always implies a material change.
Texture and sampler travel together in one `nt_gfx_texture_binding_t`; a material
without an override selects the texture's asset default. At every material
transition the renderer resolves the material's declared `nt_resource_t` texture
handles, and every sprite command submits the complete semantic set once. The
skinned renderer replaces the declared `u_skin_matrices` resource and sampler
with the current deformation texture and its default sampler, resolves the other
declarations, and submits the same complete set. The skin declaration still
counts toward the material's four slots; there is no renderer-only fifth
binding. The sprite batch key packs the material pool slot and the currently published GPU
texture pool slot, resolved from the stable page resource index. Both bindings
remain live and unchanged from list construction through draw completion; a new
list resolves the current publication again. The
gfx front-end maps names to the bound program's canonical units,
ignores inactive declarations, validates complete active coverage, and calls the
backend only after the whole set resolves. The backend GL cache drops repeated
physical binds. The text renderer draws once per flush and other renderers draw
in between, so it also submits its complete set unconditionally.

The material-driven mesh, skinned mesh, sprite, and text renderer caches build the
`nt_pipeline_desc_t` from the material's render state and key on its
`nt_gfx_pipeline_key_t`. Layouts and `color_mode` live on vertex-input
objects, so materials differing only in layout or color mode share one
pipeline. The sprite renderer resolves the pipeline once per material change
inside a `draw_list` call, not once per run: runs also split per atlas page,
and nothing can replace a material's program inside the call.

Vertex-input caches use exact identity for *derived* layouts too. The mesh and
skinned mesh renderers each instantiate the shared internal per-mesh versions
cache from `nt_renderer_shared.h`; the tables are independent because their
instance layouts differ. Each row stores its mesh's full generation-checked
handle. A different generation clears the entire row, including bufferless
vertex inputs that have no destroy-cascade hook. Within the row the mesh's
stream types, counts, offsets and stride are fixed, so entry identity packs only
what varies: per stream a presence bit and the mapped location (mesh streams ×
material attr_map — attr_map entries matching no stream do not split; a
material mapping none of the streams derives an empty layout and takes the
attribute-less gl_VertexID path) plus the color mode that selects the instance
layout. The sprite renderer packs the attr_map count and every location the same
way. Handles are revalidated on lookup because buffer destruction can invalidate
cached versions. Exhausting a mesh's version row asserts, naming the knob —
silent eviction would hide VAO re-creation thrash as an invisible perf
regression. The default `max_vertex_inputs` budgets one mesh cache; a game using
both mesh renderers adds
`max_meshes * skinned.max_mesh_layouts` to that base budget explicitly.

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
state; pipeline write masks affect draws, not the next pass initialization. Bound
pipeline, vertex input, and the logical complete texture set are pass-scoped:
`begin_pass` discards them. The texture set is additionally tied to the bound
program and is discarded when that program changes or when the bound pipeline is
destroyed. Pipeline and vertex-input binds, texture-set application,
instance-buffer re-pointing, uniform writes and draws outside a pass assert.
Destroying a texture or a render target inside a pass asserts: pass-scoped draw
state may still sample it.
Physical texture/sampler GL bindings and uniform-buffer binds remain context
state. The backend deduplicates texture/sampler binds across passes;
uniform-buffer binding calls `glBindBufferBase` on every request. The clear forces the depth
mask on and leaves it on; the pass's first pipeline bind sets its own mask.

Render-target color and sampleable depth attachments are exposed as normal
`nt_texture_t` handles for later sampling. Sampling either attachment while its
target is the active pass would create a framebuffer feedback loop and asserts
before any backend bind. Backend FBO/renderbuffer ids stay private to the concrete
graphics implementation.

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

A texture and the sampler it is read through form one semantic binding, so the
sampler is validated against that texture and not against whatever the unit held;
`NT_SAMPLER_DEFAULT` selects the texture's own default. A comparison sampler is
therefore rejected against a non-depth texture in the same call, and a unit never
holds a texture without its sampler.

With comparison on, `LINEAR` filters the 0/1 comparison results instead of the
raw depths — the ordering a shadow edge needs, since averaging depths first
compares against a depth that exists in no texel. Both the desktop GL and the
GLES specifications leave the blend implementation-dependent and promise only a
value proportional to the passing comparisons, so consumers may rely on the
proportionality but not on specific weights. Comparison itself is core in
GLES 3.0, WebGL 2, and desktop GL 3.0+, so it needs no capability bit.

Mip completeness needs no bind-time gate: `GL_TEXTURE_MAX_LEVEL` is set to
`mip_count - 1` when the storage is created, so a texture's levels `0..MAX_LEVEL`
all exist by the time its handle is published. A sampler override with a mipmap
minification filter is therefore always valid, and over a single-level texture
it samples level 0.

Block-compressed storage (`ETC2_RGB8`, `ETC2_RGBA8`, `BC7_RGBA`,
`ASTC_4x4_RGBA`) is normalized color for the sampler classes: it satisfies
`sampler2D`, and the comparison and integer classes reject it — comparison needs
depth storage, `usampler2D` needs integer storage.

`gpu_caps.has_float_texture_linear` exposes `OES_texture_float_linear` on WebGL 2
and core float filtering on desktop GL. It is probed and enabled at initialization
and context restore, alongside the other GPU capabilities. `RGBA32F` texture
defaults and sampler overrides require it for any linear filtering. Without it,
both allow `NEAREST` or `NEAREST_MIPMAP_NEAREST` minification and require
`NEAREST` magnification. Unsupported filter choices assert without silently
changing the requested sampler.
RGBA32F mipmap generation additionally requires `has_float_render_target`:
WebGL requires the source storage to be both filterable and color-renderable.
This does not add RGBA32F render-target support to the engine.

This capability supplies low-level targets and depth textures only. It does not
define light cameras, PCF, cascades, shadow atlases, material shadow integration,
or a shadow-map system.

## Frame observation

The host may bracket one callback with `nt_gfx_observe_begin_frame` and
`nt_gfx_observe_end_frame`, before resource preparation and after rendering.
These are diagnostic boundaries, independent of render begin/end and simulation
time. Each interval contains zero or one gfx frame and any number of passes.
Calls require gfx IDLE, except that known context loss permits finalization.
Missing/nested boundaries assert when a counters or capture producer is compiled
in; OFF/stub boundaries are inert. No host wiring is added implicitly by app/gfx.

`nt_gfx_stats_read` returns current counters by value; outside the interval or
with counters disabled they are unavailable. Two reads with the same sequence
measure an interval by field subtraction. Stage totals are subsets of the whole
frame, not additional totals. The bitset distinguishes frontend observations
from real backend observations; the test fake cannot claim measured GL calls.

End returns a borrowed POD snapshot that stays unchanged until the next end
or shutdown. Copy it by value for caller-owned history. A no-render callback
still produces a new sequence and zero draws; old geometry is never reused.
Existing `frame_stats` remains the only live draw/geometry source, reset by
gfx begin_frame; `nt_gfx_get_frame_draw_calls` retains its uint32 live contract.
Geometry and instance fields are always uint64, independent of counter options;
operands widen to uint64 before multiplication and accumulation asserts overflow.
Vertices/indices are submitted counts, multiplied by instance count for
instanced calls; instances counts only instances in instanced calls. These are
not rasterized triangles or vertex-shader invocations.

`NT_GFX_COUNTERS_ENABLED` and `NT_GFX_CAPTURE_ENABLED` are independent numeric
interface definitions. Counters start enabled when compiled in. A stats toggle
inside an interval takes effect at the next begin; outside it applies immediately.
OFF/stub reads are explicitly unavailable. Counter widths/flags are published
by the interface target so every consumer uses the same configuration.

`nt_gfx_upload_totals_read` exposes lifetime CPU payload calls/bytes, including
work outside observation intervals. Deltas require availability and equal
epochs within one gfx initialization lifetime. Sequence, context and epoch
identifiers reset at initialization. Re-enabling counters starts a new epoch. NULL-data storage and generated
mips are excluded; non-NULL orphaning counts once. Texture bytes use the actual
GPU format for each mip/subrectangle. Failed creates retain already-issued work.
Per-frame payload fields subtract the begin baseline from these canonical totals.
Bind requests count accepted frontend operations; call fields count actual GL
calls, including temporary program, service VAO and upload texture bindings.
Requests minus calls is not a cache-skip count. Uniform calls include link-time
sampler assignments and uniform-block binding assignments. UBO binds are unconditional; attribute counters count pointer
specifications for static/instance layouts respectively.

Command recording starts disabled. `nt_gfx_desc_t.capture_capacity` reserves one
event array at init (default zero); enabling capture without capacity asserts.
There is no growth or allocation while recording. Each pointer-free POD event
is 112 bytes, including padding; 16384 records reserve 1.75 MiB. Other storage
consists of fixed control state and counter snapshots, with no second event array.
All record bytes are initialized before publication. Every recorded begin first
snapshots inherited state, including one definition per live resource (plus
program uniform/sampler and vertex-input attribute records), into the same array.
Size the capacity for that snapshot plus the frame's commands; a capacity below
the snapshot overflows before any command is recorded.

`nt_gfx_capture_read` returns metadata by value and an immutable event prefix.
The prefix remains valid until the next **recorded** begin or shutdown; frames
with recording disabled preserve it. Two counts in the same sequence delimit
an operation interval. Keep a capture by copying the metadata and `count` records
and redirecting the saved view's pointer to the owned array. An empty view has
a NULL pointer. The finalized view retains its matching counter snapshot by value
even after subsequent counters-only frames overwrite the module's last snapshot.

BEGIN/RESULT records delimit nested operations. `ARGUMENT` records are request
arguments belonging to the enclosing BEGIN (one per texture binding of a texture
set); `DEFINITION` is reserved for resource and inherited state. Issued backend calls do not
prove GL success or GPU completion. Metadata distinguishes recording from
finalized, complete, truncated and aborted captures. An interval is aborted only
when a loss is observed during it or the context is still lost at its end; an
interval whose gfx begin_frame restores a previously lost context and then
completes is complete. Overflow is separately
reported even when aborted, stops event appends, and never truncates counters.
Runtime recording changes during observation apply next begin.

The `object_kind` and `object` pair identifies a full frontend handle, including
its generation. Backend records instead use `detail` as `nt_gfx_gl_call_t` and
carry raw GL names scoped to `context_sequence`. `backend.args` follows the GL
integer argument order; pointer payload, readback output and debug-label arguments
are presence bits, gen/delete arguments contain the count followed by each name,
and indexed offsets are byte offsets. Readback, timer-query and debug-group calls
are issued calls too and are recorded like any other.
Float arguments occupy `backend.values` in float argument order. Matrix and vec4
calls use `uniform` with the location in `name`, float count in `count`, and
copied values. `backend.bytes` is actual CPU upload payload, zero for NULL storage.
No event borrows upload memory, shader source or caller labels.

Resource `DEFINITION/STATE` records with `object_kind=NONE` use `detail` as the
resource kind and `backend.args[0..1]` as backend slot/raw GL name; render targets
also supply the depth renderbuffer name at index 2. Frontend resource definitions
carry the full handle, current backend slot and available dimensions/relationships.
Replacement names and surviving handles receive fresh definitions on resize or
restore. Definitions remain meaningful after resource destruction or slot reuse.

Program publication and initial state include `INITIAL/SAMPLER` records with
backend program slot, name hash, location, unit and sampler class in args 0–4.
`INITIAL/UNIFORM_VEC4` gives program slot/name hash/location in args 0–2 and cached
vec4 values; `UNKNOWN` means no retained value. These INITIAL records can occur
inside CREATE when the program first becomes available. Inactive names emit
SKIP/INACTIVE; cache skips are distinct from invalid requests.

Pipeline state records use integers 0–12 for program, depth enable/write/function,
cull, blend enable, RGB source/destination, alpha source/destination, RGB/alpha
operation and polygon offset enable. Values 0–5 hold blend color, offset factor
and units. Frontend definitions use full handles and frontend enums; backend
definitions use slots and backend enums; initial state uses the current raw
program name. Vertex-input creation copies each static/instance attribute with
its divisor, layout, and known buffer. Inherited layouts/UBO bindings/scissor
rectangles unavailable in existing CPU state are explicitly unknown. Capture
never adds a persistent GL-state mirror or queries GL to reconstruct them.

## Renderer complexity classes

Not all renderers carry the same weight. The engine ships three classes; copying patterns across classes is a common mistake.

**Building blocks** — direct GPU primitives (`nt_gfx_draw_indexed`,
`nt_mesh_renderer`, optional `nt_skinned_mesh_renderer`). Single pipeline, fixed
pattern, one or more instanced draws per compatible run — split at
`max_instances` chunk boundaries (see items-sorting-batching.md). Use for 3D
meshes, custom geometry, anything where the game owns batching strategy. Stay
minimal. The mesh renderers do state-delta tracking through the shared
`static inline` helper, which costs them no cmd queue and no snapshot machinery.

**Batched dynamic** — high-throughput accumulation renderers (`nt_sprite_renderer`; future particles). Cmd queue, state-delta tracking, overflow recovery via snapshot/replay, multi-page atlas resolution, SIMD path. Optimized for many small draws per frame (1k–60k items). Complex by necessity — the 580 LOC of `nt_sprite_renderer.c` are paid for by measured throughput on bunnymark. Don't simplify away the cmd queue or snapshot recovery without a measured replacement plan.

**Specialized** — domain-specific layout (`nt_text_renderer` glyph atlas + line layout; future debug-line/IM-GUI). Sit between the two — more state than primitives, less throughput pressure than batched dynamic.

When adding a new renderer, classify first:

- One pipeline, fixed pattern → **building block** (model after `nt_mesh_renderer`)
- 1k+ items/frame with dynamic state → **batched dynamic** (study `nt_sprite_renderer`, but only copy what your throughput demands)
- Domain-specific layout/data → **specialized**

`nt_sprite_renderer.c` is not a renderer template. Its complexity earns its keep at 60k items/frame; a 100-item UI overlay doesn't need any of it.
