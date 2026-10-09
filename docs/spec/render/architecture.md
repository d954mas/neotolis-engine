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

renderer_begin_pass(&desc);
renderer_end_pass();

renderer_set_camera(&camera);

renderer_draw_mesh(...);
renderer_draw_sprite(...);
```

### Draw-phase command stream

Draw-phase calls are deferred. At the call, the front-end validates and updates
its logical state and geometry counters, then records the
backend-resolved arguments of the backend call into one command stream: begin
and end pass, clear, pipeline, vertex-input and instance-buffer binds,
texture-unit and uniform-block binds, the mat4, vec4, float
and int uniform setters, scissor rectangle and enable, viewport, the plain and
indexed draws (both carry an instance count; the indexed draw also carries the
index type of the bound vertex input), and GPU timing
segment begin and end. Descriptors and uniform values are copied
into the stream. A binding equal to the current one and an indexed draw that
continues the previous one record nothing new (see Binding dedup and draw
merge). `nt_gfx_end_frame` first uploads the frame storage (see Frame storage), then
executes the stream in call order. Nothing is recorded outside a frame.

Every other operation is immediate: creates, destroys, buffer and texture
updates, activation, queries, the GPU timing toggle and polling.
`nt_gfx_end_frame` is the only place the stream executes, so every immediate
operation issued before `nt_gfx_end_frame` runs before every recorded command of
that frame. Two
rules follow from that order.

**Writes follow queue semantics**, like WebGPU `queue.writeBuffer` and
`queue.writeTexture` before `submit`. `nt_gfx_update_buffer` and
`nt_gfx_update_texture` are allowed at any time and unchecked. A write issued
before `nt_gfx_end_frame` lands before the frame's first recorded command, so
every draw of the frame reads the last write: writing one region twice in a
frame gives the second content to every draw, including draws recorded before
it. A write issued after `nt_gfx_end_frame` belongs to the next frame. Rendering
into an attachment is recorded, so it still overwrites the attachment in pass
order. Arguments of recorded calls (uniform values, descriptors, uniform blocks)
are copied at the call; buffer, texture and frame storage contents are read at
execution.

**The frame rule.** A frame is *being drawn* from its first `nt_gfx_begin_pass`
until `nt_gfx_end_frame`. While it is being drawn, destroying a live object
asserts: every `nt_gfx_destroy_*` and `nt_gfx_deactivate_*`, and so every
module shutdown, unmount or restore that destroys GPU objects, because a recorded
command may still name it. Before the first pass and after `nt_gfx_end_frame`
destroys are allowed; a destroy of a stale or invalid handle keeps its own
contract. A lost frame follows the same order: `nt_gfx_begin_pass` on a lost
context records nothing but still opens the pass, and every pass-scoped call
checks the pass before it returns `NT_GFX_RESULT_CONTEXT_LOST` (segments check the frame). The check is one compare per
destroy; draws pay nothing.

`nt_gfx_set_gpu_timing_enabled` runs only between frames: disabling closes an
active timer segment at the call, while the segments a frame records execute in
its `nt_gfx_end_frame`, so a toggle inside a frame could split them.

`nt_gfx_desc_t.stream_capacity` is the byte budget of the draw-phase commands of
one frame, allocated once at init; `nt_gfx_desc_defaults()` sets 32 KiB (over three times
the largest measured frame, Sponza at 8.9 KB; a mesh run records 60-100 bytes),
and init asserts at least 4 bytes. The stream never grows: an overflow logs the
needed and free bytes and stops the program because
the capacity is the game's budget. `nt_gfx_counters_t.stream_bytes` reports the
bytes the frame recorded, to size the capacity from a real scene.

GL `begin_pass`
reads the window framebuffer size at execution, which equals the size at the
call: the window size changes only in `nt_window_poll`, between frames. GL errors
and backend asserts without a front-end equivalent fire at execution, inside
`nt_gfx_end_frame`; see Frame observation for counters and capture.

### Binding dedup and draw merge

The front-end is the one layer that drops redundant public binds; the GL backend
only skips repeated physical state (below). Every binding call except a uniform
block compares with a front-end mirror; an equal value ends `NT_GFX_RESULT_CACHE`
and records nothing. A mirror lives exactly as long as the contract keeps its state:

- pipeline, vertex input with its instance stream and offset, the texture set
  (per unit: texture and sampler) and the viewport live for one pass:
  `begin_pass` discards them, so the first bind of each in a pass records;
- the scissor rectangle carries over passes and frames; a context loss clears it;
- scissor enable is reset to off by `begin_pass`.

`nt_gfx_apply_texture_bindings` never ends `CACHE`: a successful apply validates
and publishes the whole set and ends `ACCEPTED`, but records a unit bind only
when that unit's texture or sampler changed in the pass. A uniform block is
frame data at a fresh offset of the uniform frame stream, so its bind always
records and ends `ACCEPTED`; GL keeps the slot binding across passes and frames.
Uniform-block slots are below `NT_GFX_MAX_UNIFORM_BUFFER_SLOTS` (24, the WebGL2 minimum); a bind at
that slot or above asserts, and so does `nt_gfx_init` for a global block declared there.

The compare runs after the pass check; an equal value was validated when it was
recorded, `begin_pass` discards the pass-scoped mirrors, and the frame rule keeps
every recorded object alive until `nt_gfx_end_frame`. An invalid
pipeline or vertex-input handle clears its mirror (the unbind); other invalid
binds leave their mirrors unchanged. Uniform values are not deduplicated by
the front-end. The GL backend keeps caches for
physical GL state the front-end does not name: the program and VAO behind
different pipelines and vertex inputs, the fixed-function difference between
pipelines, the texture and sampler halves of a unit across passes, the
viewport, clear values, the active unit, the `GL_ARRAY_BUFFER` binding, and the
instance offset each VAO of a vertex input last received: a recorded instanced
bind that repeats its VAO's stream and offset issues no `glBindBuffer` or
`glVertexAttribPointer` (only the VAO bind, when another VAO is bound), so a
stream whose layout repeats from frame to frame keeps its pointers.

Draws are whole triangle lists: every draw asserts that its vertex or index
count is a multiple of 3, index data never holds the primitive-restart value
(`0xFFFF`/`0xFFFFFFFF`; WebGL2 always restarts on it, native GL draws that
vertex), and shaders follow the [draw merge rule](shader.md#draw-merge).
`nt_gfx_draw` and `nt_gfx_draw_indexed` therefore extend the previous command
when that command is the last one recorded, was recorded by the same function,
and its range of vertices or indices ends where the new one starts (the summed
count fits `GLsizei`). The merged call ends `CACHE` with its vertices and
indices counted, so
`nt_gfx_draw_calls()` counts recorded draws. An execution of the stream ends the
merge chain. Instanced draws never merge: joining them changes
`gl_InstanceID`. A merge joins only draws adjacent in call order with no state
change between them, so the picture is unchanged and the game still decides
what is adjacent.

### Vertex inputs

Vertex-input state is a public gfx object referenced by `nt_vertex_input_t`.
The caller that creates it owns the handle and destroys it with
`nt_gfx_destroy_vertex_input`; the object borrows its vertex and optional
index buffer handles. It bakes those buffers together with a vertex layout
and an optional per-instance layout (GL: a VAO). One bind selects the whole
geometry for the following draws: `nt_gfx_bind_vertex_input(vi)` for a vertex
input without an instance layout, `nt_gfx_bind_vertex_input_instanced(vi,
stream, offset)` for one with an instance layout, its instances at byte
`offset` of frame vertex stream `stream` (see Frame storage); each form
asserts the layout it binds. Per mesh switch that is a single
`glBindVertexArray` instead of buffer re-binds plus per-attribute
`glVertexAttribPointer` rewrites. The object's *static* half — vertex
attributes and the index binding — is immutable after creation. WebGL2 has no
baseInstance, so instances are located by pointing the instance attributes at
their offset. The GL backend keeps one VAO per frame vertex stream a vertex
input is bound with: the first bind with a stream builds that VAO from the
stored layouts, and each VAO keeps the pointers of its last offset in its
stream. A pass whose instances live in their own stream keeps every run's
offset, and so its pointers, while its counts stay; in one shared stream a
count change shifts every later run of the frame. A VAO holds one offset: a
vertex input drawn at two offsets of one stream in a frame re-points between
them every frame. A vertex input with
instance attributes is always bound together with its instance source, so no
draw reads unpointed attributes. An empty layout with no buffers is the
attribute-less `gl_VertexID` path; every draw asserts a bound vertex input.

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
stale and INVALID handles as no-ops. Instance attributes read frame storage,
whose buffers only a context restore or shutdown replaces, and both free every
vertex input.
Buffer *contents* may change at any time (queue semantics, see Draw-phase
command stream) — an update keeps the GL name, so baked attachments survive it;
what a write costs depends on when it happens (see Dynamic data lifetime) — and
index-buffer data ops run inside a service upload VAO in the backend,
because the element-array binding is VAO state — it would otherwise be
silently rewired into whichever vertex input is bound, and core-profile GL
rejects the bind with VAO 0. Vertex inputs die with a lost context
and are not auto-restored: loss itself frees their pool slots, so a handle
held across a loss goes stale and `nt_gfx_vertex_input_valid` reports false.
Renderer restore paths recreate them; caches validate on lookup and
self-heal. This is the rule for baked objects — pipelines, vertex
inputs and render targets, all assembled from other handles with no re-fill
path: a context loss frees their pool slots outright. Primary resources (buffers, textures,
shaders, programs) instead survive a loss as husks — pool slot alive,
backend gone — because per-frame code keeps operating on them through the
loss window and the restore recipe has their owners destroy the old handles
explicitly. Applying a texture set containing a husk reports once and issues no
texture or sampler bind, and the draws of that set are skipped; binding a husk
buffer, or writing into any husk, asserts. Destroying handle 0 is a silent no-op
for every primary type, so an owner whose create met a loss destroys without a check.

**Program / pipeline split.** A program is the linked (vertex, fragment) pair
and owns everything that follows from linking: uniform locations, uniform
values, and global UBO block bindings. A uniform write requires a bound
pipeline and addresses that pipeline's program at the backend boundary; a
write with nothing bound asserts. A pipeline is fixed-function render
state that *borrows* a program handle — it owns no vertex-input state, and
pipeline and vertex-input binding are orthogonal: either may change without
re-binding the other. Two pipelines on one program
share every uniform value, and binding one does not reset what the other set.
The mesh renderers replay declared material params on each material transition inside one draw
call and discard that tracking at the end of the call; across calls the front-end drops equal binds and the GL backend
drops repeated physical state (see Binding dedup and draw merge). The sprite
renderer instead writes a material's params only when the program, the material or
the param values differ from what it last wrote, compared at every emit and run,
because a uniform record between two draws stops them from merging. That memo
lasts across calls and frames, so nothing but the sprite renderer may write the
uniforms of a program that sprite materials use. Standalone float vec4 writes are skipped when their bytes
match the last submitted value for that program and uniform; other uniform
writes are issued unchanged. The
backend GL cache persists across passes and frames; ground state is issued once
at backend init and at context restore. Sampler uniforms are not written at all:
their units are fixed at link and belong to the program, so no material can
redirect another's texture. `nt_gfx_apply_texture_bindings` accepts a complete
name-keyed set for the bound program, resolves it into those units,
then publishes the logical set and records its texture-unit binds together.
A missing or duplicate active name, invalid handle, or sampler-type mismatch
asserts before any bind is recorded; inactive names are ignored before their handles
are inspected. Context loss, a texture husk, or failed sampler recreation
publishes no set and records no texture-unit bind: gfx reports the failure and skips
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

**State transitions.** The sprite renderer keeps no bind tracking: every
immediate emit and every `draw_list` run binds its pipeline, texture set and
vertex input and records its draw, and the front-end drops the equal binds and
merges the contiguous draws. Its one memo is the params memo above, keyed by the
program the pipeline was built on, so a program replaced behind an unchanged
material handle writes its params again.

The mesh renderers keep no bind tracking: every run binds its pipeline and
vertex input, and the front-end drops the equal ones. They skip only material
work — the vec4 param loop and the texture set — between runs of one call that
keep the material. In the skinned renderer a deformation-only change applies the
texture set, not the params. Inside one call the pipeline follows from the material, so a program change is
a material change. A core draw call applies its material every time.
Texture and sampler travel together in one `nt_gfx_texture_binding_t`; a material
without an override selects the texture's asset default. At every material
transition the renderer resolves the material's declared `nt_resource_t` texture
handles, and every sprite emit and run submits the complete semantic set. The
skinned renderer replaces the declared `u_skin_matrices` resource and sampler
with the run's deformation texture and its default sampler, resolves the other
declarations, and submits the same complete set. The skin declaration still
counts toward the material's four slots; there is no renderer-only fifth
binding. The sprite batch key packs the material pool slot and the currently published GPU
texture pool slot, resolved from the stable page resource index; a run draws with
its first item's page. Both bindings
remain live and unchanged from list construction through draw completion; a new
list resolves the current publication again. The
gfx front-end maps names to the bound program's canonical units,
ignores inactive declarations, validates complete active coverage, and calls the
backend only after the whole set resolves, recording only the units whose
texture or sampler changed in the pass. The text renderer records its font's
complete set with every draw; gfx drops it when unchanged, so consecutive draws
of one font and material merge.

The material-driven mesh, skinned mesh, sprite, and text renderer caches build the
`nt_pipeline_desc_t` from the material's render state and key on its
`nt_gfx_pipeline_key_t`. Layouts live on vertex-input objects, so materials
differing only in layout share one pipeline. The sprite renderer resolves the pipeline once per material change
inside a `draw_list` call, not once per run: runs also split per atlas page,
and nothing can replace a material's program inside the call.

Vertex-input caches use exact identity for *derived* layouts too. The mesh and
skinned mesh renderers each instantiate the shared internal per-mesh versions
cache from `nt_renderer_shared.h`; the tables are independent because their
instance layouts differ. Each row stores its mesh's full generation-checked
handle. A different generation zeroes the row and destroys nothing: the old
mesh's deactivation destroyed its buffers, and the cascade took every version
built on its VBO or IBO. The one version the cascade cannot reach, an empty
derived layout on a non-indexed mesh (no VBO, no IBO), holds nothing
mesh-specific, so each renderer creates one and every row that needs it shares
it; a cache reset destroys it once. Within the row the mesh's
stream types, counts, offsets and stride are fixed, so entry identity packs only
what varies: per stream a presence bit and the mapped location (mesh streams ×
material attr_map — attr_map entries matching no stream do not split; a
material mapping none of the streams derives an empty layout and takes the
attribute-less gl_VertexID path); each renderer has one fixed instance layout.
The sprite renderer packs the attr_map count and every location the same
way. Handles are revalidated on lookup because buffer destruction can invalidate
cached versions. Exhausting a mesh's version row asserts, naming the knob —
silent eviction would hide VAO re-creation thrash as an invisible perf
regression. The default `max_vertex_inputs` budgets one mesh cache; a game using
both mesh renderers adds
`max_meshes * skinned.max_mesh_layouts` to that base budget explicitly.

The sprite renderer's vertex inputs sit on the vertex and index frame buffers,
one per layout key; `nt_sprite_renderer_shutdown` destroys them, and it runs
before `nt_gfx_shutdown` or a gfx re-init, whose new pools reuse handle ids.
Cache entries are weak: a hit validates the handle, and an entry whose
vertex input died (context loss) is recreated in place over the new frame
buffers, so repeated losses cannot grow the cache and no restore call is needed. A miss creates the vertex input and caches it only on
success; recoverable creation failures leave the cache unchanged so the next
lookup retries. The text renderer has one fixed layout and keeps a single vertex
input under the same rules; `nt_text_renderer_shutdown` destroys it.

### Color

A tint — a color that multiplies or replaces what a draw shows — is a packed
`uint32_t` `0xAABBGGRR` everywhere in the engine API. It is straight alpha and
reaches the GPU as normalized RGBA8 (vertices and instances alike). Literals use
`NT_RGBA8(r, g, b, a)` with integer bytes 0..255; float color math lives in
`engine/color/nt_color.h` and packs once at the end. Values are display
(sRGB-encoded) colors; shaders use them without conversion. Values that are not
a tint stay float: render-target clear colors, the blend constant color, material
uniform params (where an unclamped or HDR tint belongs) and lighting. Clay's own
declarations (`backgroundColor`, `border.color`, a raw `CLAY_TEXT` color) keep
Clay's `Clay_Color` (0..255 floats); the UI walker packs them when it emits.
Float math packs once per stage (an opacity fold, then an effect), so a chain of
stages can differ from a single float product by one step per stage. Every
float-to-byte conversion saturates and rounds half up (NaN gives 0), so an
opacity fold gives the same alpha on a packed color and on a Clay color with the
same byte values.

Every mesh and skinned mesh instance carries the entity's drawable color
(`nt_mesh_instance_t`, `nt_skinned_mesh_instance_t`), so every render item needs
a drawable component. A shader that ignores color does not declare the color
input; the instance layout still provides it. Instance locations are reserved:
4–7 for meshes (world rows, color) and 10–15 for skinned meshes (world rows,
color, frame origins, alpha). A material attribute derived at one of them
asserts when the vertex input is created; an attr_map entry that matches no
mesh stream derives nothing and is not checked. The engine sets no generic
(constant) vertex attribute value.

### Dynamic data lifetime

A write into a buffer that an earlier draw of the same frame read is correct
but not free: Mali drivers under ANGLE track the whole buffer, not the written
range, so the write waits for those draws or copies around them. Measured on
the reference phone:

- appending per-draw data between draws of one frame costs 2-15x frame time when the frame is
  not GPU-bound;
- a partial rewrite from offset 0 stalls the same way;
- a full-size rewrite does not stall: Chrome gives the buffer new storage;
- rewriting a buffer one frame after its last read does not stall;
- orphaning (`glBufferData` per write) removes the wait but allocates storage on
  every call.

Policy: the stream executes once per frame, in `nt_gfx_end_frame`, so no write
ever lands between draws of one frame: every write of a frame precedes all its
draws and lands one frame after the previous frame read the buffer. Per-frame
data lives in frame storage (below): it is written to CPU staging at any point
of the frame and reaches its buffer in one upload per stream, before the replay.
The sprite, text and shape renderers write their geometry to frame storage, so a
frame drawn by engine renderers alone uploads it once, in `nt_gfx_end_frame`.

A wait is a timing cost, not lost GPU throughput. In a GPU-bound frame the
waits did not raise GPU work per frame, and the phone's governor granted the
waiting build a higher clock. Compare builds as described in
[measuring performance on phones](../../perf-measurement.md).

### Frame storage

Frame storage holds one frame's per-draw data for engine renderers and the
game alike, in streams named `NT_GFX_FRAME_*` (`uint32_t` in the API), each a CPU staging copy
plus one `STREAM` buffer:

- `NT_GFX_FRAME_INDEX`: `uint32_t` indices (`NT_INDEX_UINT32`); WebGL never
  binds an element buffer as anything else, so indices have their own buffer;
- `NT_GFX_FRAME_UNIFORM`: view blocks and other per-frame uniform data;
- `NT_GFX_FRAME_VERTEX + k`, `k < NT_GFX_MAX_VERTEX_STREAMS` (CMake, default
  8): vertex and instance data. `NT_GFX_FRAME_VERTEX` is the general stream:
  sprites, text and the shape triangle batch use it, and frame indices are
  absolute into it.

The vertex streams come last in the enum, so the game names its streams with
its own enum starting at `NT_GFX_FRAME_VERTEX`, and a stream past the range
fails the stream assert. Several vertex streams exist for instance layout:
every stream starts at offset 0 each frame, so a pass that writes its
instances to its own stream keeps their offsets, and the GL backend its
instance pointers, while that pass's counts stay (see Vertex inputs). A stream
belongs to its allocations, not to a pass: a draw names the stream its
instances were allocated from, and one allocation may serve several passes.

One `STREAM` buffer per stream, rewritten from offset 0 every frame, is the
policy the phone measurements selected: rotating several buffers showed no
consistent benefit. The API does not guarantee a stall-free upload.

`nt_gfx_frame_alloc(stream, size, align, &offset)` returns `size` bytes of
staging at an offset that is a multiple of `align`; both are nonzero. It is
inline and touches no buffer and no recorded command, so it is legal at any
point between `nt_gfx_begin_frame` and `nt_gfx_end_frame`, in a pass or after UI
layout, and never ends a draw merge. `end_frame` sends everything allocated in
the frame; bytes allocated after it would never reach the GPU, so the next
`begin_frame` asserts on them.
`nt_gfx_end_frame` sends each stream's bytes with one buffer update, then
replays: fill an allocation before `nt_gfx_end_frame`. Offsets and pointers stay
valid until the next `nt_gfx_begin_frame`, which empties the storage. Alignment padding is
zeroed once at init and keeps old payload bytes afterwards; consumers read only
the bytes they allocated.

Alignment follows the reader:

- vertex data read by index aligns to its stride, so vertex `i` of an
  allocation sits at `i * stride` from the buffer start and indices are
  absolute: WebGL 2 has no `baseVertex`, and a vertex input binds its vertex
  buffer at offset 0. Consecutive allocations of one stride are contiguous;
- instance data bound with `nt_gfx_bind_vertex_input_instanced` at its offset aligns to 4;
- indices align to 4 and are drawn from `first_index = offset / 4`;
- uniform data is written and bound by `nt_gfx_bind_uniform_block(slot, data, size)`,
  which aligns it to `gpu_caps.uniform_buffer_offset_alignment` (at most 256 on
  WebGL 2 and GLES 3, not necessarily a power of two): budget each block its size
  plus 256 bytes. The game owns no uniform buffer; the GL binding persists, the bytes live for one frame, so a draw
  reads only blocks bound in its own frame.

`nt_gfx_frame_buffer(stream)` returns an ordinary buffer handle, for vertex inputs
over the general stream and game-side binds; `nt_gfx_bind_uniform_block` and
`nt_gfx_bind_vertex_input_instanced` name streams instead of buffers. The handle is borrowed: never update
or destroy it, and read it again every frame, because a context restore
replaces it (vertex inputs over it die with the context anyway). Each enabled
stream's buffer counts against `nt_gfx_desc_t.max_buffers`.

`nt_gfx_desc_t.frame_capacity[stream]` is the byte budget of a stream per frame,
allocated once at init as staging plus buffer; `nt_gfx_desc_defaults()` leaves every
stream at 0, so the game sets the budget of each stream it uses. A zero
capacity disables the stream: no staging and no buffer, and its allocations stop the program as an
overflow, so a game pays only for the streams it uses.
Storage never grows: an overflow logs the stream, the needed and the free bytes
and stops the program because the capacity is the
game's budget. `nt_gfx_counters_t.frame_bytes` reports each stream's use of the
frame, final after `end_frame`, to size the capacities from a real scene.
Text uses the general vertex stream and INDEX: 208 bytes of vertices and 24
bytes of indices per glyph quad, decoration quads included.

Each upload is one `NT_GFX_OP_BUFFER_UPLOAD` operation on its frame buffer,
recorded and counted where the execution runs (see Frame observation). `nt_gfx_end_frame`
uploads also when the frame recorded no command, so it always leaves the
storage sent. An upload goes through the same checks as
`nt_gfx_update_buffer`: while the context is lost it ends `CONTEXT_LOST`, and the
begin_frame that restores the context makes new buffers, which that frame's data
reaches. A frame buffer that cannot be made asserts unless the context is lost. `nt_gfx_stub` has zero capacity: every allocation
asserts.

### Mesh draws

`nt_mesh_renderer` and `nt_skinned_mesh_renderer` record their draws at the
call, in the current pass. Each has two entry points:

- the core, `nt_mesh_renderer_draw(mesh, material, stream, offset, count)`
  (skinned: `nt_skinned_mesh_renderer_draw(mesh, material, deformation, stream,
  offset, count)`), records one instanced draw of `count` instances
  (`nt_mesh_instance_t`, `nt_skinned_mesh_instance_t`) that the caller wrote
  into frame vertex stream `stream` at byte `offset`. It reads no entity
  component, and asserts that the instances lie inside the stream's
  allocations of the frame (bounds only: the caller owns the stream choice);
- the ECS adapter, `draw_list(stream, items, count)`, splits the items into
  runs of adjacent equal batch keys (the skinned renderer also splits on the
  deformation texture), allocates and packs each run's instances in `stream`
  from the transform, drawable (and skin) components, and records it as the
  core does.

The core's instances are filled before `nt_gfx_end_frame` (see Frame
storage). One allocation may be drawn any number of times in any passes of the
frame, with the stream it was allocated from, so shadow cascades can draw one
packing: every pass binds each vertex input at the same offset, so the
pointers stay. Giving each pass whose counts change its own stream is the
layout that keeps the other passes' offsets, and so their pointers, from frame
to frame. Each core call resolves pipeline,
vertex input and material state: draw a batch per call, not one object.
`nt_mesh_instance_world_rows` writes the instance rows of both instance types
from a column-major world matrix.

 A run whose program is not ready, or whose
pipeline or vertex input could not be created (load, context loss), records
nothing and allocates nothing. Both resolve the
pipeline and vertex input at the call — creating them on a cache miss — and
read the material's params and texture publications there. Consequences:

- Batching happens in `draw_list`, so the game's item order decides what merges.
- Entity bindings are read at the call, so one entity can enter several lists
  with different materials (multipass) by rebinding between the calls.
- The material is read at the call; its program and textures and the mesh stay
  live until `nt_gfx_end_frame` (the frame rule).

### Frame order

One canonical order covers deformation palettes (`nt_skeletal_gpu`), instance
data and view uniform blocks. `nt_gfx_begin_frame` empties frame storage;
`nt_skeletal_gpu_begin_frame` runs once per gfx frame after it, and the
palette flush precedes the first skinned draw:

```c
nt_gfx_begin_frame();
nt_skeletal_gpu_begin_frame();

