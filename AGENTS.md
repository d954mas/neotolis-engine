## Project

Neotolis Engine — minimalist **C17** game engine for **Web/WASM** (WebGL 2). Code-first, modular. Builder does heavy work offline; runtime is intentionally simple.

## Source of truth

Check the spec before making code changes:

- `docs/spec/index.md` — start here: overview, chapter list, module → chapter map
- Changing a module → read its chapter in `docs/spec/` (chapters are small, read them whole)

If code and spec diverge, flag it explicitly in the response. Do not silently "normalize" behavior by guessing.

## Workflow

- Start work with a GitHub issue + feature branch **before** the first commit; never stack commits on local master. Branch naming: `{issue_number}-{slug}`, no `feature/` prefix.
- Navigate C code with clangd LSP first (definition / references / call hierarchy) — exact and cheaper than grep at this codebase size. After adding new `.c` files, rebuild `native-debug` so `compile_commands.json` picks them up; until then clangd diagnostics on new or platform-`#if` files are unreliable ("file not found", wrong `#if` branch) — confirm against a real build before chasing them.

## Build

- **Runtime**: WASM via Emscripten (`emcc`)
- **Builder**: native C binary
- **Standard**: C17
- **Why C17**: broader compiler, Emscripten toolchain, and build environment support
- **NT_STATIC_CRT** (CMake option, default ON): pins the static release CRT on Windows. Consumers embedding builder + runtime in one exe set OFF to inherit their own `CMAKE_MSVC_RUNTIME_LIBRARY`. All pinning goes through `nt_set_static_crt(_cxx)` — never raw `-U_DLL` (gated by `scripts/check_crt_pins.sh`).
- **NT_HYBRID_HPG** (CMake option, default ON): exe exports the NVIDIA/AMD hint symbols so hybrid-GPU Windows laptops run games on the discrete GPU. OFF for battery-friendly games/tools; the user's per-app Windows graphics preference always overrides the hint.

If specific build, check, or run commands appear in the repo, keep them up to date in this file.

### Bootstrap from a clean clone

On Windows run these from Git Bash / MSYS — the system `bash.exe` routes to WSL,
which is not the supported environment for `scripts/*.sh`.

```
git lfs pull                          # example assets are LFS pointers without this
bash scripts/setup.sh                 # install + activate the pinned emsdk (.emsdk-version)
                                      # later sessions: source emsdk/emsdk_env.sh
cmake --preset native-debug           # the three presets check.sh expects:
emcmake cmake --preset wasm-debug
emcmake cmake --preset wasm-release
bash scripts/check.sh                 # sanity check that the environment is alive
```

Native example builds produce their asset packs automatically (pack builders are
wired into the build graph — `cmake/nt_example_packs.cmake`); wasm presets copy
packs a prior native build produced — build a native preset first (a wasm build
attempted before that fails loudly, and succeeds once the packs exist).
The FIRST native build cold-encodes the sponza pack — hours, not the usual
"warm ~12 s" gate; `build/examples/*/_cache` makes every rerun seconds. To
defer that cost, configure once with `-DNT_SKIP_EXAMPLE_PACKS=sponza` (a
persistent cache var — reset it with `-DNT_SKIP_EXAMPLE_PACKS=` when you need
sponza). Packs depend only on the builder exe — after editing shader/asset
sources, delete the `.ntpack` before visual QA to force a repack.

## Philosophy

1. **Code-first** — game controls the main loop. The engine gives building blocks, not a pipeline.
2. **Explicit over implicit** — you see everything. No hidden behavior, no magic behind the scenes.
3. **Keep it simple** — less code is better. Simplify further when possible.
4. **Tiny size** — every byte counts. Binary size tracked on every PR.
5. **Set of modules** — use only what you need.
6. **Prebuilt assets** — source formats packed into binaries at build time. Runtime loads packs on demand, no parsers.

## Working principles

