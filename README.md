# Neotolis Engine

Minimalist C17 game engine for Web/WASM (WebGL 2).

**[Website](https://neotolis-engine.dev)**

## Philosophy

1. **Code-first** — game controls the main loop. The engine gives building blocks, not a pipeline.
2. **Explicit over implicit** — you see everything. No hidden behavior, no magic behind the scenes.
3. **Keep it simple** — less code is better. Simplify further when possible.
4. **Tiny size** — every byte counts. Binary size tracked on every PR.
5. **Set of modules** — use only what you need.
6. **Prebuilt assets** — source formats packed into binaries at build time. Runtime loads packs on demand, no parsers.

## Prerequisites

- **CMake** 3.25+
- **Ninja** build system
- **Clang** (LLVM) -- recommended for both native and cross-compilation consistency
- **Emscripten SDK** -- for WASM builds (setup automated via `scripts/setup.sh`)

## Quick Start

Bootstrap the Emscripten SDK (first time only):

```bash
./scripts/setup.sh
source emsdk/emsdk_env.sh
```

## Build

Eleven primary CMake presets are available: `wasm-debug`, `wasm-debug-paired`, `wasm-debug-simd`, `wasm-release`, `wasm-release-paired`, `wasm-release-simd`, `wasm-analysis`, `wasm-analysis-paired`, `wasm-analysis-simd`, `native-debug`, `native-release`.

### WASM (requires emsdk activated)

There are three explicit WASM modes for each build tier:

- `wasm-debug` / `wasm-release` / `wasm-analysis` — baseline-only output.
- `wasm-debug-paired` / `wasm-release-paired` / `wasm-analysis-paired` — baseline shell plus packaged `index_simd.wasm` beside it.
- `wasm-debug-simd` / `wasm-release-simd` / `wasm-analysis-simd` — standalone SIMD output.

Baseline-only build:

```bash
emcmake cmake --preset wasm-debug
cmake --build --preset wasm-debug
```

Standalone SIMD build:

```bash
emcmake cmake --preset wasm-debug-simd
cmake --build --preset wasm-debug-simd
```

Explicit paired baseline+SIMD build in one command:

```bash
source emsdk/emsdk_env.sh
./scripts/build_wasm_paired.sh wasm-debug-paired
```

The paired build script uses `emcmake cmake --preset ...` for both configure steps,
then builds both presets and copies each SIMD wasm beside the paired example
output as `index_simd.wasm`. Baseline-only presets do not inject a SIMD path
into the shell. Paired presets do inject that path, so the shell performs the
SIMD probe, logs the selected wasm variant in the browser console, and picks
exactly one wasm at runtime.

If you build presets separately, you can package examples manually:

```bash
./scripts/package_wasm_simd.sh hello wasm-debug-paired wasm-debug-simd
```

Recommended validation flow when you want confidence in both variants:

```bash
emcmake cmake --preset wasm-debug
cmake --build --preset wasm-debug
ctest --preset wasm-debug

emcmake cmake --preset wasm-debug-simd
cmake --build --preset wasm-debug-simd
ctest --preset wasm-debug-simd

./scripts/build_wasm_paired.sh wasm-debug-paired
ctest --preset wasm-debug-paired
node tests/wasm/smoke_test.js wasm-debug-paired
node tests/wasm/smoke_test.js wasm-debug-simd
```

`test_wasm_simd_loader` verifies the SIMD capability probe and wasm-path
switching logic directly in Node.js. `test_wasm_smoke` can be run against
baseline-only, paired baseline, and standalone SIMD presets to confirm each
produced module still loads.

### Native

```bash
cmake --preset native-debug
cmake --build --preset native-debug

cmake --preset native-release
cmake --build --preset native-release
```

## Tests

```bash
ctest --test-dir build/_cmake/native-debug --output-on-failure
```

## Running

### WASM (Hello example)

```bash
emrun build/_cmake/wasm-debug/examples/hello/index.html
```

### Native (Builder)

```bash
./build/_cmake/native-debug/tools/builder/builder
```

## Project Structure

```
neotolis-engine/
  engine/
    app/              -- application lifecycle (+ native/ web/ backends)
    core/             -- nt_core, nt_types, nt_platform (init, shutdown, types)
    input/            -- input system (+ native/ web/ stub/ backends)
    log/              -- logging (+ default/ stub/ backends)
    platform/web/     -- Emscripten glue (shell.html, pre.js, library.js)
    time/             -- frame timing
    window/           -- window management (+ native/ web/ backends)
  shared/
    include/          -- nt_formats.h (shared between runtime and builder)
  tools/
    builder/          -- native offline asset builder
  examples/
    hello/            -- minimal WASM example
  tests/
    unit/             -- unit tests (core, app, input, log, time, window)
    wasm/             -- WASM smoke tests
  deps/
    cglm/             -- vendored cglm (header-only math library)
    unity/            -- vendored Unity test framework
  cmake/              -- CMake helper modules (warnings, nt_module, nt_shell)
  scripts/            -- build & CI scripts (setup, tidy, size reports)
  docs/               -- specifications and architecture documents
```

## License

MIT -- see [LICENSE](LICENSE) for details.
