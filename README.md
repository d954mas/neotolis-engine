# Neotolis Engine

Minimalist C17 game engine for Web/WASM (WebGL 2). Data-oriented architecture, code-first design. The builder does heavy work offline; the runtime is intentionally simple.

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

Four CMake presets are available: `wasm-debug`, `wasm-release`, `native-debug`, `native-release`.

### WASM (requires emsdk activated)

```bash
emcmake cmake --preset wasm-debug
cmake --build --preset wasm-debug

emcmake cmake --preset wasm-release
cmake --build --preset wasm-release
```

### Native

```bash
cmake --preset native-debug
cmake --build --preset native-debug

cmake --preset native-release
cmake --build --preset native-release
```

## Running

### WASM (Hello example)

```bash
emrun build/examples/hello/wasm-debug/index.html
```

### Native (Builder)

```bash
./build/tools/builder/native-debug/builder
```

## Project Structure

```
neotolis-engine/
  engine/
    runtime/
      core/           -- nt_core.h/c, nt_types.h (init, shutdown, types)
      platform/       -- platform abstraction layer (future)
      memory/         -- memory management (future)
      web/            -- shell.html, pre.js, library.js (Emscripten)
    graphics/         -- rendering subsystem (future)
    systems/          -- input, audio subsystems (future)
  shared/
    include/          -- nt_formats.h (shared between runtime and builder)
  tools/
    builder/          -- native offline asset builder
  examples/
    hello/            -- minimal WASM example
  tests/
    unit/             -- unit tests
    wasm/             -- WASM integration tests
  deps/
    cglm/             -- vendored cglm v0.9.6 (header-only math library)
  scripts/
    setup.sh          -- emsdk bootstrap script
  docs/               -- specifications and architecture documents
```

## License

MIT -- see [LICENSE](LICENSE) for details.
