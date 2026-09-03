# Logging, Errors, Debugging

Log levels, the NT_ASSERT contract policy (OFF/TRAP/FULL), fatal-vs-recoverable
error policy, and the metrics-fed debug overlay. The bulk documents the dev-only
devapi: transport model, time/render control (`nt_app`), input and UI
automation, observability commands (log/perf/entity/resource), the entity-write
group, frame capture, and the compile-time option catalogue.

Related: [Module Layout](../core/module-layout.md), [Input](../io/input.md), [Resource System](../assets/resource.md)

## Logging levels

- INFO
- WARN
- ERROR
- ASSERT
- PANIC

## Assert policy

Asserts are contracts, not error handling. A failed assert means the program is broken beyond recovery — continuing would mask bugs.

- **NT_ASSERT** — single macro, three compile-time modes via `NT_ASSERT_MODE`:
  - `0 (OFF)` — `((void)0)`, zero overhead. Available via CMake override (`-DNT_ASSERT_MODE=0`) as an **unsupported**, size-oriented escape hatch. Once an asserted precondition is violated, runtime behavior is undefined.
  - `1 (TRAP)` — `__builtin_trap()`, no strings, minimal binary impact. **Release default.**
  - `2 (FULL)` — hookable handler with `expr/file/line` strings. **Debug default.** Tests use the handler to catch and verify assert failures via `setjmp`/`longjmp`.
- Release ships with TRAP (1): contract violations crash immediately instead of continuing with corrupted state. No string bloat, no handler overhead — just a single branch + trap instruction per assert.
- Assert expressions are side-effect-free because OFF does not evaluate them. No fallback path is required solely to keep an OFF build running after an invariant breach.
- Hard guards are required at untrusted/runtime-input boundaries and wherever a public API promises recoverable rejection; those guards implement the API contract, not support for OFF.
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

## Debug overlay

