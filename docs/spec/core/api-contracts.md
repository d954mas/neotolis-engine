# API Contract Style

This chapter defines how Neotolis public C APIs spell storage ownership,
pointer lifetime, nullability, callback/user-data scope, and output parameters.
It is a style contract, not a new ownership system, and it does not change the
engine/game boundary from [Principles](principles.md).

Related: [Principles](principles.md), [Memory Policy](../runtime/memory.md),
[Resource Registry](../assets/resource.md),
[Logging, Errors, Debugging](../debug/logging-errors-debugging.md)

## Scope

- Apply this vocabulary to new public headers and to touched API comments.
- Do not rename public symbols, fields, or typedefs only to match this chapter.
- On a deliberate breaking-change branch, rename or reshape an actively
  misleading API instead of explaining the mismatch with a comment.
- Do not change behavior to make a lifetime easier to describe. If the current
  behavior is bad, fix it through a focused spec and code change.
- Keep exact per-API lifetimes next to the API declaration or in the module
  chapter; this chapter defines the shared vocabulary.
- Do not introduce refcounting, smart-pointer layers, heap copies, or extra
  indirection in hot paths for style compliance.

## Required Contract

Public APIs in this chapter's scope that expose a pointer, callback, handle,
pointer-carrying struct, or output parameter must make the applicable questions
clear from the signature, name, or nearby comment:

- which storage backs pointer data: caller, module, frame scratch, pack blob,
  external runtime object, or caller-provided output
- who may mutate that storage
- who frees it, and with which function
- for containers, arrays, or pointer-carrying structs: whether ownership is
  outer storage only, elements/subpointers only, both, or neither; name the
  free path for each owned layer
- whether the callee may store the pointer, and until which unregister, clear,
  destroy, replacement, or shutdown event
- the exact invalidation event: next call, named mutation,
  `nt_mem_scratch_reset`, unmount, destroy, shutdown, etc.
- whether `NULL` is a valid result/input or a programmer bug
- for output parameters, including `out_*`, size, count, progress, and data
  pointers: whether the pointer itself may be `NULL`, what value is written on
  success/failure, and whether the value written through it may be `NULL`; if
  not stated, the output pointer is required
- for pointer-plus-count views, whether `data` is guaranteed non-NULL when
  `count > 0`
- which failures are recoverable return values and which are `NT_ASSERT`
  contract violations

Avoid vague words such as "temporary" or "owned" without the invalidation event
or free path. A good contract says "valid until X" and "freed by Y".

Input pointers are borrowed only for the duration of the call unless the API
says it copies, stores, consumes, or pins them.

If the primary invalidation event cannot be named precisely, or the invalidating
mutation set is open-ended, prefer a handle, caller-filled output, or explicit
copy over returning a raw pointer. Multiple explicit events are OK when written
as "until X or Y, whichever happens first."

## Naming Rules

| Word | Rule |
|---|---|
| `init` / `shutdown` | Initializes or tears down module state or caller-provided storage. Does not imply heap ownership by itself. |
| `create` / `destroy` | Creates a logical object, handle, or registry entry; the API must state who owns it and which teardown verb applies. Avoid in hot paths. |
| `make` / `destroy` | Module-local synonym where already established, e.g. gfx resource creation. Do not rename only to normalize. |
| `find` | Pure lookup. Must not allocate or create. Missing value is a normal result. |
| `get` | Returns a current value or borrowed view with a named owner event. Caller must not free. |
| `peek` | Returns a mutation-sensitive borrowed pointer. Caller must not store it. |
| `view` / `nt_*_view_t` | Non-owning pointer plus length/count, passed by value. No implicit NUL terminator. Backing lifetime must be stated. |
| `copy` | Makes an owned deep copy. Input may die after the call returns. |
| `take` | Transfers ownership from callee to caller. Caller frees through the documented function. |
| `consume` | Transfers ownership from caller to callee. Success and failure ownership rules must be documented. |
| `pin` / `unpin` | Extends lifetime of engine-owned backing storage without transferring free responsibility. Reserve for real pin semantics. |
| `retain` / `release` | Reserve for real refcounted APIs only. Do not use as vague synonyms for `pin` or `destroy`. |
| `out` / `out_*` | Caller-provided output storage. Pair pointer outputs with count/capacity where applicable. |

