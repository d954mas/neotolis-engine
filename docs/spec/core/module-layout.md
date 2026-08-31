# Module Layout

Directory layout of engine modules and the interface/impl/stub composition model
for swappable modules (log, input, http, gfx, basisu, meshwire, window, app, fs, clipboard).
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
    basisu/                 # swappable: nt_basisu_transcoder.h + stub/ (real impl is a top-level C++ TU)
    meshwire/               # swappable: nt_meshwire.h + stub/ (+ builder-only nt_meshwire_encoder)
    postfx/                 # optional fixed helpers over nt_gfx_interface
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
`nt_basisu_transcoder`, `nt_meshwire`, `nt_window`, `nt_app`, `nt_fs`,
`nt_clipboard`.

`nt_basisu_transcoder` is the size-motivated pair: the real impl is the C++
Basis Universal transcoder (plus the C++ stdlib on wasm), the stub keeps a
texture-less executable C-only. The builder (`tools/builder`) and
`test_basisu_roundtrip` link the real impl directly — they are executables
picking an impl, not engine modules, so the no-real-impl gate does not apply.

`nt_meshwire` follows the same size-motivated shape: the real impl is a C port
of the meshopt index codec (stream format v1) plus the SoA re-interleave loop;
the stub keeps an executable without builder-packed meshes free of the decoder.
The encoder lives in a separate TU/target (`nt_meshwire_encoder`) linked by the
builder and the codec tests; runtime binaries never link it, so they carry no
encode code structurally. The
vendored upstream C++ codec (`deps/meshoptimizer`) is a TEST-ONLY byte-parity
reference compiled solely into `test_meshwire_diff`.

Fixed helper modules may sit above a swappable interface without selecting its
implementation. `engine/postfx` is optional and currently starts with
`nt_postfx_blur`. It links `nt_gfx_interface`; each executable or test still
selects the concrete gfx implementation (`nt_gfx` or `nt_gfx_stub`) at the link
layer.

`nt_postfx_blur` is a gaussian blur helper, not a post-processing graph. It
borrows ready source, temp, and destination handles for each call; their
dimensions must match. The helper owns its shader stages, program, pipeline, and
fullscreen primitive, but it does not allocate, resize, destroy, or retain
caller handles.
The source uses a `sampler2D` color format (`R8`, `RG8`, `RGB8`, `RGBA8`,
`RGBA16F`, or `RGBA32F`); integer and depth formats are invalid. `temp` and
`dest` are distinct ready `RGBA8` targets matching the source size. Scissor
must be disabled for the call. The helper leaves scissor disabled and does not
restore prior graphics bindings.
Blur arguments and readiness of caller-supplied GPU handles are preconditions
and assert when violated, as does a link failure in the helper's program.
Initialization and restore return `NT_ERR_INIT_FAILED` for shader, buffer, or
pipeline backend creation failures, including failures with a live context, and
when context loss prevents program or pipeline creation. A failed restore leaves
the module initialized but unable to draw: passes skip until the game retries
`nt_postfx_blur_restore_gpu` successfully.

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
4. Each executable picks exactly one impl in its `target_link_libraries`
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

For those cases there are two remedies; pick by who can act on the absence:

**Runtime capability query** — `bool nt_X_available(void)` (real → `true`,
stub → `false`), letting the caller branch: skip the destructive half, or gray
out the affordance. `nt_clipboard_available()` is the engine's example — the
text field makes Ctrl+X a no-op when it returns `false`. Use this when the
caller is code with an affordance to disable and "not available" is a
legitimate composition the game ships with.

**Loud-fail stub** — every entry point asserts (`NT_ASSERT(0 && ...)`) and
returns its failure value, so under `NT_ASSERT_MODE=OFF` the caller's existing
error path still runs (log + failed asset), never a silent no-op. Use this when
hitting the stub is a build-composition BUG, not a state anyone can branch on:
the caller is a data-driven loop with no affordance to gray out, and the data
that reaches the stub should never have been shipped with it.
`nt_basisu_transcoder_stub` is the engine's example — a BASIS-compressed
texture in a pack while the stub is linked means the build packed basis content
but did not link the transcoder; the game dev must fix the composition, so the
stub traps at the first activation. No `nt_basisu_available()` query exists,
deliberately: offering one would invite masking that bug with a fallback.

Availability is a link-time/runtime fact, so it MUST be a linked symbol
(function), NEVER a `#define`. A macro is compile-time and per-TU; it cannot
differ per-executable for a consumer that is compiled once.

This matches established practice: SDL `SDL_HasClipboardText`, GLFW
`glfwVulkanSupported`, Godot `DisplayServer::has_feature(FEATURE_CLIPBOARD)`.