`nt_debug_overlay` is a **pure consumer** of `nt_metrics` ([Observability](#observability-devapi-log--perf--entity--resource)): it formats an on-screen HUD (`nt_debug_overlay_format_lines` / `nt_debug_overlay_draw`) from `nt_metrics_fps()`, the last frame's cpu/gpu/draw_calls via `nt_metrics_last()`, and the user counters via `nt_metrics_user_count()` / `nt_metrics_user_get()`. It owns **no** measurement and **no** counter storage — the host pushes per-frame data into `nt_metrics` and the overlay reads it back. Because of that dependency, a CMake `FATAL_ERROR` guard requires `NT_UI_DEBUG_TOOLS` to be built with `NT_METRICS_ENABLED` (with metrics OFF the overlay would format from no-op stubs — an empty HUD); the production default (`NT_UI_DEBUG_TOOLS=OFF`) defaults metrics OFF too, so it never trips.

Recommended stats: frame time, fps, cpu/gpu time, draw call count, plus any game-supplied user counters.

## Developer API (devapi)

`nt_devapi` is an **optional, dev-only** self-describing command surface for engine introspection and automation (probing, testing, external tooling). It is gated by `NT_DEVAPI_ENABLED` (OFF by default); the `engine/devapi` subdirectory is excluded at the CMake level when off, so a release binary contains zero devapi code or symbols. A CI "zero-delta" check asserts a devapi-OFF WASM has no `nt_devapi_*` symbols. devapi is the one sanctioned exception to the runtime's no-parser rule — it is dev-only and compiled out of release.

**Transport-agnostic core.** The dispatch core is `submit(line) -> response line`: one JSON request line in, one JSON response line out, with no platform/socket/transport code. A command may also **defer** its response: `submit` returns `NULL`, the command records a deadline of `g_nt_app.frame + N` game frames at submit time, and the result is delivered later by `nt_devapi_poll_response()` once `g_nt_app.frame` has reached that deadline. The host drains ready responses each tick (via `nt_devapi_update()`); because readiness is a comparison against the game frame counter, it counts real simulation advances, not poll calls — a paused game never advances its frame, so a wait never resolves. The two real transports (loopback TCP on native, a `ccall` bridge on web) are separate, opt-in libraries that share this one core — both are described below.

**Unified transport model.** Two parallel transports wrap the **one** core dispatch — not a single swappable interface, but two thin libraries that both feed `submit` and drain `poll_response`. The core (`nt_devapi`) owns `nt_devapi_update()` and the frame-keyed deferred drain; each transport registers its own per-tick poll (or nothing) into the core's fixed-array **lifecycle-hook registry**, so both transports run the **same** shared drain and neither is hard-referenced by the core.

- **Native — loopback TCP (`nt_devapi_net`).** A single-client, JSON-lines, non-blocking TCP listener. It binds **`127.0.0.1` only** (`INADDR_LOOPBACK`) on `NT_DEVAPI_DEFAULT_PORT` (`17890`, env-overridable via `NT_DEVAPI_PORT`), and registers its `nt_devapi_net_poll` (non-blocking accept + recv + framed dispatch + send + deferred drain) as the core's transport-poll hook (the phase that runs before the per-tick hooks). The sole socket boundary in the layer — all `_WIN32` (Winsock) vs POSIX `#ifdef` code lives in this one module.
- **Web — `ccall` bridge (`nt_devapi_web`).** Push/pull instead of a socket: JS on the **same page** calls the `EMSCRIPTEN_KEEPALIVE`-exported `nt_devapi_web_submit(line)` to push a request and `nt_devapi_web_poll()` to pull queued responses, reached through `window.__devapi`. `window.__devapi.reset()` drops that client's deferred replies and runs the same reset hooks as a native disconnect. There is **no socket and no listener** — the bridge is driven by the host page's JS, so the web transport registers **no** tick poll (it is push-driven); the core's frame-keyed drain still delivers deferred replies for the JS side to pull.

**Security model — native vs web.** This is the headline: **there is no open devapi port in the browser.** On native, the listener is a **loopback `127.0.0.1`-only** socket — never reachable off the machine. On web, there is **no listener at all**: `window.__devapi` is callable only by **same-page JS** in the same origin, and only when the build defines `NT_DEVAPI_ENABLED` (OFF by default — the whole `engine/devapi` subdir is compile-excluded from release, so a shipped web build carries zero devapi code or symbols). A devapi-enabled web build opens no network surface; an attacker would already need to be running same-origin script in the page.

**Self-describing registry.** Commands are registered once into a fixed-size table, each with a 7-field descriptor (`method`, `group`, `summary`, `params_shape`, `result_shape`, `frame_behavior`, `side_effects`). The discovery commands (`endpoints`, `command.describe`, `features`) expose the whole surface so a client reads it without source. A game registers `group="game"` commands through the public API with zero engine edits. `features` lists the distinct `group` values across all registered commands — which groups exist depends on which commands are compiled in (engine) or registered at runtime (game).

**Envelope.** Each request returns `{ok:true,result}` or `{ok:false,error:{code,message}}`, echoes `request_id` unchanged, and a JSON-array line runs as an ordered batch with continue-on-error. A **deferred** command emits no synchronous envelope — its `{ok,result,request_id}` envelope arrives later via `nt_devapi_poll_response()`; a deferred command is **not** allowed inside a batch (it is rejected with an error entry, since a batch is one ordered response array). A future phase may define a batch-deferred protocol.

**cJSON dependency.** devapi parses/serializes with vendored cJSON, exposed as a standalone reusable `cjson` static-lib target (not an engine module, `EXCLUDE_FROM_ALL`). cJSON is usable by games independently of devapi; only targets that link it pull it in.

## Time & render control (`nt_app`)

`nt_app_run(fn)` is a **single, flag-aware frame loop** that owns the one `g_nt_app.dt` scalar. Its default state — mode `RUN`, `scale` 1, unpaused — is a plain wall-clock advance (`dt` = clamped wall delta, `frame++` every iteration), so a game that never touches the controls runs exactly as a minimal loop would. The game still wires poll/input/update/render inside `fn`; the engine gives building blocks, not a pipeline.

Time control is a set of flags in `g_nt_app`, set via the `nt_app_*` mutators (and exposed to a bot through the devapi `time.*`/`render.*` commands):

- **mode** `RUN` / `MANUAL`. RUN advances on wall time; MANUAL advances one fixed-`step_dt` step per queued `nt_app_step`, with no wall clock and no `max_dt` clamp — the byte-reproducible lockstep contract.
- **paused** (RUN only): `dt` = 0 and `frame` frozen while `fn` keeps running.
- **scale** (RUN only): multiplies `dt` for slow-mo / fast-forward **observation** — explicitly not a determinism primitive (reproducible fast runs use MANUAL lockstep).
- **render_enabled**: a loop-agnostic gate the game reads to skip its draw + present together.

`g_nt_app.frame` is a **simulation-advance counter**: it increments only on a real advance, so pause and manual-idle freeze it. Deferred devapi responses (`frame.wait`, `time.step`) are keyed to it — a wait resolves once `frame` reaches its submit-time deadline (`frame + N`), so it counts game frames, not transport polls, and never resolves while the sim is frozen. `frame.wait` is therefore RUN-only (rejected in manual/paused); in MANUAL a bot uses `time.step{count}`, which advances and blocks until done.

Game-elapsed time `g_nt_app.time` (a `double` — the sum of applied `dt`s, kept double so long deterministic runs don't lose seconds-resolution to float accumulation) is the **second clock**. `time.wait{seconds?}` defers on a game-time deadline (`time + seconds`, default 0 = resolve on the next drain) rather than a frame count — the right tool in RUN, where the variable `dt` (and `scale`) make a frame count meaningless, and where time-authored mechanics (cooldowns, durations) live. Because game time only flows under RUN with `scale > 0`, `time.wait` is rejected in manual/paused/scale-0 (where it could never resolve). In MANUAL the two clocks are **linear** (`time = frames × step_dt`), so `time.step` accepts either `{count}` frames or `{seconds}` (= `ceil(seconds / step_dt)` frames) — convenience for the same time-authored logic in lockstep.

The L1 mutators **assert** their invariants (finite/non-negative scale, positive `step_dt`, valid mode) — callers are trusted game code; untrusted bot input is range-checked at the devapi L2 layer and returns `bad_params`, never an assert.

## Input automation (devapi `input.*` + player gate)

A bot drives input through the devapi `input.*` command group, which is a thin L2 veneer over an L1 engine capability (`nt_input_inject_*` + the player gate). The design rule is **bot-indistinguishable-from-human**: injected events flow through the same input-apply cores a real device hits, so the query API (`nt_input_key_is_*`, `nt_input_mouse_is_*`, `nt_input_pop_char`) cannot tell synthetic input from physical. Compiled out with the rest of devapi when `NT_DEVAPI_ENABLED` is OFF.

**Range-checked, never asserts.** Every `input.*` command validates its params and returns `bad_params` on any out-of-domain value (unknown key name, out-of-range pointer id, a button mask outside `[0,7]` or non-integral, malformed/invalid UTF-8, a frame count outside `[0,65535]`). The L1 inject API itself never asserts — it is driven by untrusted L2 — so bad bot input is always a structured error, never a crash.

**The command group** (all `frame_behavior: any`, all fire-and-forget unless noted):

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
| `input.state` | `{key?, pop_text?}` | `{down?, pressed?, released?, codepoints?}` | **READ** (not an enqueue) of the polled input state — see below |

**The L1 player gate.** `nt_input_set_player_enabled(bool)` gates **real device** events at the apply seam: while disabled, the public real-input wrappers (`nt_input_set_key`, `nt_input_pointer_*`, `nt_input_wheel`, `nt_input_buffer_char`) early-return, so a physical device is suppressed — but injected events still flow, because inject calls the `*_apply` cores directly past the gate. The bot is therefore indistinguishable from a human to the query API while the real player is locked out. The **ON→OFF edge releases held current input state (synthetic and real are indistinguishable by design — bot == human)** so nothing sticks down across a focus-lost-style cutover: held keys raise their release edge, and held pointer buttons raise their release edge with **deferred deactivation** — the slot stays active one more poll so the release edge is readable via the public mouse query, then deactivates on the next poll (the same lifecycle as a normal pointer-up). These release primitives are the **changer's** tool (a graceful bot calls them, or the game does); the engine does **not** invoke them on a devapi client disconnect — applied input is game-owned (see the B-strict disconnect rule below).

**Scheduling lives in the devapi layer; `nt_input` is a pure apply layer.** `nt_input` knows nothing about frames or scheduling: its inject API is **immediate** (`nt_input_inject_key/pointer/wheel/text`), each call staging into a bounded static-BSS **immediate inject buffer** (`NT_INPUT_INJECT_QUEUE_MAX`, `-D` overridable) that `nt_input_poll` drains **whole** every poll, after the platform poll, through the same `*_apply` cores (gate-bypassing). The **frame schedule** is owned by the devapi input group (`NT_DEVAPI_INPUT_SCHED_MAX`, `-D` overridable) — exactly because the devtool is the only side that legitimately knows `g_nt_app.frame`. The `input.*` handlers enqueue into that schedule with their offsets (immediate commands at offset 0; `input.click` = down@0 + up@hold; `input.gesture` = down@0 + moves@(idx·stride) + up@last; a `hold` key = down@0 + up@hold; a UTF-8 string = one entry per codepoint at offset 0). A command reserves its entries **whole-or-nothing** against the schedule, so a near-full schedule can never leave a stuck pointer-down or a key with no release; overflow → `bad_params`.

**The input group is an OPTIONAL devapi module.** A single CMake switch, `NT_DEVAPI_GROUP_INPUT` (default ON), gates the WHOLE group consistently: it compiles `nt_devapi_input.c`, registers the `input.*` commands, links `nt_input` + enables the `nt_input` automation surface (player gate + inject pipeline, `NT_INPUT_AUTOMATION_ENABLED`), and wires the per-tick + client-reset lifecycle hooks. With the group OFF, the devapi core and transport carry **zero** input symbols, and `nt_input` itself is the lean apply layer (no gate, no inject buffer) — "use only what you need". The core never hard-references the group: it exposes a tiny fixed-array **lifecycle-hook registry** with two ordered run phases — transport-poll hooks then per-tick hooks — plus a client-reset hook, each registered by a group's registrar exactly like its commands. The input group registers its schedule tick as a per-tick hook and its release-on-disconnect as the reset hook; `nt_devapi_update` and the transport's `close_client` call the generic hook runners, naming no group. Fail-fast is accepted: the group ON without an `nt_input` impl in the executable is an unresolved-symbol **link** error (the developer's responsibility), not a silent no-op.

**Advance-gated release.** `nt_devapi_update` runs every tick in two ordered phases — the **transport-poll hooks** (recv + enqueue) then the **tick hooks** (the input schedule release) — naming no group. The native transport registers `net_poll` in the transport-poll phase, so a line received this tick is enqueued into the schedule before the input group's tick reads it, regardless of registration order. The tick detects a real sim-advance by comparing `g_nt_app.frame` to its own last-seen value and, **only on an advance**, releases every entry whose countdown reached 0 into `nt_input`'s immediate buffer (decrementing survivors); the next `nt_input_poll` (same tick) applies that buffer post-edge-clear, so an injected rising edge survives to that frame's update. On a frozen tick (pause / manual-idle) the schedule releases **nothing** — synthetic input holds while the game is paused (real device input still flows through `nt_input_poll`). This replaces the old per-`nt_input` relative-countdown freeze.

**B-strict disconnect ownership.** On a devapi client disconnect the engine resets **only devapi-owned transient state**: the inject schedule, its advance-clock re-seed, and the in-flight deferred-reply queue — the gone bot's own pending bookkeeping, which must not bleed to the next client and is not game state. **Game-owned state is the changer's responsibility, never the engine's**: time mode/pause/scale, the render flag, the player gate, and already-**applied** input (a landed key/pointer DOWN) all stay as they were. The engine deliberately does not clobber them because L1 cannot tell whether the game or the bot set a given state — resetting it on a dev-client drop would violate code-first. A bot restores what it changed before disconnecting (its `finally`), or the host recovers explicitly: the bare `examples/devapi_host` watches `nt_devapi_net_has_client()` for the connected→disconnected edge and applies its OWN policy (return to plain RUN) so an ungraceful drop mid-MANUAL can't leave it frozen — host application code, not the engine library.

**`input.state` is a DEV-ONLY observation read.** It returns the input state `nt_input_poll` last produced — so before a sim-advance an enqueued inject is **not yet visible** (the drain-race, now machine-observable over the socket). With `key`, it returns `{down,pressed,released}` for that key (unknown name → `bad_params`). With `pop_text:true` it is a **CONSUMING read** — it drains the char ring into a `codepoints` raw-codepoint array as a side effect.

**`nt_input_poll(void)` contract.** No frame argument: `nt_input` is a pure apply layer that never includes `app/nt_app.h`. The host order is `nt_window_poll → nt_devapi_update → nt_input_poll → (game update)`. `nt_devapi_update` releases the due scheduled events into the immediate buffer (advance-gated), then `nt_input_poll` drains that whole buffer after the platform poll, in the same post-edge-clear window as the native char drain.

## UI automation (devapi `ui.*`)

A bot reads and drives UI through the devapi `ui.*` command group — a thin L2 veneer with no UI logic of its own. The **read** surface (`ui.tree`, `ui.element`, `ui.contexts`) is a projection of the L1 `nt_ui` probe; the **write** surface (`ui.click`, `ui.drag`, `ui.scroll`) resolves a target to a point and hands it to the **same** synthetic inject path `input.*` uses — there is no second injector. The whole group is gated by `NT_DEVAPI_GROUP_UI` (default OFF — opt-in) and compiled out with the rest of devapi when `NT_DEVAPI_ENABLED` is OFF.

**Build deps are hard, not silent.** Two CMake `FATAL_ERROR` guards keep the group from compiling into a vacuous or unbuildable state: `NT_DEVAPI_GROUP_UI` requires `NT_UI_DEBUG_TOOLS` (the probe has a real impl only under DEBUG_TOOLS — else a 0-node stub, a false green), and requires `NT_DEVAPI_GROUP_INPUT` (the writes reuse the input group's single inject scheduler; ui without input would link-fail on the reuse wrappers). The ui group is default **OFF** (opt-in, "use only what you need") so a plain `-DNT_DEVAPI_ENABLED` build stays lean and does not pull in UI debug tools; enable it explicitly alongside `NT_UI_DEBUG_TOOLS` + `NT_DEVAPI_GROUP_INPUT` (which stays default ON), and the two guards enforce both are present. The group registers **no** tick/reset lifecycle hook: scheduling is delegated to the input group's drain, and the only ui-owned state is the host context table, which is not transient (see below).

**Context registration & lifetime.** The host registers each UI context by name (`nt_devapi_ui_register_context(name, ctx)`); the engine keeps **no** global ctx registry. Names and ctx pointers are stored **by reference** in a small fixed table (`NT_DEVAPI_UI_CONTEXT_MAX`), so both must be **pointer-stable** for the devapi lifetime. Registration is a trusted in-process host call (it **asserts** on NULL / duplicate / table-full, like `game.*` command registration) and happens once at startup — the table is cleared only at init, never on a client disconnect, so it **survives client reconnects**. Bot input that misses the table is always `bad_params`, never an assert. A command with no `ctx` resolves to the sole/first context. A ctx that has **not completed a frame** (`nt_ui_context_has_frame` false — its layout dims are 0 and the coord converters would trap) is rejected up front with `bad_params`, so no wire input ever reaches a degenerate converter.

**One coordinate space, read==write.** The group declares a **single** space: **Y-up, origin bottom-left, in ctx LAYOUT pixels** (not raw `g_nt_window.fb_*` — those differ under `nt_ui_scale`). `ui.tree`/`ui.element` bounds and `ui.click({x,y})` speak that **same** space, so a bot reads a widget's bounds and clicks its center with **no transform**. The ctx owns a device↔layout viewport (`nt_ui_set_viewport`, default identity = full screen); `nt_ui_begin` takes the **raw device** pointer and converts device→layout internally, while the writes map layout→device through that viewport — so a click lands correctly in scaled / letterboxed / DPR UIs. The sole Y-up→Y-down flip lives in the write path's target resolve. A **string id** resolves to the **same projected bounds center** the probe reports for that id (read==id-write), including 2D-affine and 3D-raycast contexts; a behind-camera or collapsed target (zero bounds) → `bad_params` rather than a click at a screen corner. Each read result carries a metadata block — `space`, `origin`, `y_axis`, `width`, `height`, `dpr`, `projection` (`2d`/`3d`), and a `viewport` rect (omitted for `3d`, where no device↔layout affine exists).

**Probe snapshot + truncation.** `ui.tree`/`ui.element` are an **immediate** read of the **last completed frame's** tree as flat, Clay-free POD nodes (`id`/`parent`/`role`/`text`/`label`/`bounds`/`visible`/`enabled`/`child_count`); strings are **copied** into node-owned fixed-cap buffers (the borrowed Clay pointer dies at the next `nt_ui_begin`). Nodes are emitted incl. invisible / offscreen / disabled — the bot filters, not the engine. The devapi read path (`nt_ui_probe_collect_owned`) walks into a **per-ctx arena scratch sized to the runtime `max_elements`** — Clay caps live elements at that same knob and each maps to ≤ 1 node, so a node-**count** overflow can never fire; `truncated` reflects **only** the internal DFS-stack depth limit (a tree nested past the walk's `STACK_CAP`). `NT_UI_PROBE_MAX_NODES` (tied to `NT_UI_DEFAULT_MAX_ELEMENT_COUNT`) bounds only the **legacy caller-buffer** `nt_ui_probe_collect` form, where the caller picks the cap and a count overrun is possible. Under truncation a not-found id returns an **honest "truncated"** error rather than "unknown id" — a dropped widget is not called stale.

**Range-checked, never asserts.** Like `input.*`, every `ui.*` command validates its params and returns `bad_params` on any out-of-domain value (non-finite or float-overflowing coords, a fractional/negative/over-`UINT16_MAX` frame count, unknown ctx, unknown / stale id, a never-begun ctx). `ui.drag` additionally floors `frames` at **1**: the inject up releases at the pointer's current position, so a 0-frame drag (no move) would land the up back at `from` and silently drop `to`; `frames` ≥ 1 always emits at least one move that ends at `to`. The writes reuse the input scheduler's **whole-or-nothing** reservation — a multi-event command (`ui.click` = down@0 + up@hold; `ui.drag` = down + `frames` interpolated moves + up; `ui.scroll` = move + wheel) reserves **all** its slots or rejects, so a near-full schedule can never leave a stuck pointer-down or an unmatched edge. The bot supplies the drag path implicitly via `frames` (the handler expands the linear interpolation; `frames` is DoS-capped to fit the input scheduler). All writes are fire-and-forget — advance a frame to apply.

**The command group** (all `frame_behavior: any`; reads have no side effect, writes are fire-and-forget):

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

A bot inspects engine state through the devapi **obs** command group — a thin L2 veneer that serializes the dev-only log ring, the `nt_metrics` perf collector, and the entity/resource enumeration accessors. Every obs command is a **pure immediate read**: it serializes the live L1 state on the same `submit` call and returns synchronously — **nothing in this group ever defers** (no `frame.wait`-style continuation), so all are `frame_behavior: any` with no side effect except `perf.reset`. Untrusted bot input is range/type-checked and returns `bad_params`; only host-call invariants assert.

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

**Build deps are hard, not silent.** A CMake `FATAL_ERROR` guard (mirroring the `ui` group's DEBUG_TOOLS guard) requires `NT_DEVAPI_GROUP_OBS` to be built with `NT_LOG_RING_ENABLED`, `NT_METRICS_ENABLED`, **and** `NT_INTROSPECT_ENABLED` ON — those carry the real log-ring, metrics, and entity-introspection bodies the group reads (`perf.*` reads `nt_metrics` directly now; `entity.list` walks components through `nt_introspect`; the group does **not** link `nt_debug_overlay`, which is a sibling consumer, not a provider). With any dep OFF the group would link no-op stubs (an always-empty `log.tail`, a zero `perf.stats`, a core-fields-only `entity.list`) — a vacuously-passing false green — so configure fails fast instead. A second guard ties the debug overlay to metrics: `NT_UI_DEBUG_TOOLS` ⇒ `NT_METRICS_ENABLED` (the overlay HUD consumes `nt_metrics`). `NT_UI_DEBUG_TOOLS=ON` defaults all of `NT_LOG_RING_ENABLED` / `NT_METRICS_ENABLED` / `NT_INTROSPECT_ENABLED` on.

### The `entity_write` group — a dev-only DEBUG write (`entity.set`)

Symmetric to `entity.list` reads, the **`entity_write`** group adds **`entity.set`**: a bot writes a writable field of one component on a live entity. It is the inverse of the read introspection — instead of a component's `describe()` pushing values out to a sink, the component's `apply()` hook receives an already-typed value and writes it **through the component's real setter** (which maintains the engine invariants: the transform dirty flag, the drawable packed-RGBA8 mirror, quaternion normalization). cJSON is parsed into a neutral `nt_write_value` (number→F32, bool→BOOL, array[3]→VEC3, array[4]→VEC4) **inside devapi**; the component never sees cJSON, exactly as the read JSON sink keeps cJSON inside devapi.

| Command | Params | Result | Kind |
|---|---|---|---|
| `entity.set` | `{id, component, field, value}` or `{id, component, fields:{...}}` | `{component, fields:[string]}` | **WRITE** one writable field, or a whole-or-nothing batch of one component's fields, through `apply()`. Immediate (a field write is idempotent state, not an edge — `transform world_matrix` recomputes next update). Single field is atomic; a batch validates every field (parse + `dry_run` apply) before mutating any, so a bad field leaves nothing written |

**Not a control path.** `entity.set` maintains *engine* invariants but bypasses *game* logic (collision, rules) — it is for debugging, tests, and visual tuning, not for driving the game. Game control flows through the game's own semantic devapi commands.

**Writable ⊆ readable, enforced structurally.** The writable field set IS the `apply()` hook's accepted-key set: a field with no case (e.g. `world_matrix`, derived/read-only) falls through to `bad_params`; core `id`/`generation`/`index` have no component route at all. Untrusted input is fully validated before any mutation (kind, arity, finiteness, range, non-degenerate quaternion) → `bad_params`, never a trap; a stale/dead handle is `bad_params`, not an assert. The wire is intentionally stricter than the raw setter where needed (colour rejected outside `[0,1]` so the float and the packed byte cannot desync).

**Own deployment tier.** `entity.set` is its own group `NT_DEVAPI_GROUP_ENTITY_WRITE` (default **OFF**), independent of `obs` — three tiers: no devapi / read-only (obs) / read-write (obs + entity_write). A `FATAL_ERROR` guard requires `NT_INTROSPECT_WRITE_ENABLED` (the component `apply()` hooks); with it OFF every component is read-only, so `entity.set` could only ever return `bad_params` — a false surface.

## Frame capture (devapi `capture.*`)

A bot / AI / smoke-test grabs a **rendered frame** over devapi and verifies it — with **no engine file I/O and no PPM**. Three layers, mirroring the other capability groups:

- **L1 — engine capability (`nt_gfx_read_pixels`).** `nt_gfx_read_pixels(x, y, w, h, out, out_cap)` reads the default framebuffer into a caller buffer: explicit `GL_PACK_ALIGNMENT`, a single in-place Y-flip resolved once in the shared `nt_gfx` layer (the GL backend reads bottom-left; the contract is **top-left origin, straight alpha, `rgba8`**). It is cap-checked (`w*h*4 > out_cap` → false; the product is computed in `uint64` so it cannot overflow) and early-returns false on a lost context. The test-only `nt_gfx_fake` backend supplies deterministic pixels so CTest can exercise the contract (and the flip) with no GL. The production gfx stub returns false and never fabricates pixels.
- **L2 — devapi veneer (`NT_DEVAPI_GROUP_CAPTURE`).** Two commands — `capture.frame` and `capture.region` — produce a uniform `{width, height, format:"png", data:<base64>}` payload, identical on native and (later) web. The readback is RGBA8 but the wire is a **24-bit RGB PNG** (the constant alpha is stripped: smaller, faster, lossless). The PNG is encoded by the vendored `fpng` (real PNG, native SIMD + scalar fallback) behind a thin `extern "C"` wrapper, then base64-encoded into the JSON envelope. The group inits its own encoder (`nt_fpng_init`) when the host registers it (`nt_devapi_register_capture`, or `nt_devapi_register_default`) — a host needs no fpng knowledge.
- **Harness.** A Python pixel-health check decodes the payload (one code path) and asserts decode + dims + not-blank; Pillow/numpy are confined to that decode module, the harness core stays stdlib-only.

**The command group:**

| Command | Params | Result | Kind |
|---|---|---|---|
| `capture.frame` | `{scale?}` | `{width, height, format:"png", data:<base64>}` | **DEFERRED DATA** — capture the full framebuffer as a PNG; optional integer `scale ∈ {1,2,4}` box-average downscale |
| `capture.region` | `{x, y, w, h, scale?}` | `{width, height, format:"png", data:<base64>}` | **DEFERRED DATA** — capture an `(x,y,w,h)` sub-rect (top-left origin), then optional `scale` after the crop |

**Coordinates are top-left origin** on the wire, for both the full frame and the region rect — the L1 Y-flip and the region's `gl_y = fb_h - (y + h)` conversion hide GL's bottom-left from the bot. `scale` divides **after** the crop and must divide the (cropped) dimensions **evenly** — a `scale` that does not is rejected as `bad_params` rather than silently dropping the non-dividing edge pixels; the post-scale dimensions are what the payload reports.

**Timing — deferred at the pre-swap seam.** `capture.*` is the first devapi command that **defers AND returns DATA**. The GL read is only valid after the frame is rendered and **before** the buffer swap (the back buffer is GL-undefined post-swap), which is a different point in the host loop than the transport pump (`nt_devapi_update`, which runs GL-free at frame start). So a capture **defers**: the handler validates params, enqueues a producer, and returns nothing; at the next pre-swap seam the producer runs the readback → strip → encode → base64 and fills the slot's payload; the following poll yields it. A drain-race guard withholds a producer-slot whose payload is still pending so the reply is never the content-free `{deferred:true}`.

**The host installs the seam once.** Because the engine is code-first (the game owns the loop), the seam is a building block the host wires, not hidden behavior. A host calls **`nt_devapi_capture_install_seam()`** once at startup: it registers the capture seam as a generic **`nt_window` pre-swap hook** (run inside `nt_window_swap_buffers`, before the platform swap) and marks the host capture-capable. The frame loop then just renders + swaps — there is no per-frame call to forget, and render-off is inherited (no swap ⇒ no seam ⇒ the capture legitimately stalls until rendering resumes). A host that enables the group but never installs the seam (e.g. a headless host) rejects captures synchronously with `{error:"capture_unavailable"}` instead of hanging.

**Failure is a distinguishable envelope.** A producer that runs and fails (lost context, OOM, encode error) yields `{ok:false, error:"capture_failed"}`, never an `ok:true` shape without `data`. Bad bot input (bad `scale`, out-of-bounds / zero-size / non-dividing-`scale` rect) is rejected synchronously as `bad_params` and never asserts.

**Caps (DoS backstops, `-D` overridable).** `NT_DEVAPI_CAPTURE_MAX_PIXELS` (default `4096*4096`) bounds a single capture before any allocation; the producer's `uint32` size math is `_Static_assert`-proven wrap-free under that cap (×8 covers the base64 expansion). Concurrent in-flight captures share the generic deferred queue (`NT_DEVAPI_MAX_DEFERRED`, default `128`) and carry no group-specific concurrency cap: the dev-only client is request/response (effectively one capture in flight), so the shared queue is a sufficient backstop against a misbehaving batch client. The Python transport's recv-line cap is sized to cover the engine's worst-case capture line.

**OFF semantics — dev-only, compiled out.** The whole group is gated by `NT_DEVAPI_GROUP_CAPTURE` (default **OFF**, opt-in). When off, `nt_devapi_capture.c` is not compiled, the commands are absent from the registry (`unknown_method`) and discovery, and the vendored fpng encoder is not linked into the binary (zero release delta); as with all of devapi it also vanishes when `NT_DEVAPI_ENABLED` is OFF. `nt_fpng` is built `EXCLUDE_FROM_ALL`, so it compiles only when a capture-enabled target links it.

## Override-able compile-time options

The engine follows "use only what you need" — most subsystems are gated by a CMake `option(...)` or a `-D` override with a sane default, so a build pulls in only the code it asks for. This section is the **seed** of that catalogue, listing the **devapi** flags; other engine `-D` defaults fold in here over time (it is not yet an exhaustive index of every define).

**devapi build gates** (CMake `option(...)`, set at configure time):

| Flag | Default | Effect |
|---|---|---|
| `NT_DEVAPI_ENABLED` | OFF | Master gate. ON compiles the `engine/devapi` subdir and defines `NT_DEVAPI_ENABLED=1`; OFF compile-excludes the whole layer (zero devapi code/symbols in release). Every group flag below is a no-op while this is OFF. |
| `NT_DEVAPI_GROUP_CORE` | ON | Build the core group: `ping` / `engine.info` / `view`. |
| `NT_DEVAPI_GROUP_DISCOVERY` | ON | Build the discovery group: `endpoints` / `command.describe` / `features` (the live, self-describing catalog). |
| `NT_DEVAPI_GROUP_TIME` | ON | Build the `time.*` / `render.*` / `frame.*` group. |
| `NT_DEVAPI_GROUP_INPUT` | ON | Build the `input.*` synthetic-input group (player gate + inject scheduler). |
| `NT_DEVAPI_GROUP_UI` | OFF | Build the `ui.*` group (UI tree extraction + widget input). Requires `NT_UI_DEBUG_TOOLS` + `NT_DEVAPI_GROUP_INPUT` (hard CMake guards). |
| `NT_DEVAPI_GROUP_OBS` | OFF | Build the `log.*` / `perf.*` / `entity.*` / `resource.*` reads. Requires `NT_LOG_RING_ENABLED` + `NT_METRICS_ENABLED` + `NT_INTROSPECT_ENABLED`. |
| `NT_DEVAPI_GROUP_ENTITY_WRITE` | OFF | Build the `entity.set` component-write group. Requires `NT_INTROSPECT_WRITE_ENABLED`. |
| `NT_DEVAPI_GROUP_CAPTURE` | OFF | Build the `capture.frame` / `capture.region` PNG framebuffer-capture group (links `nt_gfx` + `nt_fpng`). |

**devapi caps & tunables** (`-D` overridable preprocessor defines, with their defaults):

| Define | Default | Effect |
|---|---|---|
| `NT_DEVAPI_DEFAULT_PORT` | `17890` | The native loopback-TCP listen port (env-overridable at host startup via `NT_DEVAPI_PORT`). |
| `NT_DEVAPI_STEP_MAX` | `1048576` (`1<<20`) | DoS backstop on `time.step{count}` / `{seconds}` — a request above it is `bad_params`, never a runaway advance. |
| `NT_DEVAPI_CAPTURE_MAX_PIXELS` | `4096*4096` | DoS backstop on a single `capture.*` before any allocation; the producer's `uint32` size math is `_Static_assert`-proven wrap-free under this cap. |
| `NT_INPUT_INJECT_QUEUE_MAX` | `256` | The bounded static-BSS immediate inject buffer `nt_input_poll` drains whole each poll. |

Each devapi group is its **own** static library (`nt_devapi_<group>`) that links only its deps, so a host links `nt_devapi` plus only the groups it wants (a capture-only host pulls `nt_gfx` + `nt_fpng`, never `nt_ui`); `nt_devapi_default` is the "every compiled-in group" bundle.
