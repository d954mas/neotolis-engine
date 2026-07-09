# API Contract Style

This chapter defines how Neotolis public C APIs spell storage ownership,
pointer lifetime, nullability, and callback/user-data scope. It is a style
contract, not a new ownership system, and it does not change the engine/game
boundary from [Principles](principles.md).

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
- Do not duplicate every per-API lifetime here. Exact lifetimes stay next to
  the API declaration or in the module chapter.
- Do not introduce refcounting, smart-pointer layers, or heap copies in hot
  paths for style compliance.

This chapter borrows intent from established C/C++ conventions such as GLib
transfer/scope annotations, Core Foundation Create/Get rules, Python C API
reference ownership, SQLite static/transient binding policy, LLVM
StringRef/ArrayRef views, and C++ Core Guidelines owner/span/not_null. Neotolis
uses plain C wording, not their syntax.

## Required Contract

Public APIs in this chapter's scope that expose a pointer, callback, handle, or
pointer-carrying struct must make the applicable questions clear from the
signature, name, or nearby comment:

- which storage backs pointer data: caller, module, frame scratch, pack blob,
  external runtime object, or caller-provided output
- who may mutate that storage
- who frees it, and with which function
- for containers, arrays, or pointer-carrying structs: whether ownership is
  outer storage only, elements/subpointers only, both, or neither; name the
  free path for each owned layer
- whether the callee may store the pointer
- the exact invalidation event: next call, named mutation, next
  `nt_mem_scratch_reset`, unmount, destroy, shutdown, etc.
- whether `NULL` is a valid result/input or a programmer bug
- which failures are recoverable return values and which are `NT_ASSERT`
  contract violations

Avoid vague words such as "temporary" or "owned" without the invalidation event
or free path. A good contract says "valid until X" and "freed by Y".

If the primary invalidation event cannot be named precisely, or the invalidating
mutation set is open-ended, prefer a handle, caller-filled output, or explicit
copy over returning a raw pointer. Multiple explicit events are OK when written
as "until X or Y, whichever happens first."

## Contract Shapes

Use the smallest shape that matches the real behavior.

| Shape | Contract to state |
|---|---|
| borrowed input | Default for input pointers: valid only during the call unless the API says it copies, stores, consumes, or pins it. |
| stored-by-reference input | What must remain pointer-stable, and until which unregister, clear, destroy, or shutdown event. |
| copied input | Callee copies the needed data before returning; caller may release the input after return. |
| scratch-backed output | Backed by `nt_mem_scratch`; valid until next `nt_mem_scratch_reset()`; caller must not free or store it beyond that reset or in persistent state. |
| borrowed module view | Backed by named module/owner storage; valid until the named mutation, destroy, or shutdown event. |
| caller-owned output | Allocated or transferred to caller; comment names the free function. |
| caller-filled output | Caller provides storage plus capacity; callee writes count/size and documents truncation or assert behavior. |
| callback registration | Whether `(fn, user_data)` is stored, when it stops being called, and who frees `user_data`. |

## Naming Rules

| Word | Rule |
|---|---|
| `init` / `shutdown` | Initializes or tears down module state or caller-provided storage. Does not imply heap ownership by itself. |
| `create` / `destroy` | Creates a new logical object, handle, or registry entry; the API must state who owns it and which teardown verb applies. If it returns a caller-owned object, document the destroy/free pair. Avoid in hot paths. |
| `make` / `destroy` | Module-local synonym where already established, e.g. gfx resource creation. Do not rename only to normalize. |
| `find` | Pure lookup. Must not allocate or create. Missing value is a normal result. |
| `get` | Returns a current value or borrowed view with a named owner event. Caller must not free. |
| `peek` | Returns a mutation-sensitive borrowed pointer. Caller must not store it. |
| `view` / `nt_*_view_t` | Non-owning pointer plus length/count, passed by value. No implicit NUL terminator. Backing lifetime must be stated. |
| `copy` | Makes an owned deep copy. Input may die after the call returns. |
| `take` | Transfers ownership from callee to caller, e.g. `take_data`. Caller frees through the documented function. |
| `consume` | Transfers ownership from caller to callee. Success and failure ownership rules must be documented. |
| `pin` / `unpin` | Extends lifetime of engine-owned backing storage without transferring free responsibility. Reserve for real pin semantics. |
| `retain` / `release` | Reserve for real refcounted APIs only. Do not use as vague synonyms for `pin` or `destroy`. |
| `out` / `out_*` | Caller-provided output storage. Pair pointer outputs with count/capacity where applicable. |