Use `peek` instead of `get` when the returned pointer is mutation-sensitive
slot/module auxiliary state that must not be stored, such as a pointer
invalidated by resource resolve/cleanup. Established `get_*` pointer APIs may
keep their names when renaming would only normalize style, but their lifetime
and invalidation events must be explicit.

Use `take` and `consume` only for real ownership transfer. If no ownership
moves, do not use either word. Never write bare "takes ownership"; say from
whom to whom, and name the free function or destroy event.

## Scratch-Backed Results

Scratch-backed public results are backed by `nt_mem_scratch`, are valid exactly
until the next `nt_mem_scratch_reset()`, and must not be freed or stored beyond
that reset. Do not describe them as valid "until end of frame"; reset placement
is the contract. Full scratch ownership rules live in
[Memory Policy](../runtime/memory.md).

## Callbacks And User Data

Treat `(fn, user_data)` as one registration unit. The API must state:

- whether `user_data` is stored or only passed through during the call
- invocation scope: during this call, until unregister, until replaced, until
  destroy, or until shutdown
- cardinality: zero, one, or many callback invocations
- how long stored `user_data` must remain valid
- whether each stored `user_data` value has a destroy callback, and whether
  that callback runs exactly once for that value
- replacement behavior and failed-registration ownership
- if there is no destroy callback, that the engine never frees `user_data`
- whether the callback may re-enter the module that invokes it, and which
  mutating or accessor APIs are legal from that callback context

Callback scope should use operational wording: "only during this call", "until
unregister", "until replaced", "until context destroy", "until shutdown", or
"until the destroy callback fires".

## Handles

Generational handles are values, not owned pointers. Passing a handle does not
transfer ownership of the backing resource. An API that destroys, unregisters,
invalidates, or releases backing state must say so explicitly.

Virtual-resource APIs that publish a runtime handle do not automatically own or
destroy the runtime object unless the function says it consumes ownership of the
runtime object represented by that handle.

### Program handles

`nt_program_t` is the linked (vertex, fragment) pair and has exactly one owner:
whoever called `nt_gfx_make_program`. Pipelines and materials store the handle
without owning it. Destroying the program destroys its pipelines; the owner
does not need to walk them. Shader stages have independent lifetimes: destroying
a stage does not affect a program already linked from it.

A program's linked executable and identity are immutable after
`nt_gfx_make_program`; its uniform values and block bindings remain mutable.
Recovery requires the owner to destroy the old program and link a new handle.

Handle validity and GPU liveness are separate. `nt_gfx_program_valid` reports
whether the handle still refers to a live slot; `nt_gfx_program_ready` reports
whether the GL program behind it exists. Processing context loss clears readiness while
handles stay valid, and because no API relinks, a valid handle that is not ready
never becomes ready again -- that state is terminal, not transitional.
`nt_gfx_make_pipeline` requires readiness.

`nt_gfx_destroy_program` accepts `NT_PROGRAM_INVALID` as a no-op and asserts on
a stale non-zero handle. Clear the owner's variable to `NT_PROGRAM_INVALID`
when destroying it.

A link failure is a developer error and asserts, alongside an invalid stage
handle, and an exhausted program pool.

`nt_gfx_register_global_block` applies the global name -> binding slot registry
to existing and future programs; registration may precede or follow linking.
There is no per-program override. The registry borrows `name` without copying:
the string must remain valid and unchanged until `nt_gfx_shutdown`. Registration
survives context loss.

`nt_gfx_make_program` returns `NT_PROGRAM_INVALID` for the two states a context
loss leaves behind, and for nothing else. The first is the loss itself, including
the interval after the browser recovers but before `nt_gfx_begin_frame` finishes
resetting the backend tables: linking waits until that recovery completes, even
when newly created shader stages are ready. The second is a stage handle that is
still live but whose GPU object that loss discarded — permanently unready, so the
owner recreates the stage and links again. Both are recoverable and neither
asserts. A stale stage handle remains a developer error and traps.

`nt_material_set_program` is the only setter for the borrowed handle, including assignment
from or to `NT_PROGRAM_INVALID`. Assigning the same handle is a no-op, so a
per-frame gate needs no assignment latch.

