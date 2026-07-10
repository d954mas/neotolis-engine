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
  behavior is bad, fix it through a focused behavior issue or ADR.
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

`nt_gfx_resize_render_target` preserves the logical render-target handle and
owned attachment texture handles, but reimages backend storage. Pixel contents
are undefined after a successful resize; failed resize leaves the previous
backend storage active. WebGL context restore recreates backend objects from the
retained descriptor; it does not preserve pixels. Consumers must redraw
offscreen contents after resize or context restore.

Invalid render-target descriptors, exhausted configured target capacity, stale
handles, direct mutation of owned attachments, and render-target lifecycle
calls inside an active pass are developer errors and assert. Backend allocation,
framebuffer completeness, resize, and context-restore failures remain runtime
failures reported through invalid handles, `false`, or readiness queries.

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
