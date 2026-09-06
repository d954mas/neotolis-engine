# Neotolis Engine

Minimalist C17 game engine for Web/WASM (WebGL 2).

**[Website](https://neotolis-engine.dev)**

## Philosophy

Code-first (the game owns the main loop), explicit over implicit, keep it simple,
tiny size, a set of modules you opt into, and prebuilt assets — source formats are
packed offline so the runtime carries no parsers. The full statement with its
engine/game ownership boundary is
[docs/spec/core/principles.md](docs/spec/core/principles.md).

## Prerequisites

- **CMake** 3.25+
- **Ninja** build system
- **Clang** (LLVM) -- recommended for both native and cross-compilation consistency
- **Emscripten SDK** -- for WASM builds (setup automated via `scripts/setup.sh`)
- **OpenSSL dev headers** (Linux/macOS only: `libssl-dev` / brew `openssl@3`) -- TLS for the
  libcurl-backed native HTTP module; skip entirely with `-DNT_HTTP_CURL=OFF` (Windows uses
  Schannel). Submodule builds default to OFF -- set `-DNT_HTTP_CURL=ON` to get real native HTTP
- **Python 3** -- optional; without it the native HTTP acceptance test is skipped

## Quick Start

Bootstrap the Emscripten SDK (first time only):

```bash
./scripts/setup.sh
source emsdk/emsdk_env.sh
```

## Build

Primary CMake presets: `wasm-debug`, `wasm-debug-paired`, `wasm-debug-simd`, `wasm-release`, `wasm-release-paired`, `wasm-release-simd`, `wasm-analysis`, `wasm-analysis-paired`, `wasm-analysis-simd`, `native-debug`, `native-release` (the full list, including the `*-test` variants, is `CMakePresets.json`).

One rule: `*-debug` presets build the dev tooling (`NT_UI_DEBUG_TOOLS=ON` and the log ring / metrics / introspection options it defaults on), `*-release` presets build exactly what ships (all of them OFF). Anything else is an explicit `-D`, e.g. `-DNT_METRICS_ENABLED=ON` on a release preset to profile with the overlay. Option defaults apply to a fresh cache only: after a preset policy change, delete the build dir and reconfigure. These presets exist for developing the engine; a game writes its own presets (or inherits the policy-free hidden `base-wasm` / `base-native`) and sets the engine options explicitly.

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
  engine/             -- one dir per module (nt_<mod>.h + optional native/ web/ stub/ backends)
  shared/include/     -- binary formats shared by runtime and builder (nt_*_format.h)
  tools/builder/      -- native offline asset builder
  examples/           -- runnable examples, each with its own build_packs
  tests/              -- unit/ (native), wasm/ (smoke), browser/ (Playwright), submodule/
  deps/               -- vendored dependencies (cglm, clay, unity, basisu, glfw, ...)
  cmake/              -- CMake helper modules (warnings, module/test targets, shell)
  scripts/            -- build, check and CI scripts
  docs/spec/          -- the specification; start at docs/spec/index.md
```

The module → chapter map and a task → entry-point table live in
[docs/spec/index.md](docs/spec/index.md).

## License

MIT -- see [LICENSE](LICENSE) for details.