A replace does not reach work already staged. Text captures its pipeline on the
first quad of an empty staging buffer, including the first quad after an internal
flush. Sprite captures its pipeline and bindings when a command opens; a capacity
flush continues that command's snapshot even if the material's program changed.
An explicit `nt_sprite_renderer_set_material` rechecks the program even for the
same material handle. Numeric material params remain mutable and are read at
flush; the snapshot does not freeze the whole material.

For an explicit game-controlled transition, flush, replace the program, then call
the renderer's `set_material` before emitting more work. If the old program is
destroyed rather than merely replaced, its pipelines go with it and the staged
batch is dropped instead -- there is nothing left to draw it through.

A material carries no readiness field. Callers derive readiness with
`nt_gfx_program_ready(nt_material_get_info(mat)->program)`, which is false before
the first assignment, after context loss is processed, or after program
destruction. The ECS `draw_list` paths skip unready programs and warn once until
a pipeline is built again. The immediate-mode `nt_sprite_renderer_set_material` /
`nt_text_renderer_set_material` entry points assert only that a program was
assigned. Renderers skip unready programs, and `nt_gfx_make_pipeline` checks
context loss before asserting readiness.

Texture slots differ by renderer. A sprite material that samples the atlas
declares its page sampler at slot 0; that slot's resource is never sampled,
because the renderer substitutes the page texture there per command. A material
declaring no textures never receives the page and is for shaders that compute
coverage analytically. Every other declared sprite slot must resolve to a
texture — register a placeholder with `nt_resource_set_placeholder_texture` to
survive async load races; a sampler override does not exempt a slot, since the
override only picks filtering for a texture that still has to exist. A text
material declares no textures at all — the font's curve and band textures are
the text renderer's own binds, on the units its program gave `u_curve_texture`
and `u_band_texture` — and `nt_text_renderer_flush` asserts both: that the
material declares nothing, and that those two are the program's only samplers.

Every other material declares a slot for every sampler its program uses, and the
renderer binds each slot at the unit the program assigned that name; the material
transition asserts the coverage. A declared name the program does not sample is
ignored.

Pipeline cache keys include the program handle, so replacement selects a
different entry. Destroying the old program frees its pipelines immediately;
renderers remove their dead cache records on the next insertion after a miss,
or when resetting the cache. Lookup validates a matching pipeline but does not
remove records. An unassigned program kept alive by its owner keeps its pipelines
alive too. `nt_gfx_destroy_pipeline` accepts stale handles as a no-op because
program destruction can invalidate a renderer's cached handles.
Materials retain the stale program handle until reassignment; readiness reports
false without mutating the material.

### Texture descriptors

`nt_texture_desc_t.format` is required and names the real storage format.
`RG16UI` requires `NEAREST` minification and magnification. `DEPTH16`, `DEPTH24`,
and `DEPTH32F` require the same, plus `data == NULL` and no mipmaps.

A sampler override passed to `nt_gfx_bind_texture` must obey the same format
restrictions; it cannot replace the explicit texture state with an
incompatible filter. Depth comparison
is the one documented exception, and it is sampler state only: `DEPTH*` accepts
`LINEAR` from a sampler whose `compare_func` is not `NONE`, because the filtering
then applies to comparison results rather than to raw depth. The texture keeps
`NEAREST` either way, and the same descriptor field is rejected on non-depth
storage.

### Render-target handles

`nt_render_target_t` is a logical graphics handle. `nt_gfx_make_render_target`
copies the descriptor into `nt_gfx`; the caller may release or mutate its source
descriptor after the call returns. The color attachment, and the depth attachment
when the target was created with sampleable depth, are module-owned
`nt_texture_t` values. They remain valid until
`nt_gfx_destroy_render_target(rt)`.

Destroying a render target invalidates the target handle and its owned
attachment texture handles. Callers do not destroy those textures directly.
Accessors such as `nt_gfx_render_target_color` and
`nt_gfx_render_target_depth` return invalid texture handles when the target is
invalid or the requested attachment does not exist. `nt_gfx_render_target_ready`
reports whether a valid target currently has live backend storage.
`nt_gfx_texture_ready` provides the same live-backend check for a texture handle.
Both readiness queries return `false` for invalid handles, so callers can also
use them after a failed resource-creation call.
`nt_gfx_texture_size` writes a texture's logical dimensions to its two required
outputs. Invalid handles write zero to both outputs and return `false`.
`nt_gfx_texture_format` returns the retained logical format, or
`NT_TEXTURE_FORMAT_INVALID` for an invalid handle.

