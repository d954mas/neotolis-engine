## Project

Neotolis Engine — minimalist **C17** game engine for **Web/WASM** (WebGL 2). Code-first, modular. Builder does heavy work offline; runtime is intentionally simple.

## Source of truth

Check the spec before making code changes:

- `docs/neotolis_engine_spec_1.md` — current architectural and technical baseline

If code and spec diverge, flag it explicitly in the response. Do not silently "normalize" behavior by guessing.

## Build

- **Runtime**: WASM via Emscripten (`emcc`)
- **Builder**: native C binary
- **Standard**: C17
- **Why C17**: broader compiler, Emscripten toolchain, and build environment support

If specific build, check, or run commands appear in the repo, keep them up to date in this file.

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
- **Fail early, prefer asserts** — when something is wrong, prefer crashing over silent fallbacks. `NT_ASSERT` for invariants and unexpected states, `NT_BUILD_ASSERT` in the builder. Error returns are fine as part of public API contracts (e.g. resource not found → return NULL), but don't use error codes to silently swallow problems that indicate bugs or broken data. Release default is TRAP (immediate crash, no strings); OFF mode is available via CMake override for final production builds. In the builder, bad input (missing file, bad format, validation failure) is an assert — the developer sees the problem instantly instead of waiting for the build to finish.

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

**Before every commit and task completion:**

1. Build affected targets: `cmake --build build/_cmake/native-debug`
2. Tests: `ctest --test-dir build/_cmake/native-debug --output-on-failure`
3. Formatting: `clang-format --dry-run --Werror <affected .c/.h files>`
4. Static analysis: `bash scripts/tidy.sh build/_cmake/native-debug`

If any check fails — fix before committing. Do not commit code that hasn't passed all four checks.

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