/* Palettes, then one flush before the first skinned draw samples them. */
for (uint32_t i = 0; i < character_count; i++) {
    nt_skeletal_mat34_t *palette = nt_skeletal_gpu_reserve(palette_count, nt_skin_comp_handle(characters[i]));
    nt_skin_palette_build(skin, model[i], joint_count, palette, palette_count);
}
nt_skeletal_gpu_flush();

/* Game-owned passes; each list is read and packed when it is drawn, into its pass's stream:
 * enum { GAME_STREAM_GENERAL = NT_GFX_FRAME_VERTEX, GAME_STREAM_MAIN, GAME_STREAM_SHADOW }; */
bind_shadow_materials();
for (uint32_t c = 0; c < CASCADES; c++) {
    nt_gfx_begin_pass(&shadow_pass[c]);
    nt_gfx_bind_uniform_block(0, &views[c], sizeof(view_t)); /* copied into frame storage */
    nt_skinned_mesh_renderer_draw_list(GAME_STREAM_SHADOW + c, shadow_items, shadow_count);
    nt_gfx_end_pass();
}
bind_surface_materials();
nt_gfx_begin_pass(&main_pass);
nt_gfx_bind_uniform_block(0, &views[CASCADES], sizeof(view_t));
nt_skinned_mesh_renderer_draw_list(GAME_STREAM_MAIN, skinned_items, skinned_count);
nt_mesh_renderer_draw_list(GAME_STREAM_MAIN, static_items, static_count);
nt_gfx_end_pass();
nt_gfx_end_frame(); /* uploads frame storage, then executes the passes */
```

A shadow list drawn in several cascades packs once per cascade; to pack once,
the game writes the instances itself and draws them through the core in each
cascade. The sprite and text renderers record into frame storage at each emit
or draw, inside a pass; the shape renderer copies each kind into frame storage at
`flush`. Lifetime work (resource unmounts, `nt_program_ref_drop`,
renderer restores, any destroy) runs before the first pass or after
`nt_gfx_end_frame` (the frame rule); `nt_gfx_read_pixels` runs after
`nt_gfx_end_frame`, before the swap.

### Render targets

Render targets are a general backend capability for offscreen passes, not a
text-only path and not a shadow-map subsystem. Typical users include post-fx,
glow or bloom-like effects, minimaps, portals, and depth-aware rendering.

The game still owns pass order. Each pass selects its destination through
`nt_pass_desc_t.target`: zero selects the default framebuffer, and a valid
`nt_render_target_t` selects an offscreen target. `nt_gfx` binds the matching
backend framebuffer internally during `nt_gfx_begin_pass`; public code does not
bind or unbind render-target state outside the pass descriptor.

Each pass clears color and depth unless `load_color`/`load_depth` keeps the
attachment's current contents. Every pass starts with scissor disabled, so the
pass clear initializes the entire attachment; clear values matter only for a
cleared attachment. Stencil is never cleared by a pass. What else a pass resets
and what carries over is in
[API contracts: Passes and draw state](../core/api-contracts.md#passes-and-draw-state).

`nt_gfx_clear` is an explicit operation inside an open pass. Its borrowed
`nt_clear_desc_t` selects color and depth independently with `color`/`depth`
and supplies `clear_color`/`clear_depth`; unselected values are ignored.
It clears the current target under the current scissor, or the entire attachment
when scissor is disabled. It does not use the viewport as a clear rectangle.
It preserves the pipeline, vertex input, texture set, uniforms, viewport and
scissor. Depth clear temporarily enables depth writes and restores the bound
pipeline's mask before returning. Selecting neither attachment does no GPU work;
a lost context skips the operation. Stencil has no clear API.
Capture records a CLEAR request, its copied values and selections, its target,
and the actual GL calls without growing the event record.

`discard_color`/`discard_depth` end the contents' lifetime at `end_pass`, before
the framebuffer is unbound, without invalidating texture handles;
`discard_depth` also discards stencil. A later reader must use contents written
after the discard. Producer outputs sampled by later passes must not discard; a
consuming pass's flags apply only to its own attachments. Absent attachments
ignore the flags. Discarding the default framebuffer's color asserts, because
it is the presented frame. The descriptor is borrowed only during `begin_pass`.

Loading does not preserve default-framebuffer contents across presentation
(`preserveDrawingBuffer` is false) or restore contents after context loss.
Discard maps to `glInvalidateFramebuffer`; native skips this optional hint when
the driver lacks ARB_invalidate_subdata. Call capture records attachment enums,
not pointers. BEGIN/PASS records contain the requested flags; INITIAL/PASS holds
only cached clear values, with flag fields having no meaning.

Pass color and depth clears are pass-owned operations. In particular,
`clear_depth` is applied independently of the previous pipeline's `depth_write`
state; pipeline write masks affect draws, not the next pass initialization. Bound
pipeline, vertex input, the instance binding and the logical complete texture
set are pass-scoped: `begin_pass` discards them. The texture set is additionally tied to the bound
program and is discarded when that program changes. Pipeline and vertex-input binds, texture-set application,
instance-buffer re-pointing, uniform writes and draws outside a pass assert.
Destroying a live object from the first pass until `nt_gfx_end_frame` asserts
(the frame rule, see Draw-phase command stream).
Physical texture/sampler GL bindings and uniform-block binds remain context
state. The backend deduplicates texture/sampler binds across passes;
every uniform-block bind records one `glBindBufferRange`. A depth clear forces the depth
mask on and leaves it on; the pass's first pipeline bind sets its own mask.

A render target is a thin framebuffer object over optional attachments, color
and depth. Each attachment is a game-owned texture that the target borrows, as a
vertex input borrows its buffers, and lifetime follows vertex inputs: a target is
a baked object, while its textures are primary resources. One texture may serve
several targets, such as a depth buffer shared by two passes. Sampling an
attachment while its target is the active pass would create a framebuffer
feedback loop and asserts before any backend bind. Backend FBO ids stay private
to the concrete graphics implementation. The descriptor rules, lifetime,
context-loss behavior and queries are specified in
[API contracts: Render-target handles](../core/api-contracts.md#render-target-handles).

There is no renderbuffer storage: without `glInvalidateFramebuffer` a
renderbuffer costs the same memory as a texture, and its only advantage, MSAA,
is not supported. A backend must not substitute its own attachment format.
There is no resize: the target size is the size of textures the game owns, so a
size change is new textures and new targets.

A depth-only target (a shadow map) has no color attachment. It is
framebuffer-complete, the pass color clear is a no-op, and
`nt_gfx_read_pixels` with it as the source asserts.

Attachments are ordinary textures: `NT_SAMPLER_DEFAULT` selects the sampler of
their own descriptor, and a binding may override it, for example with a
comparison sampler for a shadow lookup.

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

The supported depth formats are `DEPTH16`, `DEPTH24`, and `DEPTH32F`.
Attachment textures, like every texture, keep GL default texture state; the
sampler object each binding uses decides filtering and wrap. Depth storage
requires `NEAREST` in its descriptor.

Depth comparison lives on the sampler object (`nt_sampler_desc_t.compare_func`),
not on the texture, because one depth target is read two ways: through a
comparison sampler for the shadow lookup, and through a plain sampler for a
raw-depth debug view. The field is a single tri-state — `NONE`, `LEQUAL`,
`LESS` — so a zero-filled descriptor is a plain sampler and there is exactly one
spelling of "no comparison". Sampler state supersedes texture state, so a
comparison sampler makes `LINEAR` legal on that binding while the attachment
texture is untouched. A comparison sampler is rejected on non-depth storage,
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
`mip_count - 1` when storage commands are issued, matching the uploaded or generated
chain. A sampler override may therefore use a mipmap minification filter even
for a single-level texture, where it samples level 0. Driver upload failures
are not polled, so a published handle does not guarantee complete GPU storage.

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
RGBA16F mipmap generation requires `has_float_render_target` as well; its
filtering is core and needs no float-filtering extension.
This does not add RGBA32F render-target support to the engine.

This capability supplies low-level targets and depth textures only. It does not
define light cameras, PCF, cascades, shadow atlases, material shadow integration,
or a shadow-map system.

## Frame observation

All gfx work between `nt_gfx_init` and `nt_gfx_shutdown` happens inside a
**frame**, so no operation or GL call escapes the counters. `nt_gfx_begin_frame`
is the only counter boundary: `nt_gfx_init` opens the first frame, and every
begin_frame closes the open frame and at once opens the next. The host calls
`nt_gfx_begin_frame` once at the start of each frame callback, before any other
gfx use (resource and font steps included), also when nothing renders. Work
between init and the first begin_frame (init itself, pre-loop loading) is the
first frame; teardown work after the last callback lands in a frame that
`nt_gfx_shutdown` discards unpublished. Frames are a host contract in every
build, independent of simulation time; app/gfx never close one implicitly.

The host also calls `nt_gfx_end_frame` once per callback, also when nothing
renders: after the last pass and before `nt_window_swap_buffers`. Passes run
only between begin_frame and end_frame; a begin_pass outside them asserts, also
on a lost context. end_frame executes the frame's recorded draw-phase calls (see
Draw-phase command stream) and ends the passes, not the counters: work after it
(resource calls, GPU timing polls, the pre-swap capture seam) still counts in
the open frame. The frame that init opens only loads and starts ended, so
pre-loop code that draws opens its own begin_frame/end_frame pair. A frame holds
any number of passes; their counters sum. begin_frame also does the per-frame
backend work: it ages the upload staging buffer and, with GPU timing, checks the
timer disjoint flag on a live context. The stub keeps no frame: its begin_frame and end_frame are inert and it
never publishes counters.

`g_nt_gfx.counters` holds the live counters of the open frame.
`nt_gfx_begin_frame` copies them into `g_nt_gfx.last_frame`, the last closed
frame, then resets them and advances `frame_sequence`; passes reset nothing.
`last_frame` stays unchanged until the next begin_frame or shutdown; its
`frame_sequence` is 0 before the first one. Code in a callback after its
begin_frame reads the previous callback's work from `last_frame`. A no-render
frame reports zero draws; old geometry is never reused. Counters carry no loss
status: a frame that met a context loss holds what was accepted before and
after it.

Context loss is synced at begin_frame. The web context registers a canvas
`webglcontextlost` handler that calls `preventDefault` (the browser restores only
a handled loss, so shells must not) and sets one latch. Browser lost and restored
events are separate tasks and never arrive inside a frame callback, so a restore
never lands inside an iteration; a loss can (below). begin_frame always takes the
latch, so a loss and restore that both happen between two callbacks (a
background tab) still wipe the backend tables. Taking the latch wipes every backend
handle and, unless a failed call latched the loss first, sets
`g_nt_gfx.context_lost` and logs one error. While `context_lost` is
set, begin_frame asks the browser (the only per-iteration JS query, and only in
the lost state; failed calls and readbacks ask on their own path); once the context is back, the same begin_frame restores it and
sets `g_nt_gfx.context_restored` until the next begin_frame. The game therefore
sees the restore before its resource step and before it builds anything. The
restore is one CONTEXT operation: it recreates the context, probes
capabilities and ends ACCEPTED; it recreates no frontend resource. A recreate
that fails leaves no context, logs one error and stays lost for good. A restore
that the browser reports lost again when it finishes stays lost without an
error log and is retried by a later begin_frame. A browser restores only a
loss whose event was handled, so begin_frame has taken the latch and wiped before
it restores.

A loss inside an iteration latches at the first backend call that fails on it:
the call asks the browser, sets `g_nt_gfx.context_lost`, logs the one error and
ends `CONTEXT_LOST`, and every later call takes its lost path. An owner therefore
creates in a straight line: once a shader create met the loss, the program,
pipeline and vertex input built on it end `CONTEXT_LOST` instead of asserting on
their 0 dependencies, and the draws take the lost path too. Operations issued
before the latch are issued and do nothing. The tables stay until the next
begin_frame takes the lost event, so the recorded stream and capture stay
consistent; a frame can see `context_restored` and a new latched loss together.
`nt_gfx_init` latches the same way when its capability probe meets a loss, so
nothing is made from the zero caps a lost context reports.
On a lost context pass calls keep their sequencing asserts and record nothing:
`begin_pass` still opens the pass, pass-scoped calls end `CONTEXT_LOST` after the
pass check, and `end_pass` closes the pass (its END is a backend no-op without a BEGIN).

A backend call that reports a failure (a create, a readback, a lazy sampler recreate at bind or on a
cache hit) asks the browser: a loss latches as above and ends the operation with
`CONTEXT_LOST`; a live context keeps its own failure reason and error log. A
readback asks after every read as well: WebGL reports a loss once through
`glGetError`, and the drain before the read consumes it. The backend asks only where the answer prevents a crash or a
misleading log: before shader and program creation, because Emscripten throws on
the null object some browsers return on a lost context; error logs for link,
uniform reflection and framebuffer completeness, which a loss suppresses;
after texture upload, because a nonzero WebGL name does not establish context
liveness; and the vertex array made at context setup, whose name 0 asserts only
on a live context. A GPU timer query named 0 leaves its segment unallocated and
skipped, because `beginQuery` throws on it. A fresh context (init or restore) first
drains GL errors: Emscripten keeps a recorded error across contexts, so a call
that reached the dead context must not fail the fresh one's first check.

Texture creation reads no GL error: on WebGL `glGetError` is a blocking
round trip to the GPU process that waits for the queued uploads. After upload,
the backend asks whether the context is lost without waiting for GPU completion;
a confirmed loss deletes the name and ends the create `CONTEXT_LOST`, including
when the browser returned a non-null texture object during loss. GL misuse is
reported under `NT_GFX_WEB_GL_DEBUG` / `NT_GFX_NATIVE_GL_DEBUG`. An upload or mipmap
generation the driver rejects on a live context (out of memory) is not detected:
the create ends `ACCEPTED` but GPU storage may be incomplete.

All counters are built and counted in every build; there is no counter option
or runtime toggle. Geometry and instance fields are uint64; operands widen before
multiplication, and uint64 sums cannot overflow within a frame. Draw calls are
not a separate field: `nt_gfx_draw_calls()` sums the four accepted draw
operations, which are the recorded draws: a merged draw ends `CACHE`. Vertices/indices are submitted
counts, multiplied by instance count for instanced calls; instances counts only
instances in instanced calls. These are not rasterized triangles or
vertex-shader invocations.

`frame_bytes` holds each frame storage stream's bytes allocated in the frame,
alignment padding included. Each upload writes it, so it is final after
end_frame. A
frame storage upload is an `NT_GFX_OP_BUFFER_UPLOAD` operation on the stream's
buffer, recorded inside the execution that sends it, so a capture shows each
upload before the replayed draws that read it.

Backends without GL (the test fake) issue no GL calls, so `gl[]` and the
upload fields stay zero there. `NT_GFX_CAPTURE_ENABLED` is a numeric interface
definition published by the interface target, so every consumer sees the same
configuration; `nt_gfx_capture_request`, `nt_gfx_capture_read` and
`nt_gfx_gl_call_name` exist only when it is 1.

`gl[]` counts, by `nt_gfx_gl_call_t`, every GL call the GL backend issues
through its `NT_GL*` funnel, queries included. Platform context management
(context create/destroy, loss events, `isContextLost` queries)
is not counted. Draw-phase calls issue their GL calls when the stream executes, in
the `nt_gfx_end_frame` of the frame that recorded them. The funnel
counts with an inline constant-index increment and (with capture) records in the same
expression that issues the call; a grep gate rejects any bare `gl*` call in
`engine/graphics/gl`. The funnel does no per-call frame check: a frame is
always open between init and shutdown. The
single `NT_GFX_GL_CALLS` table in `nt_gfx.h` defines the enum, `NT_GFX_GL_COUNT`
and, with capture, `nt_gfx_gl_call_name`. WebGL JS calls the web context makes
directly (`getExtension` and the `glGetQueryObjectui64v` timer-result bridge) are
counted and recorded through `NT_GL_ISSUED` at their C call site, in a separate
statement just before the JS call; the
JS that Emscripten's GL layer runs behind a C call (lazy uniform location
lookup, state shadowing) is a documented boundary: counters and capture see the
C API call. Payload fields count calls with non-NULL CPU data and their bytes,
in the same funnel; NULL storage and generated mips are excluded, texture bytes use the actual GPU format for each
mip/subrectangle, and failed creates keep already-issued work.

`accepted[]` counts public operations by `nt_gfx_operation_t` whose END result
was ACCEPTED, in every build: every public operation, readback and GPU timer
segment calls included, is one BEGIN/END pair, and END is the only place that
counts it. It counts every operation, nested ones included (default samplers,
cascaded destroys). Only frontend cache hits
(END result CACHE: an equal bind, or an indexed draw merged into the previous
one, whose indices still count), rejections and losses are left out; an operation whose
backend skipped a call as a cache hit (SKIP/CACHE) or found an inactive uniform
(SKIP/INACTIVE) still ends ACCEPTED and counts. A GPU timer poll with no result
yet ends `UNREADY`. Texture
sets count per operation, while per-unit binds show in `gl[]`. Accepted
operations minus GL calls is not a cache-skip count.
Each initialization restarts the frame sequence: the first frame after init is 1,
and `last_frame` holds 0 until the first begin_frame.

A context restore is one CONTEXT operation inside the begin_frame that performs
it and belongs to the frame that begin_frame opens. A new loss is wiped before
that frame opens, so a recorded frame's snapshot shows the wiped tables and
`context_lost` set, and the CONTEXT operation follows it. The BACKEND records of
the restore sit between its BEGIN and RESULT. It ends ACCEPTED when the context came back and
CONTEXT_LOST when recreation failed or met a loss. Raw GL names are valid within their
context segment; a CONTEXT operation in the stream separates segments, and
frontend handles are the identity across them.

Command recording is one-shot: `nt_gfx_capture_request` asks for the next frame
to be recorded. The begin_frame that closes the requesting frame consumes the request
and starts recording the frame it opens; without a request no frame records. The
first frame never records. A request made during a recorded frame replaces that
capture at the begin_frame that finishes it, so read a capture in the following
frame before requesting again: readable captures are at most every other frame.
`nt_gfx_desc_t.capture_capacity` reserves one
event array at init (default zero); a request without capacity asserts. Capture-OFF
builds ignore the field.
There is no growth or allocation while recording. Each pointer-free POD event
is 104 bytes, including padding; 16384 records reserve 1.625 MiB. Other storage
consists of fixed control state and counter snapshots, with no second event array.
All record bytes are initialized before publication. A recorded frame starts at
the begin_frame that opens it and first snapshots
inherited state, including one definition per live resource (plus
program uniform/sampler and vertex-input attribute records), into the same array.
Size the capacity for that snapshot plus the frame's commands; a capacity below
the snapshot overflows before any command is recorded.

`nt_gfx_capture_read` returns metadata by value and an immutable event prefix.
Read a finished capture right after `nt_gfx_begin_frame`: the prefix remains valid
until the begin_frame that starts the next requested recording overwrites it, or
shutdown; unrequested frames preserve it. Two counts in the same sequence delimit
an operation interval. Keep a capture by copying the metadata and `count` records
and redirecting the saved view's pointer to the owned array. An empty view has
a NULL pointer. The finalized view retains its frame's counters (and so its
`frame_sequence`) by value even after later unrecorded frames overwrite
`g_nt_gfx.last_frame`.

Every recorded public operation produces exactly one BEGIN, carrying its
request arguments, and one RESULT, carrying the outcome `result`; a creator's
RESULT carries the new handle (zero on failure), while its backend slot and
names are in the DEFINITION record. BEGIN and RESULT are recorded at the call.
The BACKEND and backend SKIP records of recorded commands appear when the
stream executes in `nt_gfx_end_frame`, outside any BEGIN/RESULT pair, and so do the BACKEND
records of program links that `nt_gfx_begin_frame` finishes, followed by each finished program's
DEFINITION. A program's DEFINITION does not imply readiness: one recorded while it links carries
no reflection. Immediate backend work inside a
draw-phase call, such as a lazy sampler recreation, stays inside its pair.
Operations issued inside another operation
(default samplers, cascaded destroys) nest between its BEGIN and
RESULT. `ARGUMENT` records are request
arguments belonging to the enclosing BEGIN (one per texture binding of a texture
set); `DEFINITION` is reserved for resource and inherited state. Issued backend calls do not
prove GL success or GPU completion. The view's `counters` are zero (sequence 0)
while a frame records and become the finalized frame's at the begin_frame that closes it; `overflow`
alone reports an incomplete event stream. Overflow stops event appends and never
truncates counters.

The `object_kind` and `object` pair identifies a full frontend handle, including
its generation. Backend records instead use `detail` as `nt_gfx_gl_call_t`, whose
values are named after the issued function (`NT_GFX_GL_glBindVertexArray`), and
carry raw GL names of one GL context; their operation is always STATE. Inside
an immediate operation the enclosing BEGIN names the frontend operation. With GPU timing, begin_frame
runs the timer disjoint check as its own `TIMER_DISJOINT` operation on a live
context; the query is issued only while a timer query is pending. Each issued call is recorded
exactly once, at the call site, by the same statement that issues it (an
`NT_GL_ISSUED` JS bridge: by the statement before the JS call).
`backend.args` follows the GL integer argument order; pointer payload, readback
output and debug-label arguments are presence bits, gen/delete arguments contain
the count followed by each name, a returned value (`glCreate*`, `glGetError`,
locations, status) follows the arguments, and indexed offsets are byte offsets.
Output pointers are presence bits; their written values are not recorded. Readback, timer-query and debug-group calls are
issued calls too and are recorded like any other.
Float arguments occupy `backend.values` in float argument order. Matrix and vec4
calls use `uniform` with the location in `name`, float count in `count`, and
copied values. `backend.bytes` is actual CPU upload payload, zero for NULL storage.
No event borrows upload memory, shader source or caller labels.

Resource `DEFINITION/STATE` records with `object_kind=NONE` use `detail` as the
resource kind and `backend.args[0..1]` as backend slot/raw GL name. Frontend resource definitions
carry the full handle, current backend slot and available dimensions/relationships.
A render-target definition carries the color and depth texture handles in
`related[0..1]`, the color format in `format` and the depth format in `usage`,
zero for an absent attachment, and the size of those textures.
Shader, program and vertex-input definitions carry result `UNKNOWN`: the frontend
retains no shader stage or source, program stage pair or vertex-input layout, so
those fields are absent, not zero. A vertex input created during a recorded frame
follows its definition with `DEFINITION/ATTRIBUTE` records. The backend defines
each VAO of a vertex input with its stream index (`stream -
NT_GFX_FRAME_VERTEX`) in `backend.args[2]` and the instance offset its pointers
hold in `args[3]` (`UINT32_MAX` before the first instanced bind); a VAO built on
the first bind with another stream is defined during the replay. An upload that
finds its buffer already bound to `GL_ARRAY_BUFFER` records a `SKIP` of
`glBindBuffer`. Instance attribute definitions
stay one set per vertex input. Both bind forms are `NT_GFX_OP_VERTEX_INPUT`;
the instanced one carries the stream in `binding.slot` and the offset in
`binding.offset`.
Restore defines no resource. Primary resources survive a loss as husks and get
no fresh definition; pipelines, vertex inputs and render targets that the first
detection frees get no DESTROY record. Samplers are re-defined when lazily recreated. Definitions
remain meaningful after resource destruction or slot reuse.

The frontend `INITIAL/STATE` record (`detail` `NT_GFX_INITIAL_FRONTEND`, arg 0 =
context lost) opens the snapshot; pass-scoped bindings are not recorded, because the
first `begin_pass` discards them. The scissor enable and rectangle records (the rectangle
carries over frames) and the
frontend resource definitions follow, then the backend `INITIAL/STATE` record
(`NT_GFX_INITIAL_BACKEND`, cached GL names including the `GL_ARRAY_BUFFER` binding
in `args[7]`, and framebuffer size) and the backend's
own definitions. While the context is known lost, the backend records hold the
dead names of the lost context.
Among INITIAL records, `detail` is meaningful only on INITIAL/STATE.
Program publication and initial state include `INITIAL/SAMPLER` records with
backend program slot, name hash, location, unit and sampler class in args 0–4.
`INITIAL/UNIFORM_VEC4` gives program slot/name hash/location in args 0–2 and cached
vec4 values; `UNKNOWN` means no retained value. These INITIAL records can occur
inside CREATE when the program first becomes available. Inactive names emit
SKIP/INACTIVE; cache skips are distinct from invalid requests.

`SKIP` records mark work that was not issued without ending an operation:
backend cache skips (`SKIP/CACHE`) and inactive uniform or texture-set names
(`SKIP/INACTIVE`).

Pipeline state records use integers 0–12 for program, depth enable/write/function,
cull, blend enable, RGB source/destination, alpha source/destination, RGB/alpha
operation and polygon offset enable. Values 0–5 hold blend color, offset factor
and units. Only the backend defines pipeline state: its `DEFINITION/PIPELINE`
record uses the program backend slot and backend enums, and the frontend
pipeline definition carries the program handle in `related[0]`. Backend
`DEFINITION/PIPELINE` and `DEFINITION/ATTRIBUTE` records carry the pipeline or
vertex-input backend slot in `detail`; initial state uses the current raw
program name. Vertex-input creation copies each static/instance attribute with
its divisor, layout, and known buffer. The initial SCISSOR record holds the
carried-over rectangle; it is `UNKNOWN` before the first set and after a
context loss. Uniform-block bindings are not recorded: each block lives one frame. A bind inside the capture that
ends `CACHE` matches this state or one set earlier in the frame. Inherited
layouts unavailable in existing CPU state are explicitly unknown. Capture
never adds a GL-state mirror of its own or queries GL to reconstruct state.

## Shape strokes

`nt_shape_renderer` owns immediate-mode shape geometry and batches it until
`flush`; the game owns the pass, view-projection matrix and viewport. Thick
lines are triangle geometry on native GL and WebGL 2, so width never depends on
hardware line-width support.

### Paths and width

- `line(a, b, color)` draws one independent segment with butt ends.
- `polyline(points, count, closed, color)` draws a connected sequence of
  world-space `float[3]` positions. It consumes points during the call and keeps
  no caller pointers. The caller samples curves into points; this renderer does
  not own Bezier/spline evaluation or an adaptive tessellation policy.
- Open paths have butt ends. Closed paths connect the last point to the first;
  the caller may repeat the first point at the end. Consecutive equal positions
  are skipped using component-wise equality. Fewer than two remaining positions
  emit nothing; two positions produce one segment even with `closed=true`.
  `points` may be null only when `count=0`. Positions must be finite.
- Joins use a miter up to four half-widths, then a bevel. A flush in the middle
  of a path keeps its joins.
- `set_line_width(width)` selects world units, including when switching back
  from pixels. The default is `0.02`. Width is applied after a shape's scale and
  rotation, so it is independent of radius or height; the camera projection
  determines its size on screen.
- `set_line_width_pixels(width, viewport_width, viewport_height)` selects
  framebuffer pixels. Dimensions are the active viewport's physical pixel
  size, including for an offscreen target or sub-viewport. The game resubmits
  them after a viewport/target/DPR change. The setter does not change the gfx
  viewport. Both dimensions must be positive; both width setters require a
  finite positive width and assert programmer violations.

Width, width mode, viewport dimension, VP and depth changes flush all pending
geometry. Identical values do not flush.
Settings survive GPU restore, including a restore that met a new loss; queued
shapes are dropped.
The game must flush before changing render passes or directly changing the gfx
viewport; the renderer does not intercept gfx state changes.

World strokes use camera-facing cross-sections at each endpoint. The camera is
derived from the VP matrix: perspective strokes face its projection center,
orthographic strokes face the constant view direction. Pixel strokes
project adjacent points into viewport pixel coordinates before constructing
joins. Their centerline is clipped against the homogeneous near plane before
perspective division; clipped ends become butt ends. A fully hidden segment
emits no visible triangles. Reversed/degenerate directions use bounded fallback
geometry, without a division by zero. These are camera-facing strokes, not
cylindrical tubes with volumetric thickness.

### Ready-made wire shapes

Rectangle and triangle outlines use connected closed paths. Circle outlines
use one closed 16-segment XZ ring; spheres use three orthogonal rings. Cylinders
use two closed rings and four independent struts. Capsules use two equator rings
and two closed meridians that include the straight sides. A capsule with
`height <= 2 * radius` uses the sphere wire template, avoiding duplicate rings
and collapsed straight segments. Rotated variants transform the complete path
before constructing its thickness.

A branching wire graph is distinct from a path: cube edges remain independent
segments, and cylinder strut/ring intersections do not gain
an arbitrary two-edge join. No global graph stitching, hidden-edge extraction,
mesh silhouette or duplicate-edge removal is implied by these APIs.

Strokes are opaque and follow the depth setting. Inner bevel triangles and
intersecting paths can overlap; the renderer does not promise composited
translucent strokes.

### Storage and draw order

No heap allocation or trigonometry occurs when submitting strokes. Filled
shapes and circle, sphere, cylinder and capsule wires use immutable templates
built at initialization, one index buffer range per type in two shared buffers
(fills, wires), and cost one instance per shape; one program draws every filled
type, the capsule's hemisphere tag riding in the template's fourth component.

Each kind is staged on the CPU between flushes. `flush` copies every non-empty
kind into frame storage and draws it from there: instanced kinds go to the
vertex stream set by `nt_shape_renderer_set_stream` (default
`NT_GFX_FRAME_VERTEX`; a change flushes, like the other settings) and bind it at
the copy's offset, and triangles are a non-indexed list in the general stream,
which their vertex input reads at offset 0. Frame storage holds every flush of
the frame until `nt_gfx_end_frame`, and so does the command stream, so a host
sizes the used vertex streams and `stream_capacity` for its busiest frame;
`nt_shape_renderer_init` asserts a nonzero general-stream budget.

Every flush draws filled instanced shapes by type, then triangles,
then wire templates by type, connected segments and independent lines. Within
one flush this kind order replaces submission order: outlines stay on top of
fills, and interleaved submissions batch into at most one draw per kind. Flushes
are the only ordering barriers — explicit `flush`, a full queue and the state
changes above. A game that needs a later layer over an earlier one, typically in
overlay mode, calls `flush` between them.

`NT_SHAPE_RENDERER_MAX_LINES` bounds independent lines (default 8192) and
`NT_SHAPE_RENDERER_MAX_POLYLINE_SEGMENTS` connected segments (default 1024).
`NT_SHAPE_RENDERER_MAX_INSTANCES` bounds each filled type (default 2048) and
`NT_SHAPE_RENDERER_MAX_VERTICES` the triangle vertices (default 16384). Each wire
template type holds `ceil(NT_SHAPE_RENDERER_MAX_INSTANCES / 4)` shapes (default
512). These bound the CPU staging, not a frame: a full queue flushes all pending
geometry.

## Renderer complexity classes

Not all renderers carry the same weight. The engine ships three classes; copying patterns across classes is a common mistake.

**Building blocks** — direct GPU primitives (`nt_gfx_draw_indexed`,
`nt_mesh_renderer`, optional `nt_skinned_mesh_renderer`). Single pipeline, fixed
pattern, one instanced draw per compatible run (see items-sorting-batching.md),
packed into frame storage and recorded at the call. Use for 3D
meshes, custom geometry, anything where the game owns batching strategy. Stay
minimal. The mesh renderers share one resolve-and-record body in
`nt_renderer_shared.h`.

**Batched dynamic** — high-throughput renderers (`nt_sprite_renderer`; future particles). Many small items per frame (1k–60k): geometry is written straight into frame storage, `draw_list` records one draw per run of equal batch keys, and immediate emits rely on the front-end's bind dedup and draw merge instead of a command queue. Multi-page atlas resolution and the SIMD quad path stay. Measured on bunnymark at 60k against the former queue and staging: draws 16 to 2, GL calls 136 to 24, buffer uploads 32 to 4 per frame; whole-frame CPU in Chrome (ANGLE D3D11) 9.5 to 8.5 ms. The cost moved into one upload of the whole frame's geometry (6.3 MB with `uint32_t` indices): on native desktop GL the frame is 15% slower at 60k and faster below about 5k sprites, because the frame storage no longer stays in the CPU cache the way the former 400 KB staging did.

**Specialized** — domain-specific layout (`nt_text_renderer` glyph atlas + line layout; future debug-line/IM-GUI). Sit between the two — more state than primitives, less throughput pressure than batched dynamic. `nt_text_renderer` records like the sprite renderer: each draw call writes its glyph quads into frame storage and records one indexed draw, which gfx merges with the previous draw of the same font and material. The per-call `nt_text_style_t` carries everything that shapes the vertices, so the renderer keeps no font or decoration state.

When adding a new renderer, classify first:

- One pipeline, fixed pattern → **building block** (model after `nt_mesh_renderer`)
- 1k+ items/frame with dynamic state → **batched dynamic** (study `nt_sprite_renderer`, but only copy what your throughput demands)
- Domain-specific layout/data → **specialized**

`nt_sprite_renderer.c` is not a renderer template. Its complexity earns its keep at 60k items/frame; a 100-item UI overlay doesn't need any of it.
