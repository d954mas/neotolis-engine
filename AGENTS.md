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

Running an example additionally needs its asset packs: build the example's
`build_<name>_packs` target, then run the produced binary with the example's build dir,
e.g. `cmake --build --preset native-debug --target build_ui_showcase_packs &&
./build/examples/ui_showcase/native-debug/build_ui_showcase_packs build/examples/ui_showcase`.
Packs depend only on the builder exe — after editing shader/asset sources, delete the
`.ntpack` before visual QA to force a repack.

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
- **Fail early, prefer asserts** — when something is wrong, prefer crashing over silent fallbacks. `NT_ASSERT` for invariants and unexpected states, `NT_BUILD_ASSERT` in the builder. Error returns are fine as part of public API contracts (e.g. resource not found → return NULL), but don't use error codes to silently swallow problems that indicate bugs or broken data. Release default is TRAP (immediate crash, no strings); OFF mode is available via CMake override for final production builds. In the builder, programmer invariants, unexpected states, and OOM assert (`NT_BUILD_ASSERT`) — the developer sees the problem instantly instead of waiting for the build to finish. A missing or unreadable file also asserts, and the single-asset adds (`add_texture`/`add_mesh`/`add_font`) assert on a decode/parse failure. `NT_ASSERT` compiles to `((void)0)` under `NT_ASSERT_MODE=OFF` (a legal shipping config): never put side effects or the only bounds check inside an assert — runtime parsers of untrusted data need hard guards that survive assert-off. But CONTENT-dependent failures of sprite images inside the ATLAS builder — undecodable/oversized/zero-dimension sprite images, transparent-after-trim sprites, oversized slice9 borders, contour-vertex overflow, trim-offset overflow, duplicate names, size/page limits, unfittable sprites — route to the graceful content-error channel (`nt_builder_get_errors`) instead of aborting, so the atlas builder and any GUI survive one bad sprite with an actionable message.

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

## Performance and hot path

- Hot path includes at minimum: frame loop, fixed update loop, render item generation, batching, resource resolve per frame, and any dense ECS/SoA iterations.
- In hot path: no heap allocation, no hidden container realloc, no unnecessary copies, no heavy abstraction layers.
- Prefer dense data, predictable memory access patterns, and simple control flow.

## Pre-commit checks

**After each edit burst and before every commit:** run

```
bash scripts/format_and_check.sh
```

It auto-formats changed files (`fmt.sh`) and then runs the full read-only check
(warm: ~12 s, ~25 s when builder/atlas paths changed — the three atlas-bench
guard tests auto-run only then; `--push`/`--full` always run them). `check.sh`
itself never mutates files; the formatter lives in `fmt.sh`.

It runs the cheap gates (module composition, EM_JS_DEPS, doc links + spec-index coverage, CRT-pin centralization), builds native-debug, runs ctest, then checks clang-format and clang-tidy on changed files only (falls back to full tidy when headers changed). clang-tidy uses a devapi-enabled compile DB matching the CI lint job, so devapi TUs are checked, not skipped. Vendored deps (`deps/clay`, `deps/cglm`, `deps/unity`, `deps/basisu`, `deps/glfw`) follow upstream style and are excluded; review patches to them separately.

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
- **CI ctest must stay serial** — the real-GL tests share one xvfb display; parallel ctest there fails `glfwInit`. Local `-j` is safe (desktop GL); the two GL tests hold `RESOURCE_LOCK gl_display`.
- **CI native-release passes a global `-DNT_ASSERT_MODE`** — a per-target `-D` collides (`-Wmacro-redefined` under `-Werror`). Force a different assert mode via a wrapper TU with `#undef`/`#define` (pattern: `tests/unit/test_helpers/nt_atlas_assert_off_tu.c`).
- **Local tidy can false-green NEW files** — before pushing new test/tool files run `clang-tidy -p build/_cmake/tidy-ci <file>` directly (the devapi-enabled DB check.sh creates; plain native-debug lacks devapi TUs); that reproduces CI. A bogus `'X.h' file not found` attributed to a header in CI tidy output is a tidy.sh retry artifact — fix the header's real diagnostic and it disappears.

## Test-infra & debugging gotchas

- Only a fresh full `check.sh` run is authoritative — targeted builds + ctest can pass on stale binaries after an edit burst.
- A failed `NT_BUILD_ASSERT` aborts the test process; if a dead process still holds the exe (next link fails with permission denied), `taskkill //F //IM <test>.exe`. Deliberate assert-trip tests: run the binary directly, not through ctest.
- A crashing test that prints nothing: diagnose with `lldb -b -o run -o bt <exe>` (gdb absent; MSVC CRT buffers stdout to pipes and ignores stdbuf).
- `UNITY_EXCLUDE_FLOAT` is defined — float Unity asserts compile to nothing; compare small exact values via `(int32_t)` casts.
- `nt_atlas_begin` requires atlas-level `shape == RECT` when `extrude > 0`.
- Changing a validator contract: first grep every constructor of that data shape — spec literals AND parameterized helpers.
- `nt_builder.lib` is not linkable ad hoc from a shell (unresolved glad/cgltf externals outside its CMake `PUBLIC` link set) — behavioural probes need a real CMake target.
- `examples/{atlas,bunnymark,text}/generated/*.h` are stale in git (pack targets aren't in the default build). Revert, don't commit, if a generator run dirties them.
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