- **Data-oriented** for renderer and components (SoA, dense iteration, typed handles). Not everything — input, window, app stay simple structs.
- **Platform abstraction** — all platform calls go through engine wrappers, never call browser/OS API directly from modules.
- **No heap in hot path** — use compile-time limits (`#define`), preallocated storages, frame scratch memory.
- **Builder validates, runtime is a safety net** — runtime checks only magic/version/type, handles fallbacks gracefully. No heavy validation at runtime.
- **Fail early, prefer asserts** — when something is wrong, prefer crashing over silent fallbacks. `NT_ASSERT` for invariants and unexpected states, `NT_BUILD_ASSERT` in the builder (programmer invariants, unexpected states, OOM, a missing/unreadable file, and a decode failure in the single-asset adds). Error returns are fine as part of public API contracts (resource not found → NULL), but never to silently swallow a bug or broken data. Release default is TRAP. OFF remains available only as an unsupported size-oriented escape hatch.
  - `NT_ASSERT_MODE=OFF` compiles `NT_ASSERT` to `((void)0)`. Once an asserted precondition is violated, behavior is undefined; no fallback path is required solely for OFF. Assert expressions must still be side-effect-free because OFF does not evaluate them. Hard guards belong only at untrusted/runtime-input boundaries or where a public API promises recoverable rejection.
  - Exception: CONTENT-dependent sprite failures inside the ATLAS builder route to the graceful `nt_builder_get_errors` channel instead of aborting, so one bad sprite cannot kill a build or a GUI. Full list of those cases: [docs/spec/builder/builder.md](docs/spec/builder/builder.md#asserts-vs-graceful-content-errors).

## Code style

- **Comments: short WHY only.** Single-line preferred, never more than 2-3. Explain a non-obvious decision or hidden constraint — not what the code does (identifiers do that). If you need more than 2-3 lines, the code probably needs refactoring, not commenting.
  - **Do not write**: historical context (`Pre-fix the X was Y, then commit ab6d235 moved it…`), Phase/REVIEW/CHUNK tags, commit SHAs, PR numbers, issue numbers, test-name pins (`pin: test_X`), user quotes, "what changed and why" narratives, `EXPERIMENTAL` boilerplate paragraphs. Those belong in commit messages, PR descriptions, or the changelog — not in source.
  - **Do write**: one-line invariants the reader can't derive from the code (`Walker layer-sort relies on debug layers being >= 240.`), short safety notes (`Pointer must outlive the layout solve.`), or a brief WHY where a non-obvious choice was made (`Direct-map avoids hash-table realloc in hot path.`).
- Use `// #region name` / `// #endregion` to mark logical sections inside long functions (VS Code foldable regions). No blank line after `// #region` or before `// #endregion`. Do not remove existing short inline comments when adding regions — regions group, comments explain.
- Organize large files with regions instead of splitting into more TUs — cross-TU calls block inlining on the hot path (no LTO).

## Before adding a new subsystem

- Diagram coordinate/data transforms between all systems involved.
- Check API consistency with parallel subsystems (if sprites have it, UI images need it too).
- Verify types and ranges will scale (target: mobile WASM).
- Prototype the riskiest integration point before building the rest.
- Test with asymmetric data that breaks on axis swap or flip.

## Change rules

- Do not introduce monolithic subsystems where the spec requires a composable module set.
- Do not move game responsibility into the engine without explicit architectural justification.
- When adding a new subsystem, verify it does not conflict with the spec on explicit-over-implicit and runtime simplicity.
- If a temporary deviation from the spec is necessary, mark it explicitly in the change comment and the final report.
- New UI widget demos go into the existing `examples/ui_showcase` vitrine (new tab) — never a new example dir.
- Never id sibling widgets as `base_id + index`: Clay's anonymous child ids are additive (`seed + offset`), so consecutive seeds collide → DUPLICATE_ID. Salt sub-ids with a mixed hash (`nt_ui_child_id` / `nt_ui_fmix_id`). Virtualized widgets must RECYCLE ids (see `nt_ui_vlist`'s ring) or Clay's element hashmap saturates until `build_tree` asserts.
- Never fold a pool handle or a small enum linearly into a cache key (`handle*K + field`): handles are sequential, so a one-step change on one side equals a one-step change on the other and neighbouring resources silently share the cached object. Pipeline caches key on `nt_gfx_pipeline_key_t` (exact); other GPU-object caches pack their identity bit-exact with `_Static_assert`ed lane widths, or hash the whole canonical struct via `nt_hash64` when the identity is content. Every new cache key ships with a neighbour test: consecutive handles × one-step change of each field → all keys distinct (gate: `scripts/check_cache_keys.sh`).