`nt_gfx_resize_render_target` preserves the logical render-target handle and
owned attachment texture handles, but reimages backend storage. Pixel contents
are undefined after a successful resize; failed resize leaves the previous
backend storage active. WebGL context restore recreates backend objects from the
retained descriptor, including attachment formats and independent color/depth
default sampler state; it does not preserve pixels. Consumers must redraw
offscreen contents after resize or context restore.
While the backend reports a lost context, `nt_gfx_begin_frame` skips the frame
without attempting recreation. Recreation starts only after the backend leaves
the lost state; a failed backend-context recreation is retried on a later frame.
After the context recovers, each render target is recreated once. A failed target
remains unready; its owner destroys and recreates it, or uses a fallback.

Render-target descriptors explicitly separate depth storage from depth format.
`NONE` has no depth format or attachment, `BUFFER` has a non-sampleable depth
attachment, and `TEXTURE` has a sampleable `nt_texture_t`. Returned attachment
texture metadata uses the real storage format; color formats are never used as
placeholders for depth. The backend receives the complete descriptor and does not
choose attachment formats or sampler defaults.

Invalid render-target descriptors include mismatched color/depth format classes,
a missing or extraneous depth format for the selected storage, invalid sampler
values, and non-`NEAREST` depth filtering — comparison is sampler state and
never reaches this descriptor. These cases, exhausted configured target
capacity, stale handles, direct mutation of owned attachments, and
render-target lifecycle calls inside an active pass are developer errors and
assert. `nt_gfx_make_pipeline` follows the same split: a NULL descriptor, an
unready program, and an exhausted pipeline pool assert, so a returned invalid
pipeline handle means a lost context or a failed backend allocation — the two
recoverable outcomes, both retried on a later frame.
`nt_gfx_make_vertex_input` applies the same contract to the layout checks: an
attribute count over `NT_GFX_MAX_VERTEX_ATTRS` (instance layouts over
`NT_GFX_MAX_INSTANCE_ATTRS`), a stride over the WebGL2 cap of 255, misaligned
or duplicated attributes, mismatched buffers, and an exhausted pool all
assert. The returned vertex input is caller-owned and destroyed with
`nt_gfx_destroy_vertex_input`; it borrows the referenced buffers, whose
destruction may invalidate it through the documented cascade. The descriptor
and label are borrowed only for the call. Creating a pipeline or a vertex input
preserves both current bindings (the bound pipeline and the bound vertex
input); the caller does not need to rebind after creating another object.
Allocation failures from public GPU-resource operations, framebuffer
completeness, resize, and context restore remain runtime failures reported
through invalid handles, `false`, or readiness queries. Mandatory backend
setup objects are internal invariants: failure to create the GL service EBO
upload VAO with a live context asserts.

`nt_gfx_begin_pass` asserts on invalid sequencing and on a non-ready target.
Callers check readiness before beginning work that depends on restored GPU
storage; there is no non-asserting pass-begin variant.

## Hot Path Rule

In hot paths, prefer handles, borrowed views, scratch-backed temporaries,
caller-filled output storage, or preallocated module storage. Heap allocation,
persistent deep copy, and ownership transfer belong in init, builder, explicit
load/activation boundaries, debug/devapi, or APIs documented as non-hot.
Resource resolve runs from `nt_resource_step()` and is part of the hot path. Do
not add resolve-time heap work as a style fix. The resource registry's current
transient resolve-pass storage is an explicitly documented module-specific
deviation, not a pattern; when touching that behavior, prefer preallocated
module storage instead.

## Wording Examples

Use these as wording targets; verify the owning header states exact optionality
and invalidation events before copying a pattern.

```c
/* Returns a scratch-backed view. Valid until the next nt_mem_scratch_reset();
 * caller must not free or store beyond that reset. */
```

```c
/* Transfers the completed buffer to the caller; caller frees with free().
 * out_size may be NULL; when non-NULL it receives size or 0 on no transfer. */
```

```c
/* Borrowed module view. Returns NULL when absent. Non-NULL data is valid until
 * this slot publishes another winner, unmount, destroy, or shutdown. */
```
