# Logging, Errors, Debugging

Log levels, the NT_ASSERT contract policy (TRAP/FULL), fatal-vs-recoverable
error policy, and the metrics-fed debug overlay. The bulk documents the dev-only
devapi: transport model, time/render control (`nt_app`), input and UI
automation, observability commands (log/perf/entity/resource), the entity-write
group, frame capture, and the compile-time option catalogue.

Related: [Module Layout](../core/module-layout.md), [Input](../io/input.md), [Resource System](../assets/resource.md)

## Logging levels

`NT_LOG_MIN_LEVEL` selects the compile-time floor: numeric `0` INFO, `1` WARN,
`2` ERROR, `3` NONE. Plain CMake defaults to INFO regardless of build type.
Debug presets and `native-release-test` select INFO; production Release presets
select WARN. Set the cache value when building the engine, not only a define on
the final executable. `nt_log_interface` publishes the value to its consumers.
Asserts are a separate policy, below.

Below-floor ordinary, domain, ONCE and UNIQUE macros retain no arguments,
format strings or ONCE latch. Disabled UNIQUE is a boolean `false` expression.
Uppercase domain macros still require `NT_LOG_DOMAIN` or `NT_LOG_DOMAIN_DEFAULT`.
Enabled ONCE keeps its call-site latch semantics, including a runtime-filtered
first call. `nt_log_set_level` can further suppress enabled levels.

Direct `nt_log_write` and `nt_log_write_unique` filter before formatting, but C
still evaluates their caller arguments. `nt_log_entity` skips its internal
entity description below the floor; independent introspection remains usable.
At NONE the normal `nt_log` target uses the existing inert stub source and has
no logger formatting/dedup storage. `nt_log_stub` remains useful for link-time
suppression of prebuilt libraries; it cannot remove their caller-side work.

## Optional measurements