## Performance and hot path

- Hot path includes at minimum: frame loop, fixed update loop, render item generation, batching, resource resolve per frame, and any dense ECS/SoA iterations.
- In hot path: no heap allocation, no hidden container realloc, no unnecessary copies, no heavy abstraction layers.
- Prefer dense data, predictable memory access patterns, and simple control flow.

## Pre-commit checks

**After each edit burst and before every commit:** run

```
bash scripts/format_and_check.sh
```

It auto-formats changed files (`fmt.sh`) under the same run lock, then runs the full read-only check
(warm: ~12 s, ~25 s when builder/atlas paths changed — the three atlas-bench
guard tests auto-run only then; `--push`/`--full` always run them). `check.sh`
direct modes never mutate files; `format_and_check.sh` opts into the formatter before the checks.

It runs the cheap gates (module composition, EM_JS_DEPS, doc links + spec-index coverage, CRT-pin centralization), builds native-debug, runs ctest, then checks clang-format and clang-tidy on changed files only (falls back to full tidy when headers changed). clang-tidy uses a devapi-enabled compile DB matching the CI lint job, so devapi TUs are checked, not skipped. Vendored deps (`deps/clay`, `deps/cglm`, `deps/unity`, `deps/basisu`, `deps/glfw`, `deps/curl`, `deps/zlib`) follow upstream style and are excluded; review patches to them separately.

- Before `git push`: `bash scripts/check.sh --push` — additionally builds wasm-debug (emscripten catches warnings native clang exempts), wasm-release (Closure-only failures are invisible to debug builds), and runs the submodule consumption test.
- Full sweep (CI lint equivalent): `bash scripts/check.sh --full` — whole-tree format + full tidy.

If any check fails — fix before committing. Do not commit code that hasn't passed.

### Known CI-only failure classes after a green `--push`

Environment differences a local Windows host cannot reproduce:

- **GNU ld link order** — Linux resolves archives left-to-right; Windows/wasm links don't. Swappable impls + stubs must trail every consumer (`... nt_resource ... nt_http_stub nt_fs_stub nt_log_stub` last; see `tests/submodule/CMakeLists.txt`). A standalone test using libm also needs `if(NOT WIN32) target_link_libraries(<test> PRIVATE m) endif()`.
- **clang-format version skew** — CI's Linux clang-format flags multi-space-aligned trailing comments the local one accepts. Keep trailing comments single-spaced.
- **emsdk pin skew** — CI installs `.emsdk-version`; if local `emcc --version` differs, wasm-release/Closure can false-green locally. Compare versions before trusting it.
- **clang-tidy skips `#if defined(__linux__)` blocks off-Linux** — reason about platform-`#if` code as Linux code or add `NOLINT` defensively.
- **Browser Smoke runs under LeakSanitizer, headless** — skip `glfwInit` when neither `DISPLAY` nor `WAYLAND_DISPLAY` is set.
- **CI debug ctest runs under ASan/LeakSanitizer** — an `NT_TEST_EXPECT_ASSERT` that longjmps past a live `malloc` fails the test with a leak report printed *after* Unity's `OK`; code an assert-trip test crosses must hold no heap (stack or preallocated buffers).
- **CI ctest must stay serial** — the real-GL tests share one xvfb display; parallel ctest there fails `glfwInit`. Local `-j` is safe (desktop GL); the real-GL tests hold `RESOURCE_LOCK gl_display` (list in `cmake/test_target.cmake`).
- **CI native-release passes a global `-DNT_ASSERT_MODE`** — a per-target `-D` collides (`-Wmacro-redefined` under `-Werror`). Force a different assert mode via a wrapper TU with `#undef`/`#define` (pattern: `tests/unit/test_helpers/nt_atlas_assert_off_tu.c`).
- **Local tidy can false-green NEW files** — before pushing new test/tool files run `clang-tidy -p build/_cmake/tidy-ci <file>` directly (the devapi-enabled DB check.sh creates; plain native-debug lacks devapi TUs); that reproduces CI.

## Test-infra & debugging gotchas

