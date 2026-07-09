# API Contract Style

This chapter defines how Neotolis public C APIs spell ownership, pointer
lifetime, nullability, and storage authority. It is a style contract for new
APIs and for APIs touched during nearby work. It is not a new ownership system,
and it does not change the engine/game boundary from
[Principles](principles.md).

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

This chapter was informed by established C/C++ API conventions: GLib
transfer/scope annotations, Core Foundation Create/Get rules, Python C API
new/borrowed/stolen references, SQLite static/transient binding policy, LLVM
StringRef/ArrayRef views, C++ Core Guidelines owner/span/not_null, and optional
SAL/Clang analyzer annotations. Neotolis borrows the intent, not their syntax.

## Contract Checklist

New public APIs, and existing APIs whose contract comments are touched, must
make the relevant questions clear from the signature, name, or nearby comment
when they expose a pointer, callback, handle, or pointer-carrying struct:

- who allocates storage
- who may mutate it
- who frees it, and with which function
- whether the callee may store the pointer
- the exact invalidation event (`next call`, `next frame`,
  `nt_mem_scratch_reset`, `unmount`, `destroy`, `shutdown`, etc.)
- whether `NULL` is a valid public result or a programmer bug
- which errors are recoverable return values and which are `NT_ASSERT`
  contract violations

Avoid vague words such as "temporary" or "owned" without the invalidation event
or free path. A good contract says "valid until X" and "freed by Y".

## Vocabulary

| Word | Meaning |
|---|---|
| `init` / `shutdown` | Initializes or tears down module state or caller-provided storage. This does not imply heap ownership by itself. |
| `create` / `destroy` | Creates a caller-owned handle/object with a documented destroy pair. Runtime hot paths should use this sparingly. |
| `make` / `destroy` | Module-local synonym for create/destroy where already established, e.g. gfx resource creation. Do not rename to normalize. |
| `copy` | Makes an owned deep copy. Input may die after the call returns unless the API says otherwise. |
| `get` | Returns a current value or borrowed view. Caller must not free. Pointer lifetime must be stated. |
| `find` | Pure lookup. It must not allocate or create a resource. Missing value is a normal result. |
| `peek` | Borrowed pointer with a very short lifetime. Stronger warning than `get`: do not store. |
| `take` / `consume` | Transfers ownership from caller to callee, or from callee to caller for `take_data`-style APIs. The success/failure transfer rule must be documented. |
| `view` / `nt_*_view_t` | Non-owning pointer plus length/count, usually passed by value. No implicit NUL terminator. The backing lifetime must be stated. |
| `pin` / `unpin` | Extends the lifetime of engine-owned backing storage without transferring free responsibility. Reserve for real pin semantics. |
| `retain` / `release` | Reserve for real refcounted APIs only. Do not use as vague synonyms for `pin` or `destroy`. |
| `out` / `out_*` | Caller-provided output storage. Pair pointer outputs with a count/capacity when applicable. |

## Pointer Inputs

Input pointers are borrowed for the duration of the call by default. If the
callee stores a pointer or anything derived from it, the API must state one of:

- copied now; caller may release its input after return
- stored by reference until a named unregister, clear, destroy, or shutdown event
- taken; caller must not free/use after the documented transfer point
- pinned; backing storage stays engine-owned but the API extends its lifetime

Do not silently store caller pointers. Stored-by-reference is allowed when it is
the simplest and cheapest design, but the contract must say what must remain
pointer-stable and for how long.

## Pointer Returns

Pointer returns fall into one of these shapes:

- borrowed view into engine/module storage; caller never frees it
- caller-owned allocation; caller frees through the documented function
- caller buffer filled by the callee; caller owns the storage
- optional result; `NULL` is a documented absence/failure result

If a pointer return is non-NULL by contract, assert the invariant before
returning rather than silently returning a null pointer. If absence is normal,
use `NULL`, `false`, or `NT_ERR_*` as the public result and document it.

## Views

Use `nt_*_view_t` for borrowed pointer-plus-count snapshots such as SoA bulk
component views. A view is not a container owner. It should normally be passed
by value and should not be stored unless the API states the backing storage is
stable for that use.

When a view points into frame scratch, its lifetime is exactly until the next
`nt_mem_scratch_reset()`, not generically "until next frame".

## Callbacks And User Data

For callbacks, treat `(fn, user_data)` as one registration unit. The API must
state:

- whether `user_data` is stored or only passed through during the call
- how long stored `user_data` must remain valid
- whether there is a destroy callback, and whether it runs exactly once
- whether the callback may call back into the registering module

Callback scope should use operational wording: "only during this call", "until
unregister", "until context destroy", "until shutdown", or "until the destroy
callback fires".

## Handles

Generational handles are values, not owned pointers. Passing a handle does not
transfer ownership of the backing resource. An API that destroys, unregisters,
invalidates, or releases backing state must say so explicitly.

Virtual-resource APIs that publish a runtime handle do not automatically own or
destroy the runtime object unless the function says that the handle is taken.

## Hot Path Rule

In hot paths, prefer handles, borrowed views, caller-provided output storage, or
preallocated module storage. Allocation, deep copy, and ownership transfer
belong in init, builder, loading/resolve, debug/devapi, or explicitly non-hot
APIs. A style fix must not add heap work to a hot path.

## Existing Canonical Examples

- `nt_resource_find`: pure lookup naming.
- `nt_resource_get_blob`: borrowed blob view wording.
- `nt_resource_peek_user_data`: short-lived borrowed slot pointer naming.
- `nt_ui_state`: retained UI view-state wording.
- `nt_ui_probe_collect`: caller-buffer output with cap/truncation wording.
- `nt_ui_probe_collect_owned`: context-owned scratch wording.
- `nt_gfx_read_pixels`: caller-provided buffer with capacity wording.
- Builder encode/bridge helpers: caller-owned heap output with module-free
  wording.

Use these as wording patterns, then keep the exact lifetime and invalidation
event in the owning header or module chapter.
