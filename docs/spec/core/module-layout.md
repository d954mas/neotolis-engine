# Module Layout

Directory layout of engine modules and the interface/impl/stub composition model
for swappable modules (log, input, http, gfx, window, app, fs, clipboard).
Consumers link header-only interface targets; each executable picks exactly one
impl at link time — omission is a loud link error, enforced by CI scripts.
Includes stub semantics and runtime capability queries.

Related: [Core Principles](principles.md), [Platform Architecture](../runtime/platform.md), [Logging, Errors, Debugging](../debug/logging-errors-debugging.md)

> The tree below is the conceptual module grouping. Some leaf names differ from
> the actual source dirs (e.g. `engine/resource/`, and the builder lives at
> `tools/builder/`); the authoritative source-dir → chapter mapping is the module
> map in [index.md](../index.md).

A fixed module is a single directory with its header + TU(s).

A **swappable** module is a directory with a public interface header plus
per-platform impl subdirs (`native/`, `web/`, `stub/`); the executable picks one.

```text
engine/
    core/
    memory/
    log/                    # swappable
        nt_log.h
        default/            # real impl (note: "default", not "native")
        stub/
        CMakeLists.txt
    input/                  # swappable
        nt_input.h
        native/  web/  stub/
        CMakeLists.txt
    clipboard/              # swappable
        nt_clipboard.h
        native/  web/  stub/
        CMakeLists.txt
    fs/                     # swappable: nt_fs.h + native/ web/ stub/
    http/                   # swappable: nt_http.h + native/ web/ stub/
    window/                 # swappable: nt_window.h + native/ web/ stub/
    app/                    # swappable: nt_app.h + native/ web/ stub/
    graphics/               # swappable: nt_gfx.h + gl/ + stub/ (real impl dir is "gl")
    ui/
    font/
    debug_overlay/          # dev HUD — consumes nt_metrics (frame time / draw calls / user counters)
    metrics/                # L1 perf source of truth (host pushes per-frame data; overlay + perf.* read)
    platform/               # OS process/memory probes; native/ web/ (configure-time split; header is core/nt_platform.h)
    resources/
    packs/
    formats/
    audio/
    devapi/    (dev-only, NT_DEVAPI_ENABLED — see debug/logging-errors-debugging.md)

deps/
    cjson/     (vendored standalone lib, EXCLUDE_FROM_ALL; linked only by consumers)

builder/   (separate program)

game/      (game-side code)
```

## Module composition (interface / impl / stub)

Every swappable module exposes a header-only `nt_X_interface` target, declared
via `nt_declare_interface()` in `cmake/nt_module.cmake`. It carries ONLY the
public header + the engine include root — no TU, no symbols.

Two or more concrete targets implement that interface with identical signatures:
the real `nt_X` and the `nt_X_stub` (the web/native variant of the real impl is
selected inside `nt_X`'s own `CMakeLists.txt`).

Consumer modules link ONLY `nt_X_interface` — never a concrete impl. The
**executable** selects exactly one impl per interface in its
`target_link_libraries`.

Omitting an impl is a LOUD unresolved-symbol link error, not a silent no-op.
Two gates enforce this:

- `scripts/check_no_real_impl_links.sh` — scans every `engine/**/CMakeLists.txt`
  and fails if any engine module links a real swappable impl (it must link the
  `_interface` instead). Re-poisoning the link graph would make stubs unreachable.
- `scripts/check_link_failure_loud.sh` — compiles a throwaway TU that calls
  `nt_log_write` but links NO `nt_log` impl, and asserts the linker reports an
  undefined reference. Proves omission fails loudly (exit 0 only when the
  expected unresolved-symbol error is observed).

Current swappable pairs: `nt_log`, `nt_input`, `nt_http`, `nt_gfx`,
`nt_window`, `nt_app`, `nt_fs`, `nt_clipboard`.

**Why link-time, not compile-time.** Selection happens at LINK time. This
replaced the older `NT_MODULE_X` `#define` + provider-fn-ptr + weak-symbol
approach. Link-time selection keeps each consumer compiled exactly once (one
artifact reused across executables), and makes the choice explicit and
gate-enforced instead of buried in per-TU macros.

## Adding a swappable module

1. Create the interface header `nt_X.h` (public API only).
2. Declare the interface target: `nt_declare_interface(nt_X)` in the module's
   `CMakeLists.txt`.
3. Write `native/`, `web/`, `stub/` impls with identical signatures.
4. Add `nt_X` to the `SWAPPABLE` list in
   `scripts/check_no_real_impl_links.sh`.
5. Each executable picks exactly one impl in its `target_link_libraries`
   (the real `nt_X` or `nt_X_stub`).

## Stub semantics and capability queries

A stub is a Null Object: the API is always present, the impl is inert.

A no-op / empty stub is safe ONLY when the caller already treats "nothing
happened" as a legitimate runtime state, not an error:

- `nt_log` — a dropped log is functionally invisible.
- `nt_input` — a frame with "no events" is already a normal, handled case.
- `nt_clipboard` get/paste — an empty clipboard is a real state the text field
  already tolerates (paste of "" is a no-op).

A no-op stub is NOT sufficient when either:

1. the capability is user-facing, so a silent no-op looks like a bug; or
2. an operation pairs a destructive LOCAL half with the backend — e.g. cut
   deletes the selection locally AND stores it remotely; a no-op store turns cut
   into silent DATA LOSS.

For those cases expose a runtime capability query `bool nt_X_available(void)`
(real → `true`, stub → `false`) and let the caller branch: skip the destructive
half, or gray out the affordance. `nt_clipboard_available()` is the engine's
example — the text field makes Ctrl+X a no-op when it returns `false`.

Availability is a link-time/runtime fact, so it MUST be a linked symbol
(function), NEVER a `#define`. A macro is compile-time and per-TU; it cannot
differ per-executable for a consumer that is compiled once.

This matches established practice: SDL `SDL_HasClipboardText`, GLFW
`glfwVulkanSupported`, Godot `DisplayServer::has_feature(FEATURE_CLIPBOARD)`.