- Only a fresh full `check.sh` run is authoritative — targeted builds + ctest can pass on stale binaries after an edit burst.
- Never run two `check.sh`/`ctest` in one tree at once (agent + lead included): shared test outputs and relinked exes make builder/atlas tests fail spuriously. `check.sh` holds `build/.check.lock` and exits 2 while another run is active; `rmdir` it only if the other run is dead.
- A failed test's name and output after `check.sh`: `build/_cmake/native-debug/check-ctest.log` (kept on disk) or `Testing/Temporary/LastTestsFailed.log` — do not rely on a `tail`-truncated terminal.
- A failed `NT_BUILD_ASSERT` aborts the test process; if a dead process still holds the exe (next link fails with permission denied), `taskkill //F //IM <test>.exe`. Deliberate assert-trip tests: run the binary directly, not through ctest.
- A crashing test that prints nothing: diagnose with `lldb -b -o run -o bt <exe>` (gdb absent; MSVC CRT buffers stdout to pipes and ignores stdbuf).
- `UNITY_EXCLUDE_FLOAT` is defined — float Unity asserts compile to nothing; compare small exact values via `(int32_t)` casts.
- `nt_atlas_begin` requires atlas-level `shape == RECT` when `extrude > 0`.
- Changing a validator contract: first grep every constructor of that data shape — spec literals AND parameterized helpers.
- `nt_builder.lib` is not linkable ad hoc from a shell (unresolved glad/cgltf externals outside its CMake `PUBLIC` link set) — behavioural probes need a real CMake target.
- `examples/*/generated/*.h` are builder output committed to git; pack builds run inside every native build, so if one dirties them the committed copies were stale — commit the refresh (output is deterministic, no timestamps).
- Visual QA: self-capture of GL windows (GDI/PrintWindow) does not work here. Pixel-exact checks: devapi `capture.frame` (glReadPixels, works headless, needs `NT_DEVAPI_ENABLED=ON` + CAPTURE group). Aesthetics/layout: ask the user to run and look — say explicitly what to check.
- Browser smoke tests drive `tests/browser/app` (`window.__nt` hooks), not the showcase.
- wasm links failing with `node.exe ... returned 3221225794` (0xC0000142) on random emscripten tools = transient Windows process-spawn exhaustion under parallel links — retry once before investigating.
- New EM_JS that allocates into the wasm heap: use `wasmExports['malloc']` — `Module['_malloc']` fails at runtime under emmalloc, bare `_malloc` fails Closure (pattern: `engine/http/web/nt_http_web.c`).

## Evidence standard

State the changed claim in one sentence; prove it with the NARROWEST check that
exercises that claim; the result must match expected behavior, not exit 0.
**Not enough:** build success as runtime proof; a generic green command; "could
not test" treated as pass (report `unverified` + the next concrete command);
errors in logs ignored because the exit code was 0. An existing binary is never
freshness evidence — before running an example/bench by hand, `cmake --build`
first (a no-op build costs sub-second). Record rejected/superseded approaches in
the issue so the next session does not re-propose them.

## Reviewing a branch

For a full pre-merge review against engine principles, spec, correctness, and tests, use the `reviewing-engine-code` skill in `.claude/skills/` — say "review this branch". It spawns parallel focus-lens reviewers (including a mandatory engine-principle lens driven by its `references/principle-catalog.md`), adversarially verifies findings, and reports P0-P2. Claude Code auto-discovers it; in Codex, read `.claude/skills/reviewing-engine-code/SKILL.md` directly (it is a self-contained, runtime-agnostic playbook) or install it into `~/.codex/skills/`.

If build or test infrastructure is missing, state it explicitly in the response — do not imply the check was done.

## Developer Profile

### Response Style
- Concise by default: the result in the first 1-2 lines, then only the delta —
  no context recap, no repeated summaries, routine ops (commit/gate/push) = one line.
- Expand only for: new concepts, decisions with trade-offs, or when asked ("подробно").
  Tables only when comparing ≥3 options.

### Decisions & Libraries
- Present multiple options with trade-offs, don't choose for the developer.
  Respect library choices — when suggesting dependencies, include size impact,
  benchmarks, and how other engines handle it.

### Debugging
- Developer hypothesis — verify first, confirm or refute with evidence from code.
  Bug without hypothesis — diagnose and fix independently.
  Don't restate what the developer already wrote.

### Boundaries
- Execute what was requested precisely, no deviations.
  Improvements and findings — suggest separately after the main task.
