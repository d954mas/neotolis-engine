# Building, checks and troubleshooting

Operational reference for developers and agents. Shared working rules live in
[AGENTS.md](../AGENTS.md); engine behavior lives in the [specification](spec/index.md).
Update this file when build commands or configuration change.

## Setup

The runtime targets WASM through Emscripten; the builder is native. Engine code
uses C17 for compiler and toolchain compatibility. Prerequisites and the explicit
baseline/SIMD/paired build modes are in [README.md](../README.md#prerequisites).

On Windows use Git Bash / MSYS for `scripts/*.sh`; the system `bash.exe` may
launch WSL, which is not the supported shell for this Windows build.

```bash
git lfs pull
bash scripts/setup.sh
source emsdk/emsdk_env.sh
cmake --preset native-debug
cmake --preset native-release
emcmake cmake --preset wasm-debug
emcmake cmake --preset wasm-release
bash scripts/check.sh
```

`setup.sh` installs and activates the version pinned by `.emsdk-version`.
Activate that SDK again in later shell sessions. LFS checkout is required:
asset pointer files are not usable example assets or benchmark inputs.

### Example packs

Native example builds generate packs through `cmake/nt_example_packs.cmake`.
WASM builds copy packs produced by a native build; build native first. Missing
packs cause an explicit WASM build failure.

The first Sponza encode can take hours. To defer it, configure native builds
with `-DNT_SKIP_EXAMPLE_PACKS=sponza`; this cache setting persists. Reset it with
`-DNT_SKIP_EXAMPLE_PACKS=` when Sponza is needed. Warm encodes reuse
`build/examples/*/_cache`. Packs depend on the builder executable; after changing
shader/asset sources, delete the relevant `.ntpack` before visual QA to force a
repack. Generated `examples/*/generated/*.h` are deterministic tracked output;
commit refreshed copies when a pack build reveals they were stale.

## Build options

Configure the engine with `-D` cache values, not just preprocessor defines on the
final executable. Numeric feature definitions propagate through module targets.
[CMakeLists.txt](../CMakeLists.txt) defines defaults and
[CMakePresets.json](../CMakePresets.json) defines preset overrides. This table
covers the common options; module specs own detailed ON/OFF behavior.

| Option | Plain CMake default | Preset/usage notes |
|---|---|---|
| `NT_STATIC_CRT` | ON | Static release CRT on Windows; OFF inherits the embedding application's CRT. |
| `NT_BUILD_TESTS` | ON | `native-release` OFF; `native-release-test` ON. |
| `NT_ASSERT_MODE` | Automatic | Debug FULL (2), Release TRAP (1); release-test FULL. OFF (0) is unsupported. |
| `NT_LOG_MIN_LEVEL` | 0 (INFO) | Debug/release-test 0; production Release 1 (WARN). Also 2 ERROR, 3 NONE. |
| `NT_RESOURCE_TIMING_ENABLED` | OFF | Debug/release-test ON; production Release OFF. |
| `NT_UI_TIMING_ENABLED` | OFF | Debug/release-test ON; production Release OFF. |
| `NT_GFX_GPU_TIMING_ENABLED` | OFF | Debug/release-test ON; production Release OFF. |
| `NT_UI_DEBUG_TOOLS` | OFF | Debug/release-test ON; production Release OFF. |
| `NT_LOG_RING_ENABLED`, `NT_METRICS_ENABLED`, `NT_INTROSPECT_ENABLED` | Follow `NT_UI_DEBUG_TOOLS` | Debug/release-test ON; production Release OFF. |
| `NT_INTROSPECT_WRITE_ENABLED` | Follows `NT_INTROSPECT_ENABLED` | Debug/release-test ON; production Release OFF. |
| `NT_HYBRID_HPG` | ON | Windows hybrid-GPU preference hint; per-app Windows graphics preferences override it. |
| `NT_FONT_EMBOLDEN_ENABLED` | OFF | Explicit opt-in, including Debug. |
| `NT_UI_CLAY_DEBUG_VIEW` | OFF | Explicit opt-in, independent of the Neotolis inspector. |
| `NT_DEVAPI_ENABLED` | OFF | Group switches are dormant while the master gate is OFF. |
| `NT_SKIP_EXAMPLE_PACKS` | Empty | Example pack generation to skip, such as `sponza`. |

Contracts and less common options:

- [Log/assert policy, measurement producers and DevAPI group options](spec/debug/logging-errors-debugging.md).
- [Resource measurements and resident bytes](spec/assets/resource.md#optional-measurements-and-resident-bytes).
- [Font synthesis and rich markup](spec/ui/rich-text.md), [Clay debug view](spec/ui/nt-ui.md).
- [WASM build variants](../README.md#wasm-requires-emsdk-activated).

### CRT, probes and profiling

All Windows CRT pinning goes through `nt_set_static_crt(_cxx)`, never raw `-U_DLL`;
`scripts/check_crt_pins.sh` enforces this. Set `NT_STATIC_CRT=OFF` when embedding
builder/runtime into an executable that selects `CMAKE_MSVC_RUNTIME_LIBRARY`.
Set `NT_HYBRID_HPG=OFF` for battery-friendly games/tools; ON exports the NVIDIA/AMD
preference symbols from the executable, without per-frame engine work.

`tests/` supplies `NT_TEST_ACCESS` to the libraries used by native tests. Disabling
`NT_BUILD_TESTS` removes probe state/bookkeeping from production measurements and
the release builder. Native unit tests are excluded under Emscripten; on WASM,
`NT_BUILD_TESTS` gates the registered test targets. `native-release-test` compiles
test translation units under NDEBUG; the production `native-release` does not.

Measure performance in Release with the required timing flag explicitly ON.
Timing producers do not require metrics. `NT_LOG_MIN_LEVEL=3` uses the existing
stub source through `nt_log`; `nt_log_stub` remains a separate link-time choice.
Atlas benchmark scripts (`benchmark.sh`, `autoresearch-bench.sh`, `bench-vector.sh`
in `scripts/atlas/`) select INFO in `build/_cmake/native-release-atlas-bench`;
`--no-build` uses that build's executable.

## Checks

Run these from the repository root, with the SDK activated for WASM checks:

```bash
bash scripts/format_and_check.sh        # after edits, before committing
bash scripts/check.sh --push            # before pushing
bash scripts/check.sh --full            # whole-tree format/tidy sweep
```

`format_and_check.sh` runs the formatter under the same lock as the check;
`check.sh` alone is read-only. Modes pass through, so
`bash scripts/format_and_check.sh --push` combines formatting and the push gate.
Do not run competing check/ctest processes in one tree: shared outputs and
relinked executables cause false failures. `build/.check.lock` rejects a second
check with exit 2; remove it only after confirming the owner is dead.

The default gate checks module composition, EM_JS_DEPS, doc links/spec-index
coverage, CRT pins and test registration; builds native-debug; runs ctest; and
checks changed files with clang-format/clang-tidy. Changed headers trigger full
tidy. The three atlas benchmark guards run when builder/atlas paths change, or
always with `--push`/`--full`. Warm checks are much faster than the initial pack
encode. Vendored dependencies follow upstream style and are excluded from
format/tidy; review changes to them separately.

Tidy uses the DevAPI-enabled compile database in `build/_cmake/tidy-ci`, matching
CI. Before pushing new test/tool C files, run
`clang-tidy -p build/_cmake/tidy-ci <file>` directly; new files can otherwise be
missed. Rebuild native-debug after adding a C source to refresh its compile DB;
new-file and platform-`#if` clangd diagnostics need confirmation by a real build.

The push gate additionally builds native-release, wasm-debug and wasm-release,
then runs submodule consumption and the diagnostics matrices. These catch
NDEBUG-only warnings, Emscripten-specific warnings and Closure failures that the
native Debug build cannot catch. The full mode checks whole-tree format/tidy.

### Optional-feature checks

```bash
python scripts/check_diagnostics_config.py
python scripts/check_diagnostics_runtime.py
```

Run the scripts serially; they use separate build directories and also run in
`check.sh --push`. The runtime matrix covers log/rich-parser consumers at every
log floor with FULL asserts, TRAP positive paths, timing producers ON/OFF,
metrics independence and inspector ON with UI timing OFF.

Browser diagnostics use `tests/browser/diagnostics.spec.ts`. Set
`NT_SHOWCASE_DIR` to the exact build, distinct `NT_SHOWCASE_PORT`/`NT_DEVAPI_PORT`,
`CI=1` to forbid server reuse, and matching
`NT_DIAGNOSTICS_PRESET/LOG/UI/GPU/METRICS`. Debug and Release use timer values
above 32 bits to exercise the 64-bit bridge. General browser smoke tests drive
`tests/browser/app` (`window.__nt` hooks), not the showcase.

Verify font geometry changes with `NT_FONT_EMBOLDEN_ENABLED` OFF and ON. The ON
mirror includes `test_font`, `test_text_renderer`, `test_nt_ui_label` and all
`test_nt_ui_rich_*` suites. CI also exercises synthesis and the Clay debug view
under NDEBUG and compiles them with emcc in Release. To compare Clay arena sizes:

```bash
python scripts/check_clay_arena.py <off-build-dir> <on-build-dir>
```

Build `test_nt_ui_clay_debug_view` in both directories first, with identical
capacities. Runtime behavior of disabled synthesis/debug-view features remains
specified in the UI chapters linked above.

## Troubleshooting

### Tests and runtime evidence

- Read `build/_cmake/native-debug/check-ctest.log` or
  `Testing/Temporary/LastTestsFailed.log`; a truncated terminal tail is insufficient.
- `UNITY_EXCLUDE_FLOAT` is defined: Unity float assertions compile to nothing.
  Use meaningful comparisons, such as integer casts for small exact values.
- A failing `NT_BUILD_ASSERT` aborts the process. Run deliberate assert-trip tests
  directly. If a dead test still locks its executable on Windows, use
  `taskkill //F //IM <test>.exe` from Git Bash before relinking.
- For a silent crash, use an available debugger; with LLDB:
  `lldb -b -o run -o bt <exe>`. The MSVC CRT buffers stdout to pipes and ignores
  `stdbuf`; debugger availability is machine-specific.
- Builder behavioral probes need a CMake target with the builder's PUBLIC
  dependencies. Ad-hoc linking `nt_builder.lib` misses glad/cgltf dependencies.
- For pixel-exact GL checks use DevAPI `capture.frame` (`glReadPixels`, also
  works headless), with `NT_DEVAPI_ENABLED` and the CAPTURE group enabled.
  GDI/PrintWindow capture may not capture a GL framebuffer. For aesthetics/layout
  that cannot be verified locally, ask the developer to inspect specific results.

### CI and platform differences

- **GNU ld:** archives resolve left-to-right. Providers/stubs must follow
  consumers (`nt_resource ... nt_http_stub nt_fs_stub nt_log_stub`); FS is
  native-only. A standalone math test may need conditional linking to `m` on
  non-Windows platforms. See `tests/submodule/CMakeLists.txt`.
- **Format version skew:** use single-space trailing comments; CI clang-format
  can reject multi-space alignment accepted by another local version.
- **SDK pin skew:** compare `emcc --version` with `.emsdk-version` before trusting
  a local Closure build.
- **Platform branches:** Windows tidy does not check Linux-only `#if` blocks.
  Review them as Linux code; use a justified local `NOLINT` where needed.
- **Headless smoke:** skip `glfwInit` when neither `DISPLAY` nor `WAYLAND_DISPLAY`
  exists in the Linux headless environment.
- **ASan/LeakSanitizer:** an assert test that longjmps past live heap allocations
  leaks even if Unity prints OK. Assert-trip paths must use stack/preallocated
  buffers, not heap allocations abandoned by the jump.
- **Shared display:** CI ctest stays serial because real-GL tests share xvfb.
  Local parallel runs use the desktop display; GL tests have `RESOURCE_LOCK
  gl_display` in `cmake/test_target.cmake`.
- **Assert define collision:** CI Release supplies a global `NT_ASSERT_MODE`.
  For tests requiring another mode, use a wrapper TU with `#undef`/`#define`,
  following `tests/unit/test_helpers/nt_atlas_assert_off_tu.c`, not a conflicting
  target `-D` that trips `-Wmacro-redefined`.
- **Release test warnings:** production `--push` does not compile test TUs under
  NDEBUG; `native-release-test` in CI may expose their unused variables.
- **Windows spawn exhaustion:** random Emscripten subprocess failures with
  `3221225794` (`0xC0000142`) can be transient during parallel links. Retry once
  before investigating further.