Use `peek` instead of `get` when the returned pointer is mutation-sensitive
slot/module auxiliary state that must not be stored, such as a pointer
invalidated by resource resolve/cleanup. Use `get` for borrowed data with a
named owner event, such as pack-blob views valid until the resource slot
publishes a different winner, blob eviction, unmount, destroy, or shutdown.
Established `get_*` pointer APIs may keep their names when renaming would only
normalize style, but their lifetime and invalidation events must be explicit.

Use `take` and `consume` only for real ownership transfer. If no ownership
moves, do not use either word. Never write bare "takes ownership"; say from
whom to whom, and name the free function or destroy event.

Output pointers are not optional by default for APIs in this chapter's scope.
For `out_*` parameters, state separately whether the output parameter pointer
itself may be `NULL` to ignore the result, and whether the value written through
it may be `NULL`. Do not use "nullable" without naming which pointer is
nullable. Otherwise a `NULL` output parameter pointer is an `NT_ASSERT`
contract violation. If an older untouched API accepts `NULL` for an output
pointer but does not say so, fix the comment when that API is next touched.

For `nt_*_view_t`, state whether `data` is guaranteed non-NULL when `count > 0`.
A zero-length view may use `NULL` data unless the API says otherwise.

## Frame Scratch Storage

`nt_mem_scratch` is engine-owned transient storage. A scratch allocation is not
an owned object; it is caller-usable storage inside the frame arena. Callers must
not free it, store it beyond the next reset or in persistent state, or return it
from an API without documenting that the result is scratch-backed.

The invalidation event is exactly `nt_mem_scratch_reset()`. Do not describe
scratch-backed pointers as valid "until end of frame": the reset placement is
the contract, and tests may reset explicitly between simulated frames.

Use `scratch-backed` only for `nt_mem_scratch` storage. For per-context or
module arenas, say `context-owned buffer`, `module-owned arena`, or another
named owner, then name that owner-specific invalidation event.

Only the code that owns the top-level frame loop, or a test explicitly
simulating that owner, may call `nt_mem_scratch_init`,
`nt_mem_scratch_reset`, and `nt_mem_scratch_shutdown`. Modules may allocate
scratch storage, but must not reset the arena. Public APIs that allocate into
scratch must say whether they copy caller input into scratch or return a view
into scratch.

Canonical wording:

```c
/* Returns a scratch-backed view. Valid until the next
 * nt_mem_scratch_reset(); caller must not free or store beyond that reset. */
```

## Callbacks And User Data

Treat `(fn, user_data)` as one registration unit. The API must state:

- whether `user_data` is stored or only passed through during the call
- invocation scope: during this call, after registration until unregister,
  until replaced, until destroy, or until shutdown
- cardinality: zero, one, or many callback invocations
- how long stored `user_data` must remain valid
- whether each stored `user_data` value has a destroy callback, and whether
  that callback runs exactly once for that value when it is replaced,
  unregistered, destroyed, or shut down
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

## Canonical Examples

- `nt_resource_find`: pure lookup naming.
- `nt_resource_get_blob`: borrowed pack-blob view wording.
- `nt_resource_peek_user_data`: read-only, mutation-sensitive borrowed slot pointer.
- `nt_fs_take_data` / `nt_http_take_data`: callee-to-caller ownership transfer.
- `nt_ui_state`: retained UI view-state wording.
- `nt_ui_probe_collect`: caller-filled output with cap/truncation wording.
- `nt_ui_probe_collect_owned`: context-owned probe buffer wording.
- `nt_gfx_read_pixels`: caller-provided buffer with capacity wording.
- Builder encode/bridge helpers: caller-owned heap output with documented
  `free()` or module free-function wording.

Use these as wording targets, not as blanket proof that older header comments
already satisfy this chapter. Before copying a pattern, verify the owning header
states the output optionality, invalidation event, and free path required above.