`NT_UI_TIMING_ENABLED`, `NT_GFX_GPU_TIMING_ENABLED`, `NT_GFX_CAPTURE_ENABLED` and `NT_RESOURCE_TIMING_ENABLED` are independent CMake options, all OFF by default regardless of build type. Debug presets and
`native-release-test` select ON; production Release presets select OFF.
UI/gfx interfaces and the `nt_resource` target publish numeric 0/1 values.
Gfx counters (always built) and capture are described in [frame observation](../render/architecture.md#frame-observation). The devapi `render.info` command runs early
in a frame, so its `draw_calls` is the last closed frame's
(`g_nt_gfx.last_frame`); draw calls count recorded draws, after merging. The `nt_metrics` `draw_calls` channel is whatever the
host pushes: most example hosts push `nt_gfx_draw_calls(&g_nt_gfx.counters)` after rendering,
inside the same frame; `examples/devapi_host` draws nothing and pushes 0.
The resource header does not require the flag; only its implementation checks it.
No producer depends
on `NT_METRICS_ENABLED`: the game may consume measurements directly.

UI timing OFF removes the layout/build/walk clock reads and three private
results. The existing getters require a non-NULL context and return zero.
ON measures Clay finalization, tree building and the main walk; a separate
inspector walk does not replace the main result. Main-walk early-outs reset
only the walk result.

Resource timing OFF removes parse/CRC/activation/step results and extra clocks;
its getters return zero. Budget, retry and eviction still read operational time.
Resident pack bytes are queried from existing state even with timing OFF. See
[resource measurements](../assets/resource.md#optional-measurements-and-resident-bytes)
for last-call semantics and nested duration boundaries.

GPU timing OFF removes timer rings, timer-extension probes and query calls.
Segment calls are inert; supported returns false and poll returns false
with a zero output. Segment names must have static lifetime in every
configuration: with timing ON the begin call records the pointer until the
stream executes. Segment names and poll output pointers must be non-NULL;
violations assert even with timing OFF, the stub, or a lost context. Segment
calls outside an open frame assert with timing OFF or a lost context too, but
not in the stub.
With timing compiled ON, the GL implementation leaves output unchanged on an
unsuccessful poll. `nt_gfx_stub` always returns
false and zeroes output, regardless of the timing flag.
The runtime choice starts enabled and survives context loss. The toggle asserts
unless called between `nt_gfx_end_frame` and the next `nt_gfx_begin_frame`, with
timing OFF too (not in the stub): recorded segments execute at `end_frame`. Runtime disable
closes an active segment and its native debug group, cancels pending samples,
and retains query objects until shutdown. Re-enable starts fresh samples.
Dead-context cleanup performs no GL calls. Support reports capability,
independently of the runtime choice.

WebGL disjoint status is read once at begin-frame only while timing is enabled
and an active or pending query exists. Disjoint closes an active segment and
invalidates all pending samples. An old disjoint event latched during idle may
conservatively discard the first new batch. Context creation disables automatic
extension activation; existing capability probes explicitly enable the texture
and float extensions, and the compiled timer producer enables its own extension.

Example hosts compile out metrics-only clocks, memory sampling and sample
preparation with metrics OFF. GPU segments also require GPU timing ON. With
metrics ON and GPU timing OFF they report `gpu_ms = -1`; UI timing OFF omits
the UI duration user channels.

Missing log-floor, metrics, log-ring or introspection configuration is a header
error, not an implicit development configuration. Introspection read/write
values propagate through `nt_core`, so external component consumers share the
same layout without linking the introspection implementation. Devapi group
choices are dormant without warnings while its master switch is OFF; enabled
groups retain their dependency errors.

## Assert policy

Asserts are contracts, not error handling. A failed assert means the program is broken beyond recovery — continuing would mask bugs.

- **NT_ASSERT** — single macro, two compile-time modes via `NT_ASSERT_MODE`:
  - `1 (TRAP)` — `__builtin_trap()`, no strings, minimal binary impact. The CMake default and the production Release presets select this mode.
  - `2 (FULL)` — hookable handler with `expr/file/line` strings. Debug and release-test presets select this mode. Tests use the handler to catch and verify assert failures via `setjmp`/`longjmp`.
- `nt_core` exports the selected mode to consumers. `nt_assert.h` requires it explicitly; `NDEBUG` and `NT_DEBUG` do not select or alter it.
- Production Release presets select TRAP (1): contract violations crash immediately instead of continuing with corrupted state. No string bloat, no handler overhead — just a single branch + trap instruction per assert.
- Assert expressions are side-effect-free. A failed assert never continues in either mode; do not duplicate its condition with recovery or a log-and-return path; a failure record (below) is the only log allowed before it. OFF (0) is rejected by CMake and the header.
- Failure record: TRAP keeps no assert text and a field report keeps only logs, so a release crash names at most a function. One ERROR record before the assert keeps what identifies the cause; it never adds recovery. A backend check that a context loss can explain asks for the loss again after its record, so a loss surfaced by the record's queries keeps the documented context-loss outcome. It is required for every check of a result the device or driver returned, naming the object, the failed phase and the values the driver returned, including checks a bad shader also fails (program link and reflection): in the field the two causes look the same. Other asserts add one only when the function alone cannot name the cause, such as a sampler without a texture or a stream overflow.
- Hard guards are required at untrusted/runtime-input boundaries and wherever a public API promises recoverable rejection; those guards implement the API contract independently of diagnostic configuration.
- Never use asserts for conditions that can legitimately occur at runtime (missing files, user input, network errors) — those are error handling (see below).

## Error policy

### Fatal

- backend init failure
- unsupported critical format version
- impossible startup state

### Recoverable

- missing texture → placeholder
- material without a ready program → renderers skip it (there is no placeholder material)
- resource load fail → asset state failed + log
- audio decode failure → clip state failed + log

## Engine version

The engine version is its git commit. `nt_version_engine()` (opt-in module `engine/version`) returns the first 8 hex of the engine commit, with `-dirty` when tracked files differ from `HEAD`, or `unknown` when git is unavailable, fails, or the engine directory is not the top of its own repository (a copy without `.git` would otherwise report the host repository's commit). There is no semantic version.

The engine itself prints no version. Examples log it at startup when INFO logging is enabled; `hello` prints it directly. Crash records and `engine.info` report it independently. See [build identity and crash records](../../build.md#build-identity-and-crash-records) for generation and rebuild behavior.

## Crash record

`engine/crash` (opt-in; nothing depends on it, so a game that does not link it pays nothing) keeps one text record of the last fatal crash. The engine records; the game decides what to send, where, and when. The engine never uploads.

**API.**

- `nt_crash_init(name, game_version)` — once per process. `name` is the `localStorage` key on web (several games can share one origin) and the record file path on native (UTF-8; a relative path resolves against the cwd at crash time). `game_version` must name the game build, so the record maps to that build's symbols. Both are copied, non-empty and shorter than 256 bytes. Init takes one of `NT_LOG_MAX_SINKS` log sinks and, on native, installs its own unhandled-exception filter and signal handlers, replacing earlier ones without chaining; a library that later installs its own replaces them in turn, so call init after such libraries are set up. It does not touch the stored record.
- `nt_crash_set_context(text)` copies optional game text into 1024 bytes; init and non-NULL text are required. Each call replaces the previous text; empty clears it. Truncation keeps at most 1023 bytes, possibly inside a code point (web shows the cut as U+FFFD); CR/LF become spaces, so the text cannot forge record lines. The caller may immediately free or change its source. Updates are main-thread, best effort if a crash interrupts them; the last byte always remains NUL.
- `nt_crash_load_previous(out, cap)` copies the stored record into caller storage as a string and keeps it; it returns the string length (0: no record, or the storage is unavailable or unreadable). The stored text is untrusted and returned as it is, cut at `cap - 1` bytes and at an embedded NUL. Load and clear require init.
- `nt_crash_clear_previous()` removes the record. The game clears only after a confirmed delivery; an unsent record waits for the next launch. A new crash overwrites the stored record.
- Web also dispatches window event `nt-crash` with `detail.text` once the record is captured and storage was attempted. The page is still alive, so a game can send immediately (best effort; the record stays stored, so a server dedups by text — it carries `time`).

**Record.**

```
game: <game_version>
engine: <nt_version_engine()> <preset>    (e.g. 0595a717-dirty wasm-release)
gpu_vendor: <last observed vendor>       (optional)
gpu_renderer: <last observed renderer>   (optional)
gpu_api: <graphics API version>          (optional)
gpu_caps: <nt_gfx_gpu_caps_t as text>    (optional)
context: <copied game text>              (optional)
time: <unix ms>
reason: RuntimeError: unreachable | signal SIGSEGV | exception 0xc000001d | abort
pc: exe+0x1a2b3c          (native; absolute 0x... outside the executable)
stack:                    (web: the JS error stack, cut at 4096 UTF-16 code units)
...
log:
WARN [gfx] ...
ERROR [resource] ...
```

`log:` is the tail of WARN/ERROR lines from a fixed BSS byte ring of `NT_CRASH_LOG_BYTES` (default 8192). The minimum capacity reserves two maximum-size messages plus space for prefixes; domain lengths are not bounded, so this does not guarantee two complete lines. A wrapped ring drops its first line, which may be partial, and a long domain can leave no complete line. Configuration is described in [build identity and crash records](../../build.md#build-identity-and-crash-records).

GPU fields come from the gfx device snapshot ([Device identity](../render/architecture.md#device-identity)),
independently of the log floor and ring wrap. Enable crash before window/gfx init
to capture startup failures. Unknown GPU fields and empty game context are omitted.
The engine owns system fields; the game only supplies its context and transport.

A record is at most about `NT_CRASH_LOG_BYTES` + 4 KB on native. On web the reason and stack are cut at 256 and 4096 UTF-16 code units: about `NT_CRASH_LOG_BYTES` + 8 KB for ASCII text and + 16 KB for Unicode text, which can require up to 3 UTF-8 bytes per UTF-16 code unit. Invalid UTF-8 in log or diagnostic text can expand each byte into a 3-byte replacement character, increasing the web bound to about `3 * NT_CRASH_LOG_BYTES` + 20 KB. Size the game's buffer for its platform; a longer record is cut.

Lines filtered by `NT_LOG_MIN_LEVEL` or `nt_log_set_level` never reach the ring; with `NT_LOG_MIN_LEVEL=3` or `nt_log_stub` the section is empty. FULL assert text goes to stderr/console, not into the record — a crash-prone check that needs its cause in field reports logs one ERROR before the assert (see [Assert policy](#assert-policy)). The separate debug `nt_log_ring` is controlled by `NT_LOG_RING_ENABLED`; production Release presets disable it.

**Capture.**

- Web: window `error` and `unhandledrejection` listeners take the first engine crash per session: a `WebAssembly.RuntimeError` (a trap; `abort()` throws one too) or an error whose stack has wasm frames (a wasm stack overflow arrives as `RangeError`). Other page errors are ignored. The listener never calls back into the trapped instance: it reads the ring, current GPU fields and copied game context through current linear-memory views and writes `localStorage`. The shell's `Module.onAbort` overlay also fires on `abort()`; both are independent.
- Native: the handler writes the file and returns, so the OS default follows (WER on Windows; the original signal and core dump on Linux — the handler re-raises its signal). It uses only async-signal-safe calls (Win32 calls on Windows, where the filter and the UCRT SIGABRT handler run synchronously on the crashing thread). Windows: unhandled-exception filter (with a 32 KB stack guarantee for stack overflow) and SIGABRT; UCRT `abort()` then reports through WER; one record per process (a second faulting thread waits while the first writes, on both platforms). Linux: ILL, SEGV, BUS, FPE, ABRT, TRAP on a 64 KB alternate stack (skipped when the kernel needs more), fault PC from `ucontext` on x86_64 and aarch64. Linux older than glibc 2.34 sizes the alternate stack from `SIGSTKSZ`. Under ASan no handlers are installed; its report stays.
- No stack walk on native: the PC names the faulting instruction, and for `NT_ASSERT` in TRAP that is the assert. Full stacks come from WER dumps, cores or a debugger.

**Symbolization.** The game's `game_version` identifies the matching symbols. See [build identity and crash records](../../build.md#build-identity-and-crash-records) for symbol retention and lookup commands.

**Not captured:** a hard kill of the process or tab (OOM, "Aw, Snap", task manager); Windows `__fastfail` (/GS); crashes before `nt_crash_init`; Windows under a debugger; ASan/UBSan reports; errors swallowed by someone's `.catch`; cross-origin "Script error." without `crossorigin`; a JS error whose wasm frames fell past `Error.stackTraceLimit`; stack overflow off the main thread or without the alternate stack. Another wasm module on the page can trigger a false record. The log tail is best effort if a fatal signal or another thread's crash overlaps a log write. Firefox and Safari are unverified (the browser tests run Chromium).

## Debug overlay

`nt_debug_overlay` is a **pure consumer** of `nt_metrics` ([Observability](#observability-devapi-log--perf--entity--resource)): it formats an on-screen HUD (`nt_debug_overlay_format_lines` / `nt_debug_overlay_draw`) from `nt_metrics_fps()`, the last frame's cpu/gpu/draw_calls via `nt_metrics_last()`, and the user counters via `nt_metrics_user_count()` / `nt_metrics_user_get()`. It owns **no** measurement and **no** counter storage — the host pushes per-frame data into `nt_metrics` and the overlay reads it back. Because of that dependency, a CMake `FATAL_ERROR` guard requires `NT_UI_DEBUG_TOOLS` to be built with `NT_METRICS_ENABLED` (with metrics OFF the overlay would format from no-op stubs — an empty HUD); both options default OFF; presets enable each explicitly.

Recommended stats: frame time, fps, cpu/gpu time, draw call count, plus any game-supplied user counters.

## Developer API (devapi)

`nt_devapi` is an **optional, dev-only** self-describing command surface for engine introspection and automation (probing, testing, external tooling). It is gated by `NT_DEVAPI_ENABLED` (OFF by default); the `engine/devapi` subdirectory is excluded at the CMake level when off, so a devapi-OFF binary contains zero devapi code or symbols. A CI "zero-delta" check asserts a devapi-OFF WASM has no `nt_devapi_*` symbols. devapi is the one sanctioned exception to the runtime's no-parser rule — it is dev-only and compiled out of shipped builds.

**Transport-agnostic core.** The dispatch core is `submit(line) -> response line`: one JSON request line in, one JSON response line out, with no platform/socket/transport code. A command may also **defer** its reply: `submit` returns `NULL`, and the reply is yielded later by the **drain** (`nt_devapi_poll_response()`) once the command's **gate** opens — `g_nt_app.frame` advanced by `count`/`frames` (`time.step`, `frame.wait`), `g_nt_app.time` advanced by `seconds` (`time.wait`), or the next `nt_devapi_frame_rendered()` (`capture.*`). `nt_devapi_update()`, which the host calls once per tick, **checks** the gate before the transports drain, so native and web judge the same state; a clock reply is `{frame,time,dt}` read the tick its gate opens. Readiness compares against the game clocks, so it counts real simulation advances, not drain calls.

**A deferred reply resolves or fails, never waits forever.** Each check tests readiness first. A reply that is not ready fails with `stalled` when its gate can no longer open: a clock gate whose time mode differs from the mode at submit (tested before the deadline, so RUN frames never complete a manual step), a frozen sim (RUN paused, or MANUAL with no queued step), RUN scale 0 for a game-seconds gate, or a whole frame without `nt_devapi_frame_rendered()` for the rendered gate (rendering off, or a host that skips the call): the first check after the request waits, the second fails it, so the host calls `nt_devapi_update()` once per frame.

**A clock check reads `g_nt_app`, not who changed it.** A pause or mode switch from game code or a host fails the reply the same way a bot's would. A change that game code sets and undoes between two checks is not seen, except that any real mode switch drops the queued manual steps, so a pending `time.step` then fails at the next check.

**Platform stalls end at the caller's deadline.** A hidden tab (rAF stops), a host that skips `nt_devapi_update`, a trapped wasm module, or a scale too small to reach the deadline leave the reply pending. The client has no read timeout, because it cannot know how long a request takes on a given frame cost and display; the caller's outer deadline (a CI step, a test runner, a shell) ends the session. The next client takes over: a new native connection replaces the old one (so any connection to the port drops a live bot), and `PlaywrightTransport` calls `reset()` when it is created; on web the replaced client gets no error, so drive one page with one client. The engine keeps an abandoned `time.step`'s queued steps (game state); `time.set_mode` to `run` drops them, and `devapi_host` does so in its reset hook.

**One deferred reply in flight.** While a deferred reply is pending — until the drain yields it — every well-formed command for a registered method answers `busy` (a batch answers it per entry). A bot waits for the reply before its next command; a reconnect (native) or `reset()` (web) drops the pending reply.

**Unified transport model.** The two real transports (loopback TCP on native, a JS bridge on web) are separate, opt-in libraries that share this one core. Two parallel transports wrap the **one** core dispatch — not a single swappable interface, but two thin libraries that both feed `submit` and drain `poll_response`. The core (`nt_devapi`) owns `nt_devapi_update()` and the deferred drain; each transport sets its own per-tick poll (or nothing) in the core's one **transport-poll slot**, so both transports run the **same** shared drain and neither is hard-referenced by the core.

- **Native — loopback TCP (`nt_devapi_net`).** A single-client, JSON-lines, non-blocking TCP listener. The newest connection wins: it replaces a served client (a disconnect of the old one), so a reconnect is served even before the old close is read, and a hung client cannot lock the port. `nt_devapi_net_start(port)` binds **`127.0.0.1` only** (`INADDR_LOOPBACK`) on the port the host passes; a socket/bind/listen failure (the port may be taken) returns `false` with the cause logged — a recoverable outcome, not an assert; success logs the port at INFO. The host picks the port: a fixed value, its CLI, or `nt_devapi_net_port_from_env(fallback, &port)`, which reads the `NT_DEVAPI_PORT` env var (`fallback` when it is unset or empty) and returns `false` with the cause logged on a value that is not a port in 1..65535. `devapi_host` and `skeletal_showcase` use the env var with `NT_DEVAPI_DEFAULT_PORT` as the fallback, so the scenario launcher gives each process its own port. It sets its poll (non-blocking accept + recv + framed dispatch + send + deferred drain) as the core's transport poll, so `nt_devapi_update()` is the host's one per-tick call (the poll runs before the tick hook). The sole socket boundary in the layer — all `_WIN32` (Winsock) vs POSIX `#ifdef` code lives in this one module. `nt_devapi_net_wait_for_client(timeout_ms)` is an opt-in pre-loop gate for a game whose bot must push setup before frame 0: it blocks until a client connects or the timeout elapses; `devapi_host` does not call it.
- **Web — JS bridge (`nt_devapi_web`).** Push/pull instead of a socket: the host calls `nt_devapi_web_install_shim()` after `nt_devapi_init` and group registration, and JS on the **same page** then reaches `window.__devapi = {submit, poll, reset}` (present means ready). `submit(line)` returns the synchronous response line, or `""` when the command deferred; `poll()` returns one queued deferred response line, or `""`. `reset()` drops that client's pending deferred reply and runs the same reset hooks as a native disconnect. The bridge carries lines only and knows no command; time advance belongs to the client, the same as over the socket. The names are quoted, so they survive Closure (`NT_WASM_CLOSURE`, ON in `wasm-release`) and a devapi-enabled release build is driven the same way. There is **no socket and no listener** — the bridge is driven by the host page's JS, so the web transport registers **no** tick poll (it is push-driven); the core's deferred drain still delivers deferred replies for the JS side to pull.

**Security model — native vs web.** This is the headline: **there is no open devapi port in the browser.** On native, the listener is a **loopback `127.0.0.1`-only** socket — never reachable off the machine. On web, there is **no listener at all**: `window.__devapi` is callable only by **same-page JS** in the same origin, and only when the build defines `NT_DEVAPI_ENABLED` (OFF by default — with it OFF the whole `engine/devapi` subdir is compile-excluded, so a shipped web build carries zero devapi code or symbols). A devapi-enabled web build opens no network surface; an attacker would already need to be running same-origin script in the page.

**Self-describing registry.** A group or game registers a static table of commands in one call: `nt_devapi_register(cmds, count, user_data)`. Each `nt_devapi_command` is `{method, summary, params_shape, result_shape, fn}`; every field is required, and the table, its strings and `user_data` are stored by reference until shutdown (no copies, no heap) and the engine never frees them. Every handler in the table receives the table's `user_data` (a deferred reply runs with `params` and `user_data` NULL). A duplicate method asserts. `summary` carries what a client needs beyond the shapes: any side effect, and whether the command replies later (a deferred command says "replies once ..." or "at the next rendered frame"). The discovery commands are part of the framework: `nt_devapi_init` registers them, so every host is self-describing; `endpoints` lists every command with all four descriptor fields and `command.describe` returns one, so a client reads the whole surface without source; which commands exist depends on which groups are compiled in (engine) and what the game registers. A game registers its `game.*` commands through the public API with zero engine edits.

**Envelope.** Each request returns `{ok:true,result}` or `{ok:false,error:{code,message}}`, echoes `request_id` unchanged, and a JSON-array line runs as an ordered batch with continue-on-error. A **deferred** command emits no synchronous envelope — its envelope (the result, its own error, or `stalled`) arrives later from the drain; a deferred command is **not** allowed inside a batch (it is rejected with a `bad_params` entry, since a batch is one ordered response array). Core error codes: `bad_params`, `unknown_method`, `busy`, `stalled`; a group lists its own codes in its section. A failing handler fills its `nt_devapi_error` and returns false in one statement: `return nt_devapi_fail(err, "...");` answers `bad_params`, `nt_devapi_fail_code(err, code, "...")` another code (the dispatcher copies both strings as soon as the handler returns). A handler that returns false without filling `err` asserts.

**cJSON dependency.** devapi parses/serializes with vendored cJSON, exposed as a standalone reusable `cjson` static-lib target (not an engine module, `EXCLUDE_FROM_ALL`). cJSON is usable by games independently of devapi; only targets that link it pull it in.

## Time & render control (`nt_app`)

`nt_app_run(fn)` is a **single, flag-aware frame loop** that owns the one `g_nt_app.dt` scalar. Its default state — mode `RUN`, `scale` 1, unpaused — is a plain wall-clock advance (`dt` = clamped wall delta, `frame++` every iteration), so a game that never touches the controls runs exactly as a minimal loop would. The game still wires poll/input/update/render inside `fn`; the engine gives building blocks, not a pipeline.

Time control is a set of flags in `g_nt_app`, set via the `nt_app_*` mutators (and exposed to a bot through the devapi `time.*`/`render.*` commands):

- **mode** `RUN` / `MANUAL`. RUN advances on wall time; MANUAL advances one fixed-`step_dt` step per queued step (`nt_app_step(count)` queues `count`), with no wall clock and no `max_dt` clamp — the byte-reproducible lockstep contract. `nt_app_step` is MANUAL-only (it asserts), and `nt_app_set_mode` to a different mode drops the queued steps, so a later return to MANUAL starts with none queued.
- **paused** (RUN only): `dt` = 0 and `frame` frozen while `fn` keeps running.
- **scale** (RUN only): multiplies `dt` for slow-mo / fast-forward **observation** — explicitly not a determinism primitive (reproducible fast runs use MANUAL lockstep).
- **render_enabled**: a loop-agnostic gate the game reads to skip its draw + present together.

`g_nt_app.frame` is a **simulation-advance counter**: it increments only on a real advance, so pause and manual-idle freeze it. Deferred devapi replies (`frame.wait`, `time.step`) are keyed to it — a wait resolves once `frame` reaches its submit-time deadline (`frame + N`), so it counts game frames, not transport polls, and fails as `stalled` once the sim freezes or the mode changes. `capture.*` is not keyed to it: it waits for the next rendered frame. `frame.wait` (an integer `frames`) is therefore RUN-only (rejected in manual/paused); in MANUAL a bot uses `time.step{count}`, which advances and replies once every queued step, including steps queued before it, is done. `frame.wait{frames}` and the step count, queued steps included, go up to `INT32_MAX` (the wrap-safe frame compare and the `int` step queue); above it is `bad_params`. `time.wait{seconds}` takes any finite value `>= 0`. There is no duration cap.

Game-elapsed time `g_nt_app.time` (a `double` — the sum of applied `dt`s, kept double so long deterministic runs don't lose seconds-resolution to float accumulation) is the **second clock**. `time.wait{seconds?}` defers on a game-time deadline (`time + seconds`, default 0 = resolve on the next `nt_devapi_update()`) rather than a frame count — the right tool in RUN, where the variable `dt` (and `scale`) make a frame count meaningless, and where time-authored mechanics (cooldowns, durations) live. Because game time only flows under RUN with `scale > 0`, `time.wait` is rejected in manual/paused/scale-0, and fails as `stalled` if time freezes while it waits. In MANUAL the two clocks are **linear** (`time = frames × step_dt`), so `time.step` accepts either `{count}` frames or `{seconds}` (= `ceil(seconds / step_dt)` frames) — convenience for the same time-authored logic in lockstep.

The L1 mutators **assert** their invariants (finite/non-negative scale, positive `step_dt`, valid mode) — callers are trusted game code; untrusted bot input is range-checked at the devapi L2 layer and returns `bad_params`, never an assert.

## Input automation (devapi `input.*` + player gate)

A bot drives input through the devapi `input.*` command group, which is a thin L2 veneer over an L1 engine capability (`nt_input_inject_*` + the player gate). The design rule is **bot-indistinguishable-from-human**: injected events flow through the same input-apply cores a real device hits, so the query API (`nt_input_key_is_*`, `nt_input_mouse_is_*`, `nt_input_pop_char`) cannot tell synthetic input from physical. Compiled out with the rest of devapi when `NT_DEVAPI_ENABLED` is OFF.

**Range-checked, never asserts.** Every `input.*` command validates its params and returns `bad_params` on any out-of-domain value (unknown key name, out-of-range pointer id, a button mask outside `[0,7]` or non-integral, malformed/invalid UTF-8, a frame count outside `[0,65535]`). The L1 inject API itself never asserts — it is driven by untrusted L2 — so bad bot input is always a structured error, never a crash.

**The command group** (all reply immediately, all fire-and-forget unless noted):

| Command | Params | Result | Kind |
|---|---|---|---|
| `input.key` | `{key, down?, hold?}` | `{ok}` | inject a key edge (`down` default true), or with `hold` a tap = down@0 + up@hold |
| `input.pointer` | `{action, id, x?, y?, type?, buttons?}` | `{queued}` | the pointer primitive: action `down`/`move`/`up` on a given id (default mouse type) |
| `input.move` | `{x, y, id?, type?}` | `{queued}` | sugar: pointer move on the default mouse slot |
| `input.click` | `{x, y, button?, id?, hold?}` | `{queued}` | sugar: pointer down@0 + up@`hold` (2 entries) carrying a button mask; `hold` default 1 frame (a realistic 1-frame-held click), `hold=0` = same-frame instant click |
| `input.wheel` | `{dx?, dy?, x?, y?}` | `{ok}` | scroll the mouse slot; with `x`/`y` a move to (x,y) then the wheel (self-contained, scrolls AT (x,y)), else at the slot's **apply-time** position (no slot → no-op) |
| `input.gesture` | `{id, type?, points:[[x,y]], frame_stride?}` | `{queued}` | sugar: down@0 + a move per subsequent point (`frame_stride` apart) + up; NO C interpolation — the bot supplies the path samples |
| `input.button` | `{buttons, id?}` | `{ok}` | set the mouse-button mask `{1,2,4}` on the given id at the slot's **apply-time** position (respects a pending `input.move` queued ahead of it; no prior slot → created at (0,0)) |
| `input.set_player_enabled` | `{enabled}` | `{enabled}` | toggle the player gate (see below) |
| `input.text` | `{text}` | `{queued}` | decode a UTF-8 string → codepoints and enqueue them into the char ring |
| `input.state` | `{key?, text?}` | `{down?, pressed?, released?, codepoints?}` | **READ** (not an enqueue) of the last sim-advancing poll — see below |

**The L1 player gate.** `nt_input_set_player_enabled(bool)` gates **real device** events at the apply seam: while disabled, the public real-input wrappers (`nt_input_set_key`, `nt_input_pointer_*`, `nt_input_wheel`, `nt_input_buffer_char`) early-return, so a physical device is suppressed — but injected events still flow, because inject calls the `*_apply` cores directly past the gate. The bot is therefore indistinguishable from a human to the query API while the real player is locked out. The **ON→OFF edge releases held current input state (synthetic and real are indistinguishable by design — bot == human)** so nothing sticks down across a focus-lost-style cutover: held keys raise their release edge, and held pointer buttons raise their release edge with **deferred deactivation** — the slot stays active one more poll so the release edge is readable via the public mouse query, then deactivates on the next poll (the same lifecycle as a normal pointer-up). These release primitives are the **changer's** tool (a graceful bot calls them, or the game does); the engine does **not** invoke them on a devapi client disconnect — applied input is game-owned (see the B-strict disconnect rule below).

**Scheduling lives in the devapi layer; `nt_input` is a pure apply layer.** `nt_input` knows nothing about frames or scheduling: its inject API is **immediate** (`nt_input_inject_key/pointer/wheel/text`), each call staging into a bounded static-BSS **immediate inject buffer** (`NT_INPUT_INJECT_QUEUE_MAX`, `-D` overridable) that `nt_input_poll` drains **whole** every poll, after the platform poll, through the same `*_apply` cores (gate-bypassing). The **frame schedule** is owned by the devapi input group (`NT_DEVAPI_INPUT_SCHED_MAX`, `-D` overridable) — exactly because the devtool is the only side that legitimately knows `g_nt_app.frame`. The `input.*` handlers enqueue into that schedule with their offsets (immediate commands at offset 0; `input.click` = down@0 + up@hold; `input.gesture` = down@0 + moves@(idx·stride) + up@last; a `hold` key = down@0 + up@hold; a UTF-8 string = one entry per codepoint at offset 0). A command reserves its entries **whole-or-nothing** against the schedule, so a near-full schedule can never leave a stuck pointer-down or a key with no release; overflow → `bad_params`.

**The input group is an OPTIONAL devapi module.** A single CMake switch, `NT_DEVAPI_GROUP_INPUT` (default ON), gates the WHOLE group consistently: it compiles `nt_devapi_input.c`, registers the `input.*` commands, links `nt_input` + enables the `nt_input` automation surface (player gate + inject pipeline, `NT_INPUT_AUTOMATION_ENABLED`), and wires the per-tick + client-reset lifecycle hooks. With the group OFF, the devapi core and transport carry **zero** input symbols, and `nt_input` itself is the lean apply layer (no gate, no inject buffer) — "use only what you need". The core never hard-references the group: it exposes **lifecycle hooks** with three ordered run phases — one frame slot (run before the transport, so a group can act on the frame `nt_gfx_begin_frame` just finalized before this update's commands), one transport-poll slot, then one per-tick slot — plus client-reset hooks, each set by a group's registrar exactly like its commands. The input group sets its schedule tick (which also takes the `input.state` copy on a frozen tick) as the per-tick hook and its schedule clear (no input release) as the reset hook; `nt_devapi_update` calls the frame, poll and tick slots and the transport's `close_client` runs the reset hooks, naming no group. Fail-fast is accepted: the group ON without an `nt_input` impl in the executable is an unresolved-symbol **link** error (the developer's responsibility), not a silent no-op.

**Advance-gated release.** `nt_devapi_update` runs every tick: it checks the pending deferred reply's gate, then runs the **frame hook**, the **transport poll** (recv + enqueue) and the **tick hook** (the input schedule release) — naming no group. The native transport registers `net_poll` in the transport-poll phase, so a line received this tick is enqueued into the schedule before the input group's tick reads it, regardless of registration order. The tick detects a real sim-advance by comparing `g_nt_app.frame` to its own last-seen value and, **only on an advance**, releases every entry whose countdown reached 0 into `nt_input`'s immediate buffer (decrementing survivors); the next `nt_input_poll` (same tick) applies that buffer post-edge-clear, so an injected rising edge survives to that frame's update. On a frozen tick (pause / manual-idle) the schedule releases **nothing** — synthetic input holds while the game is paused (real device input still flows through `nt_input_poll`). A client reset does not re-seed the last-seen frame: a native reset runs mid-tick, after `frame++`, and a re-seed would hide that tick's advance.

**B-strict disconnect ownership.** On a devapi client disconnect the engine resets **only devapi-owned transient state**: the inject schedule and the pending deferred reply — the gone bot's own pending bookkeeping, which must not bleed to the next client and is not game state. **Game-owned state is the changer's responsibility, never the engine's**: time mode/pause/scale, the render flag, the player gate, and already-**applied** input (a landed key/pointer DOWN) all stay as they were. The engine deliberately does not clobber them because L1 cannot tell whether the game or the bot set a given state — resetting it on a dev-client drop would violate code-first. A bot restores what it changed before disconnecting (its `finally`), or the host recovers explicitly: `nt_devapi_register_reset(fn)` runs `fn` whenever a client leaves (a native disconnect, a newer connection taking over, a web `reset()`), and the bare `examples/devapi_host` registers its OWN policy there (set RUN, unpause and drop queued steps; scale and the render flag stay) so an ungraceful drop mid-MANUAL can't leave it frozen — host application code, not the engine library.

**`input.state` is a DEV-ONLY observation read.** It returns the key state (`down` and edges) and text of the last **sim-advancing** `nt_input_poll` — so before a sim-advance an enqueued inject is **not yet visible** (the drain-race, machine-observable over the socket), and frozen ticks after the advance (pause, MANUAL idle) do not hide it: their polls still clear edges and text for the game, so the input group copies that poll's state lazily, at whichever comes first after it: a frozen tick, an `input.state` read or `input.set_player_enabled(false)`. `input.state` always reads the copy. Input that changes during a freeze (real device input during a pause) is not reported. `input.set_player_enabled(false)` takes the copy before its gate releases held keys, so the release never shows in the advancing poll's state. A client reset keeps the copy (it observes game input). With `key`, it returns `{down,pressed,released}` for that key (unknown name → `bad_params`). With `text:true` it returns that poll's text as a `codepoints` raw-codepoint array — only text the game left unread (a focused UI text field pops it first); a read consumes nothing. Taking the copy pops that text out of `nt_input`'s ring, so only the transports submit `input.state` and `input.set_player_enabled`: native before the tick's `nt_input_poll`, web between frames, where the next poll drops unread text anyway. A game calling `nt_devapi_submit` from its own update is not supported.

**`nt_input_poll(void)` contract.** No frame argument: `nt_input` is a pure apply layer that never includes `app/nt_app.h`. The host order is `nt_window_poll → nt_devapi_update → nt_input_poll → (game update)`. `nt_devapi_update` releases the due scheduled events into the immediate buffer (advance-gated), then `nt_input_poll` drains that whole buffer after the platform poll, in the same post-edge-clear window as the native char drain.

## UI automation (devapi `ui.*`)

A bot reads and drives UI through the devapi `ui.*` command group — a thin L2 veneer with no UI logic of its own. The **read** surface (`ui.tree`, `ui.element`, `ui.contexts`) is a projection of the L1 `nt_ui` probe; the **write** surface (`ui.click`, `ui.drag`, `ui.scroll`) resolves a target to a point and hands it to the **same** synthetic inject path `input.*` uses — there is no second injector. The whole group is gated by `NT_DEVAPI_GROUP_UI` (default OFF — opt-in) and compiled out with the rest of devapi when `NT_DEVAPI_ENABLED` is OFF.

**Build deps are hard, not silent.** Two CMake `FATAL_ERROR` guards keep the group from compiling into a vacuous or unbuildable state: `NT_DEVAPI_GROUP_UI` requires `NT_UI_DEBUG_TOOLS` (the probe has a real impl only under DEBUG_TOOLS — else a 0-node stub, a false green), and requires `NT_DEVAPI_GROUP_INPUT` (the writes reuse the input group's single inject scheduler; ui without input would link-fail on the reuse wrappers). The ui group is default **OFF** (opt-in, "use only what you need") so a plain `-DNT_DEVAPI_ENABLED` build stays lean and does not pull in UI debug tools; enable it explicitly alongside `NT_UI_DEBUG_TOOLS` + `NT_DEVAPI_GROUP_INPUT` (which stays default ON), and the two guards enforce both are present. The group registers **no** tick/reset lifecycle hook: scheduling is delegated to the input group's drain, and the only ui-owned state is the host context table, which is not transient (see below).

**Context registration & lifetime.** The host names its UI contexts in one static table, `nt_devapi_ui_set_contexts(list, count)` with `{name, ctx}` rows; the engine keeps **no** global ctx registry. The table is stored **by reference** (like command tables) and read only when a `ui.*` command runs, so the host may set it at any point and a later call replaces it; it **survives client reconnects**. Setting it is a trusted in-process host call (it **asserts** on a NULL name or ctx and on a duplicate name). Bot input that misses the table is always `bad_params`, never an assert. A command with no `ctx` resolves to the sole/first context. A ctx that has **not completed a frame** (`nt_ui_context_has_frame` false — its layout dims are 0 and the coord converters would trap) is rejected up front with `bad_params`, so no wire input ever reaches a degenerate converter.

**One coordinate space, read==write.** The group declares a **single** space: **Y-up, origin bottom-left, in ctx LAYOUT pixels** (not raw `g_nt_window.fb_*` — those differ under `nt_ui_scale`). `ui.tree`/`ui.element` bounds and `ui.click({x,y})` speak that **same** space, so a bot reads a widget's bounds and clicks its center with **no transform**. The ctx owns a device↔layout viewport (`nt_ui_set_viewport`, default identity = full screen); `nt_ui_begin` takes the **raw device** pointer and converts device→layout internally, while the writes map layout→device through that viewport — so a click lands correctly in scaled / letterboxed / DPR UIs. The sole Y-up→Y-down flip lives in the write path's target resolve. A **string id** resolves to the **same projected bounds center** the probe reports for that id (read==id-write), including 2D-affine and 3D-raycast contexts; a behind-camera or collapsed target (zero bounds) → `bad_params` rather than a click at a screen corner. Each read result carries a metadata block — `space`, `origin`, `y_axis`, `width`, `height`, `dpr`, `projection` (`2d`/`3d`), and a `viewport` rect (omitted for `3d`, where no device↔layout affine exists).

**Probe snapshot + truncation.** `ui.tree`/`ui.element` are an **immediate** read of the **last completed frame's** tree as flat, Clay-free POD nodes (`id`/`parent`/`role`/`text`/`label`/`bounds`/`visible`/`enabled`/`child_count`); strings are **copied** into node-owned fixed-cap buffers (the borrowed Clay pointer dies at the next `nt_ui_begin`). Nodes are emitted incl. invisible / offscreen / disabled — the bot filters, not the engine. The devapi read path (`nt_ui_probe_collect_owned`) walks into a **per-ctx arena scratch sized to the runtime `max_elements`** — Clay caps live elements at that same knob and each maps to ≤ 1 node, so a node-**count** overflow can never fire; `truncated` reflects **only** the internal DFS-stack depth limit (a tree nested past the walk's `STACK_CAP`). `NT_UI_PROBE_MAX_NODES` (tied to `NT_UI_DEFAULT_MAX_ELEMENT_COUNT`) bounds only the **legacy caller-buffer** `nt_ui_probe_collect` form, where the caller picks the cap and a count overrun is possible. Under truncation a not-found id returns an **honest "truncated"** error rather than "unknown id" — a dropped widget is not called stale.

**Range-checked, never asserts.** Like `input.*`, every `ui.*` command validates its params and returns `bad_params` on any out-of-domain value (non-finite or float-overflowing coords, an `{x,y}` target whose screen position is not a finite float, a fractional/negative frame count or one outside `[1, NT_DEVAPI_UI_DRAG_FRAMES_MAX]`, unknown ctx, unknown / stale id, a never-begun ctx). `ui.drag` additionally floors `frames` at **1**: the inject up releases at the pointer's current position, so a 0-frame drag (no move) would land the up back at `from` and silently drop `to`; `frames` ≥ 1 always emits at least one move that ends at `to`. The writes reuse the input scheduler's **whole-or-nothing** reservation — a multi-event command (`ui.click` = down@0 + up@hold; `ui.drag` = down + `frames` interpolated moves + up; `ui.scroll` = move + wheel) reserves **all** its slots or rejects, so a near-full schedule can never leave a stuck pointer-down or an unmatched edge. The bot supplies the drag path implicitly via `frames` (the handler expands the linear interpolation; `frames` is DoS-capped to fit the input scheduler). All writes are fire-and-forget — advance a frame to apply.

**The command group** (all reply immediately; reads have no side effect, writes are fire-and-forget):

| Command | Params | Result | Kind |
|---|---|---|---|
| `ui.tree` | `{ctx?}` | `{…meta, truncated, nodes:[{id,parent,role,id_string,text,label,child_count,visible,enabled,bounds}]}` | **READ** the last completed frame's tree (Y-up bounds, same space the writes take); `truncated` true **only** when the DFS-depth limit cut the walk (scratch is sized to the ctx element budget, so node count never truncates) |
| `ui.element` | `{id, ctx?}` | `{…meta, truncated, node}` | **READ** one node by developer string id; unknown/stale → `bad_params` (a miss under truncation says so) |
| `ui.contexts` | `{}` | `{contexts:[string]}` | **READ** the host-registered context names |
| `ui.click` | `{id\|{x,y}, hold?, ctx?}` | `{queued}` | resolve target → center → synthetic down@0 + up@`hold` (`hold` default 1 frame) |
| `ui.scroll` | `{id\|{x,y}, dx?, dy?, ctx?}` | `{queued}` | resolve target → center → synthetic move there + wheel(`dx`,`dy`) notches |
| `ui.drag` | `{from, to, frames?, ctx?}` | `{queued}` | resolve `from`/`to` → down@from + `frames` interpolated moves + up@to (handler expands the path; `frames` ≥ 1 and DoS-capped — a 0-frame drag emits no move so the inject up lands at `from`, not `to`, and is rejected `bad_params`) |

## Observability (devapi `log.*` / `perf.*` / `entity.*` / `resource.*`)

**`nt_metrics` is the Layer-1 perf SOURCE OF TRUTH.** The host measures the frame (it owns the loop / has time control) and pushes one `nt_metrics_frame_t` per frame into `nt_metrics_sample()` — `frame_ms`, `cpu_ms`, `gpu_ms` (`< 0` sentinel = no timer), `draw_calls`, memory, scratch — and sets game user counters via `nt_metrics_count` / `nt_metrics_count_f`. `nt_metrics` does the rest: it derives a rolling `fps`, windows every host-pushed channel for percentile aggregates (`pool_occupancy` is currently **unsampled** — no portable provider exists yet, so it reports `samples:0` / null aggregates until one does), stores the last-pushed frame for the immediate snapshot view, and keys user counters by the **full** 64-bit name hash with an **exact** tagged value (uint64 counts keep full precision past 2^53, floats keep their double). It reads **no** clock and references **no** other engine module — pure storage + math (its only deps are `nt_core` + `nt_hash`). Both `nt_debug_overlay` ([Debug overlay](#debug-overlay)) and the devapi `perf.*` group are **consumers** that read from `nt_metrics`.

> This supersedes the earlier "overlay measures the frame, `nt_metrics` pulls from the overlay getters" model: the dependency is now inverted — the host pushes raw data into `nt_metrics`, and the overlay + `perf.*` read it. There is one perf store, fed once per frame by the host.

A bot inspects engine state through the devapi **obs** command group — a thin L2 veneer that serializes the dev-only log ring, the `nt_metrics` perf collector, and the entity/resource enumeration accessors. Every obs command is a **pure immediate read**: it serializes the live L1 state on the same `submit` call and returns synchronously — **nothing in this group ever defers** (no `frame.wait`-style continuation), so all reply immediately with no side effect except `perf.reset`. Untrusted bot input is range/type-checked and returns `bad_params`; only host-call invariants assert.

**The command group:**

| Command | Params | Result | Kind |
|---|---|---|---|
| `log.tail` | `{n?, level?}` | `{entries:[{level,domain,msg}]}` | **READ** newest-first ring entries, up to `n` (integer `[0, NT_LOG_RING_DEPTH]`, default full depth), optionally filtered to `level` ≥ `info\|warn\|error` |
| `perf.snapshot` | `{}` | `{fps,frame_ms\|null,cpu_ms,gpu_ms\|null,draw_calls,user_counters:object}` | **READ** `nt_metrics`' last-pushed frame: `fps` is the rolling avg, `frame_ms` the real last frame time (distinct from `cpu_ms`), JSON `null` until the first valid frame (the host pushes `<= 0` before any `dt`), like `gpu_ms`; `gpu_ms` JSON `null` when the host pushed the `< 0` sentinel; `user_counters` carry their exact stored value (counts above 2^53 lose low bits to the JSON IEEE-754 double — a wire-format limit, not a store defect) |
| `perf.stats` | `{channels?, budget_ms?}` | `{channels:object,user_channels:object,fps_low_1pct,fps_low_01pct,over_budget_pct,budget_ms}` | **READ** windowed `nt_metrics` aggregates (`samples`/`avg`/`min`/`max`/`median`/`p95`/`p99`/`p99_9`; null aggregates when `samples:0`) per requested-or-all fixed channels + user channels; `budget_ms` (finite, > 0, default 16.67) drives `over_budget_pct` and is echoed back |
| `perf.reset` | `{}` | `{reset:true}` | clear the metrics window (counts → 0) without tearing down state |
| `entity.list` | `{offset?, limit?, component?, all?, any?, none?}` | `{total,entities:[{id,index,generation,enabled,<component>:{...}}]}` | **READ** live entities: core fields (`id`/`index`/`generation`/`enabled`) plus **each present component as a named group** by the generic `nt_entity_introspect` walk — a component with no `describe()` still emits an empty `{}` (presence visible; a marker component is filterable + shown). The obs layer names no component, so a new one appears here with zero edits. Component-set filter: an entity passes if it has **every** `all` + **at least one** `any` (when present) + **none** of `none` (`component:"x"` is sugar for `all:["x"]`); an unknown component → `bad_params`. No world matrix; fully paginated against the honest `total` (two heap-free passes) |
| `resource.list` | `{offset?, limit?, pack_id?, include_assets?}` | `{total,packs:[{id,state,priority,asset_count}],assets?:[{resource_id,type,state,pack_index,blob_pins}],asset_total?,assets_truncated?}` | **READ** mounted packs (paginated with `total`); a flat `assets[]` only when `include_assets`. `pack_id` filters **both** packs and assets. `resource_id` is a `0x`-hex string (a 64-bit hash can't round-trip through a JSON double); `pack_index` is the raw packs[] slot (not the public `pack_id`); `blob_pins` is the pack's PIN_BLOB aggregate pin count (nonzero only for the published winner of a PIN_BLOB slot); the flat `assets[]` is DoS-capped, with `asset_total`/`assets_truncated` reporting the honest scope vs the emitted prefix |

`offset`/`limit` (and `n`, `pack_id`) are parsed **exactly**: a non-finite, fractional, or out-of-range number is `bad_params`, never silently truncated.

**Component group contents + dev-only asset names.** Each component's `describe()` fills its named group (e.g. `transform:{position,rotation,scale}`, `drawable:{visible,color,tag,tag_name?}`). Identity of a backing asset is resolved WITHOUT per-component storage: a resource-backed component (mesh) emits its runtime handle via the introspection sink's `field_asset`, and the **devapi JSON sink** reverse-maps it through `nt_resource_source_of` (an O(slots) dev-only scan of the canonical slot table — no separate index, no desync) to the source `resource` id (hex), then to a human `name` via the `nt_hash` label registry when built with `NT_HASH_LABELS`. The resource/hash deps live in the devapi sink, so components stay decoupled from the resource system. A hash a component owns directly (a `drawable.tag`, a `sprite.region_hash`) resolves to its string the same way via `nt_hash*_label`; a 64-bit such hash (`sprite.region_hash`) is itself emitted as a `0x`-hex string (like `resource_id`, via the sink's `field_u64_hex`) so its full width survives the JSON double. A **runtime-constructed** asset (`material`) has no `resource_id` to reverse-resolve, so its name is the create-time `label` it carries: the component emits an `NT_REF_MATERIAL` handle and the **devapi JSON sink** maps it to that `label` via `nt_material_get_info` (the component stays decoupled from the material runtime — the text sink shows the handle only, no resolve, mirroring `field_asset`).

**OFF semantics — dev-only, compiled out.** The whole obs group is gated by `NT_DEVAPI_GROUP_OBS` (default **OFF**, opt-in). When off, `nt_devapi_obs.c` is not compiled, the commands are **absent** from the registry (a `log.tail`/`perf.stats`/… request returns `unknown_method`), and the discovery surface does not list them. As with all of devapi it also vanishes entirely when `NT_DEVAPI_ENABLED` is OFF.

**Build deps are hard, not silent.** A CMake `FATAL_ERROR` guard (mirroring the `ui` group's DEBUG_TOOLS guard) requires `NT_DEVAPI_GROUP_OBS` to be built with `NT_LOG_RING_ENABLED`, `NT_METRICS_ENABLED`, **and** `NT_INTROSPECT_ENABLED` ON — those carry the real log-ring, metrics, and entity-introspection bodies the group reads (`perf.*` reads `nt_metrics` directly now; `entity.list` walks components through `nt_introspect`; the group does **not** link `nt_debug_overlay`, which is a sibling consumer, not a provider). With any dep OFF the group would link no-op stubs (an always-empty `log.tail`, a zero `perf.stats`, a core-fields-only `entity.list`) — a vacuously-passing false green — so configure fails fast instead. A second guard ties the debug overlay to metrics: `NT_UI_DEBUG_TOOLS` ⇒ `NT_METRICS_ENABLED` (the overlay HUD consumes `nt_metrics`). Each option defaults OFF and must be enabled explicitly; `NT_UI_DEBUG_TOOLS` does not enable its dependencies or other diagnostics.

### The `entity_write` group — a dev-only DEBUG write (`entity.set`)

Symmetric to `entity.list` reads, the **`entity_write`** group adds **`entity.set`**: a bot writes a writable field of one component on a live entity. It is the inverse of the read introspection — instead of a component's `describe()` pushing values out to a sink, the component's `apply()` hook receives an already-typed value and writes it **through the component's real setter** (which maintains the engine invariants: the transform dirty flag, quaternion normalization); the drawable hook rejects color components outside [0,1] and packs the VEC4 to RGBA8. cJSON is parsed into a neutral `nt_write_value` (number→F32, bool→BOOL, array[3]→VEC3, array[4]→VEC4) **inside devapi**; the component never sees cJSON, exactly as the read JSON sink keeps cJSON inside devapi.

| Command | Params | Result | Kind |
|---|---|---|---|
| `entity.set` | `{id, component, field, value}` or `{id, component, fields:{...}}` | `{component, fields:[string]}` | **WRITE** one writable field, or a whole-or-nothing batch of one component's fields, through `apply()`. Immediate (a field write is idempotent state, not an edge — `transform world_matrix` recomputes next update). Single field is atomic; a batch validates every field (parse + `dry_run` apply) before mutating any, so a bad field leaves nothing written |

**Not a control path.** `entity.set` maintains *engine* invariants but bypasses *game* logic (collision, rules) — it is for debugging, tests, and visual tuning, not for driving the game. Game control flows through the game's own semantic devapi commands.

**Writable ⊆ readable, enforced structurally.** The writable field set IS the `apply()` hook's accepted-key set: a field with no case (e.g. `world_matrix`, derived/read-only) falls through to `bad_params`; core `id`/`generation`/`index` have no component route at all. Untrusted input is fully validated before any mutation (kind, arity, finiteness, range, non-degenerate quaternion) → `bad_params`, never a trap; a stale/dead handle is `bad_params`, not an assert. The wire is intentionally stricter than the raw setter where needed (colour rejected outside `[0,1]` so the float and the packed byte cannot desync).

**Own deployment tier.** `entity.set` is its own group `NT_DEVAPI_GROUP_ENTITY_WRITE` (default **OFF**), independent of `obs` — three tiers: no devapi / read-only (obs) / read-write (obs + entity_write). A `FATAL_ERROR` guard requires `NT_INTROSPECT_WRITE_ENABLED` (the component `apply()` hooks); with it OFF every component is read-only, so `entity.set` could only ever return `bad_params` — a false surface.

## Frame capture (devapi `capture.*`)

A bot / AI / smoke-test grabs a **rendered frame** over devapi and verifies it — with **no engine file I/O and no PPM**. Three layers, mirroring the other capability groups:

- **L1 — engine capability (`nt_gfx_read_pixels`).** `nt_gfx_read_pixels(src, x, y, w, h, out, out_cap)` reads the window (`src.id == 0`) or a render target with an `RGBA8` color attachment into a caller buffer, only between `nt_gfx_end_frame` and the next `nt_gfx_begin_frame` (asserts otherwise): the frame has executed by then, and the window still holds it before the swap. A target rect must fit the target; after a pass that discarded the target color the content is undefined. The read uses explicit `GL_PACK_ALIGNMENT`, a single in-place Y-flip resolved once in the shared `nt_gfx` layer (the GL backend reads bottom-left; the contract is **top-left origin, straight alpha, `rgba8`**). An empty rect and `w*h*4 > out_cap` assert (the product is computed in `uint64` so it cannot overflow; L2 validates the external request first); it returns false only on a lost context. The test-only `nt_gfx_fake` backend supplies deterministic pixels so CTest can exercise the contract (and the flip) with no GL. The production gfx stub returns false and never fabricates pixels.
- **L2 — devapi veneer (`NT_DEVAPI_GROUP_CAPTURE`).** Two commands — `capture.frame` and `capture.region` — produce a uniform `{width, height, format:"png", data:<base64>}` payload, identical on native and web. The readback is RGBA8 but the wire is a **24-bit RGB PNG** (the constant alpha is stripped: smaller, faster, lossless). The PNG is encoded by the vendored `fpng` (real PNG, native SIMD + scalar fallback) behind a thin `extern "C"` wrapper, then base64-encoded into the JSON envelope. The group inits its own encoder (`nt_fpng_init`) when the host registers it (`nt_devapi_register_capture`, or `nt_devapi_register_default`) — a host needs no fpng knowledge.
- **Harness.** A Python pixel-health check decodes the payload (one code path) and asserts decode + dims + not-blank; Pillow/numpy are confined to that decode module, the harness core stays stdlib-only.

**The command group:**

| Command | Params | Result | Kind |
|---|---|---|---|
| `capture.frame` | `{scale?}` | `{width, height, format:"png", data:<base64>}` | **DEFERRED DATA** — capture the full framebuffer of the rendered frame, at its size, as a PNG; optional integer `scale ∈ {1,2,4}` box-average downscale |
| `capture.region` | `{x, y, w, h, scale?}` | `{width, height, format:"png", data:<base64>}` | **DEFERRED DATA** — capture an `(x,y,w,h)` sub-rect (top-left origin), then optional `scale` after the crop |

**Coordinates are top-left origin** on the wire, for both the full frame and the region rect — the L1 Y-flip and the region's `gl_y = fb_h - (y + h)` conversion hide GL's bottom-left from the bot. `scale` divides **after** the crop and must divide the (cropped) dimensions **evenly** — a `scale` that does not is rejected as `bad_params` rather than silently dropping the non-dividing edge pixels; the post-scale dimensions are what the payload reports.

**Timing — deferred to the rendered frame.** `capture.*` defers and returns data. The GL read is only valid after the frame is rendered and **before** the buffer swap (the back buffer is GL-undefined post-swap), which is a different point in the host loop than the transport pump (`nt_devapi_update`, which runs GL-free at frame start). So a capture **defers** on the rendered gate: the handler parses params and defers its reply; at the next `nt_devapi_frame_rendered()` the reply checks the rect against that frame's framebuffer and runs the readback → strip → encode → base64, and the next drain yields it. Capture waits for a rendered frame, not for a sim advance: it resolves in RUN, MANUAL idle and pause alike, and never advances time, so in MANUAL it shows the current state. A frame without the call fails a pending capture as `stalled` (see the deferred-reply rule above).

**The host calls `nt_devapi_frame_rendered()`.** Each rendered frame, after `nt_gfx_end_frame` and before `nt_window_swap_buffers`, as `nt_recorder_frame` does. With nothing pending it returns at once; calling it after `nt_devapi_shutdown` asserts. A host that registers capture inits `nt_gfx`; without it the readback asserts at the first capture. A host that skips the call (or renders nothing) answers captures with `stalled`, never a hang; send a new capture once rendering is back.

**Failure is a distinguishable envelope.** A capture the rendered frame cannot serve (a zero-size framebuffer) or a readback or encode that fails (lost context, a failed capture-buffer allocation, encode error) yields error code `capture_failed`, never an `ok:true` shape without `data`. Group code: `capture_failed`. Bot input never asserts. Malformed params (types, zero-size rect, `scale` outside {1,2,4}) fail synchronously as `bad_params`. The rect is checked against the rendered frame, whose framebuffer can differ from the one at the request: a rect that does not fit it, exceeds the pixel cap or is not divided by `scale` fails from the deferred reply as `bad_params`. A capture that never reaches a rendered frame fails as `stalled` before this check.

**Caps (DoS backstops, `-D` overridable).** `NT_DEVAPI_CAPTURE_MAX_PIXELS` (default `4096*4096`) bounds a single capture before any allocation; the producer's `uint32` size math is `_Static_assert`-proven wrap-free under that cap (×8 covers the base64 expansion). One deferred reply is in flight at a time, so at most one capture is pending. The Python transport's recv-line cap is sized to cover the engine's worst-case capture line.

**OFF semantics — dev-only, compiled out.** The whole group is gated by `NT_DEVAPI_GROUP_CAPTURE` (default **OFF**, opt-in). When off, `nt_devapi_capture.c` is not compiled, the commands are absent from the registry (`unknown_method`) and discovery, and the vendored fpng encoder is not linked into the binary (zero release delta); as with all of devapi it also vanishes when `NT_DEVAPI_ENABLED` is OFF. `nt_fpng` is built `EXCLUDE_FROM_ALL`, so it compiles only when a capture-enabled target links it.

## Gfx frame recording (devapi `gfx.*`)

A bot, agent or developer turns recording on and reads any recent frame as [capture text](../render/architecture.md#capture-text) at once. The group keeps the history; gfx only records (its two capture arrays let every frame record).

**Recording is a mode, and game state.** `gfx.record {enabled:true}` turns it on and records every frame from the next one that records until it turns off; `{enabled:false}` stops recording, keeps the ring and still stores the frame in flight (the frame gfx records at the call, also before that frame's `nt_devapi_update`), so the frame just rendered when a bug was seen is kept. Turning on while on changes nothing; off then on clears the ring. Game code switches the same mode with `nt_devapi_gfx_record(bool)` (declared in `nt_devapi_groups.h` under `NT_DEVAPI_GROUP_GFX`), valid anywhere in a frame; a client leaving does not change it. Recording works with rendering disabled, because frames still begin and end; it never changes simulation time, pass order, draw scheduling or renderer state.

**Ring.** Static storage of `NT_DEVAPI_GFX_RING_RECORDS` records and `NT_DEVAPI_GFX_RING_FRAMES` frames (compile definitions, see [build options](../../build.md#build-options)). A frame is stored contiguously, only its `count` records; the oldest frames give way, so the stored frames are always a gapless run of gfx sequences. A stored frame with more records than the ring asserts. A frame whose capture overflowed is stored with `overflow` set and logs one WARN per process.

**Timing.** The group's frame hook stores the frame `nt_gfx_begin_frame` just finalized, with `sim_frame` (`g_nt_app.frame` while it recorded) and whether the simulation advanced in it, and requests the next frame. The host calls `nt_devapi_update` once per frame after `nt_gfx_begin_frame`, so a native command already sees that frame; a skipped update while a frame is in flight asserts, and the order itself is the host's contract. A `time.step` reply leaves before its frame renders; a web submit between frames can arrive before that frame is stored and gets `frame_not_ready`, so a client retries. The group needs the real gfx with a nonzero `capture_capacity`; a host that builds the group sets one (`devapi_host` and `skeletal_showcase` do).

**The command group** (all reply immediately):

| Command | Params | Result | Kind |
|---|---|---|---|
| `gfx.record` | `{enabled}` | `{}` | turn recording on or off |
| `gfx.frames` | `{}` | `{recording, frames:[{sequence,sim_frame,advanced,records,overflow}]}` | **READ** the stored frames, oldest first |
| `gfx.read` | `{sequence?, sim_frame?}` | `{sequence, sim_frame, text}` | **READ** one stored frame's capture text: the newest with no param; by gfx `sequence`; or by `sim_frame`, the frame in which the simulation advanced to it (idle frames of the same sim frame never match) |

`gfx.read` takes at most one param, an exact integer in uint32 range (else `bad_params`). While recording, or while the frame in flight after off is not stored yet, a frame that can still be stored answers `frame_not_ready`: the newest of an empty ring, the frame recording now or a later sequence, a sim frame not reached yet or still recording. Anything else not stored (evicted, before recording, never recorded) is `unknown_frame`. Sim frames compare modularly, like the frame gates. Until the frame in flight after off is stored, `gfx.frames` answers `frame_not_ready` too, because storing it can still evict the oldest frame. To keep the frames before a bug, turn recording off, list them with `gfx.frames` (retrying `frame_not_ready`) and read each by sequence; the client saves them, the engine writes no files. Group codes: `frame_not_ready`, `unknown_frame`.

**OFF semantics.** The group is gated by `NT_DEVAPI_GROUP_GFX` (default OFF) and requires `NT_GFX_CAPTURE_ENABLED` (a configure-time `FATAL_ERROR`). When off, `nt_devapi_gfx.c` is not compiled, the commands are absent from the registry and discovery, and `nt_gfx_capture_text` is not linked; it vanishes with the rest of devapi when `NT_DEVAPI_ENABLED` is OFF.

## Override-able compile-time options

The engine follows "use only what you need" — subsystems use CMake options or
`-D` overrides so builds pull in only requested code. This section owns DevAPI
group gates and tunables. Common engine flags, preset overrides and validation
commands live in [Building and checks](../../build.md#build-options).

**devapi build gates** (CMake `option(...)`, set at configure time):

| Flag | Default | Effect |
|---|---|---|
| `NT_DEVAPI_ENABLED` | OFF | Master gate. ON compiles the `engine/devapi` subdir and defines `NT_DEVAPI_ENABLED=1`; OFF compile-excludes the whole layer (zero devapi code/symbols in release). Every group flag below is a no-op while this is OFF. |
| `NT_DEVAPI_GROUP_CORE` | ON | Build the core group: `ping` / `engine.info` / `view`. |
| `NT_DEVAPI_GROUP_TIME` | ON | Build the `time.*` / `render.*` / `frame.*` group. |
| `NT_DEVAPI_GROUP_INPUT` | ON | Build the `input.*` synthetic-input group (player gate + inject scheduler). |
| `NT_DEVAPI_GROUP_UI` | OFF | Build the `ui.*` group (UI tree extraction + widget input). Requires `NT_UI_DEBUG_TOOLS` + `NT_DEVAPI_GROUP_INPUT` (hard CMake guards). |
| `NT_DEVAPI_GROUP_OBS` | OFF | Build the `log.*` / `perf.*` / `entity.*` / `resource.*` reads. Requires `NT_LOG_RING_ENABLED` + `NT_METRICS_ENABLED` + `NT_INTROSPECT_ENABLED`. |
| `NT_DEVAPI_GROUP_ENTITY_WRITE` | OFF | Build the `entity.set` component-write group. Requires `NT_INTROSPECT_WRITE_ENABLED`. |
| `NT_DEVAPI_GROUP_CAPTURE` | OFF | Build the `capture.frame` / `capture.region` PNG framebuffer-capture group (links `nt_gfx` + `nt_fpng`). |
| `NT_DEVAPI_GROUP_RECORDER` | OFF | Build the `record.start` / `record.stop` group over [`nt_recorder`](recorder.md); configure error where `nt_recorder` has no backend. |

**devapi caps & tunables** (`-D` overridable preprocessor defines, with their defaults):

| Define | Default | Effect |
|---|---|---|
| `NT_DEVAPI_DEFAULT_PORT` | `17890` | The conventional native loopback-TCP listen port; the examples pass it as the `nt_devapi_net_port_from_env` fallback. |
| `NT_DEVAPI_CAPTURE_MAX_PIXELS` | `4096*4096` | DoS backstop on a single `capture.*` before any allocation; the producer's `uint32` size math is `_Static_assert`-proven wrap-free under this cap. |
| `NT_INPUT_INJECT_QUEUE_MAX` | `256` | The bounded static-BSS immediate inject buffer `nt_input_poll` drains whole each poll. |
| `NT_DEVAPI_MAX_RESET_HOOKS` | `8` | Cap of the client-reset hook table (`nt_devapi_register_reset`); overflow asserts. |

Each devapi group is its **own** static library (`nt_devapi_<group>`) that links only its deps, so a host links `nt_devapi` plus only the groups it wants (a capture-only host pulls `nt_gfx` + `nt_fpng`, never `nt_ui`); `nt_devapi_default` is the "every compiled-in group" bundle.
