## Project

Neotolis Engine — minimalist **C17** game engine for **Web/WASM** (WebGL 2). Code-first, modular. Builder does heavy work offline; runtime is intentionally simple.

## Source of truth

Check the spec before making code changes:

- `docs/spec/index.md` — start here: overview, chapter list, module → chapter map
- Changing a module → read its chapter in `docs/spec/` (chapters are small, read them whole)

If code and spec diverge, flag it explicitly in the response. Do not silently "normalize" behavior by guessing.

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
- **Fail early, prefer asserts** — when something is wrong, prefer crashing over silent fallbacks. `NT_ASSERT` for invariants and unexpected states, `NT_BUILD_ASSERT` in the builder. Error returns are fine as part of public API contracts (e.g. resource not found → return NULL), but don't use error codes to silently swallow problems that indicate bugs or broken data. Release default is TRAP (immediate crash, no strings); OFF mode is available via CMake override for final production builds. In the builder, programmer invariants, unexpected states, and OOM assert (`NT_BUILD_ASSERT`) — the developer sees the problem instantly instead of waiting for the build to finish. A missing or unreadable file also asserts, and the single-asset adds (`add_texture`/`add_mesh`/`add_font`) assert on a decode/parse failure. But CONTENT-dependent failures of sprite images inside the ATLAS builder — undecodable/oversized/zero-dimension sprite images, transparent-after-trim sprites, oversized slice9 borders, degenerate hulls, trim-offset overflow, duplicate names, size/page limits, unfittable sprites — route to the graceful content-error channel (`nt_builder_get_errors`) instead of aborting, so the atlas builder and any GUI survive one bad sprite with an actionable message.

## Code style

- **Comments: short WHY only.** Single-line preferred, never more than 2-3. Explain a non-obvious decision or hidden constraint — not what the code does (identifiers do that). If you need more than 2-3 lines, the code probably needs refactoring, not commenting.
  - **Do not write**: historical context (`Pre-fix the X was Y, then commit ab6d235 moved it…`), Phase/REVIEW/CHUNK tags, commit SHAs, PR numbers, issue numbers, test-name pins (`pin: test_X`), user quotes, "what changed and why" narratives, `EXPERIMENTAL` boilerplate paragraphs. Those belong in commit messages, PR descriptions, or the changelog — not in source.
  - **Do write**: one-line invariants the reader can't derive from the code (`Walker layer-sort relies on debug layers being >= 240.`), short safety notes (`Pointer must outlive the layout solve.`), or a brief WHY where a non-obvious choice was made (`Direct-map avoids hash-table realloc in hot path.`).
- Use `// #region name` / `// #endregion` to mark logical sections inside long functions (VS Code foldable regions). No blank line after `// #region` or before `// #endregion`. Do not remove existing short inline comments when adding regions — regions group, comments explain.

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

## Performance and hot path

- Hot path includes at minimum: frame loop, fixed update loop, render item generation, batching, resource resolve per frame, and any dense ECS/SoA iterations.
- In hot path: no heap allocation, no hidden container realloc, no unnecessary copies, no heavy abstraction layers.
- Prefer dense data, predictable memory access patterns, and simple control flow.

## Pre-commit checks

**Before every commit and task completion:** run

```
bash scripts/check.sh
```

It runs the cheap gates (module composition, EM_JS_DEPS, doc links + spec-index coverage, CRT-pin centralization), builds native-debug, runs ctest, then checks clang-format and clang-tidy on changed files only (falls back to full tidy when headers changed). clang-tidy uses a devapi-enabled compile DB matching the CI lint job, so devapi TUs are checked, not skipped. Vendored deps (`deps/clay`, `deps/cglm`, `deps/unity`, `deps/basisu`, `deps/glfw`) follow upstream style and are excluded; review patches to them separately.

- Before `git push`: `bash scripts/check.sh --push` — additionally builds wasm-debug (emscripten catches warnings native clang exempts), wasm-release (Closure-only failures are invisible to debug builds), and runs the submodule consumption test.
- Full sweep (CI lint equivalent): `bash scripts/check.sh --full` — whole-tree format + full tidy.

If any check fails — fix before committing. Do not commit code that hasn't passed.

Known CI-only failure class after a green `--push`: GNU ld link order. The Linux linker resolves archives left-to-right; Windows/wasm links don't, so a wrong link order only fails in CI's native job. If CI fails at link while local passes, fix the archive order (see the comment in `tests/submodule/CMakeLists.txt`).

## Reviewing a branch

For a full pre-merge review against engine principles, spec, correctness, and tests, use the `reviewing-engine-code` skill in `.claude/skills/` — say "review this branch". It spawns parallel focus-lens reviewers (including a mandatory engine-principle lens driven by its `references/principle-catalog.md`), adversarially verifies findings, and reports P0-P2. Claude Code auto-discovers it; in Codex, read `.claude/skills/reviewing-engine-code/SKILL.md` directly (it is a self-contained, runtime-agnostic playbook) or install it into `~/.codex/skills/`.

If build or test infrastructure is missing, state it explicitly in the response — do not imply the check was done.

## Developer Profile

### Response Style
- Detailed responses with tables, options, and explanations.
  New concepts — explain thoroughly.

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
