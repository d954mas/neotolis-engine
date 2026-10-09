# Baseline parity record

Reference: Methane Asteroids `16a5751e835dd0776d976e51438604dc8de27d16`.
Source paths below are in that revision, unless a dependency is named.

## Scene and workload mapping

- `App/AsteroidsApp.cpp` / `Modules/Simulation/AsteroidsArray.h`: ten source complexities, instances,
  unique meshes, texture counts; original Debug default 1. Runtime `s_complexities`.
- `Modules/Simulation/Asteroid.cpp` and `AsteroidsArray.cpp`, MethaneKit `IcosahedronMesh`/`BaseMesh`: seed 1123, four
  independent source subdivisions, FastNoise deformation and exact accumulated
  normals/depth bounds. Offline `reference_generator` uses source method oracles.
- `AsteroidsArray.cpp`: scale/distribution, palettes, texture/mesh indices and
  spin/orbit state. `reference_scene_data` preserves source distribution calls.
- `Asteroid.cpp`: elapsed radians = float(pi × seconds); row-major
  spin × scale/translate × orbit; original screen-size LOD equation. Runtime
  `asteroid_world_matrix` and `asteroid_lod_index`, tested against 48 source fixtures.
- Source camera eye (-110,75,210), aim (0,-60,25), up Y, FOV 90°, left-handed
  row-vector projection; reversed source ZO near parameter600/far0.01. Runtime
  converts clip Z to GL's [-w,+w], clears depth0 and compares GEQUAL.
- Source light (-100,120,0), power3, ambient0.05, specular30; no pixel-stage
  renormalization of interpolated normals. Source precision2 gamma approximations
  retained in translated GLSL. Planet radius45, spin -0.1 × seconds.
- MethaneKit `SphereMesh(1,32,32)` and `CubeMesh`: exact planet/sky topology.
- Original Mars image samples as sRGB; Galaxy and noise remain linear RGBA8.
  All 50 maps ×3 layers retained, including identical layer contents.
- One draw per asteroid; no CPU culling, no batching, no variant count reduction.
  Chunked mesh storage respects Neotolis's existing builder limits.

## Representation differences (not hidden parity claims)

1. Upstream parallel tasks share RNG state, so scheduling does not define stable
   mesh-to-random-value ordering. Export uses the same operations serially, pinned
   Clang19/libstdc++14 and SSE2 relaxed FastNoise. It is deterministic and oracle
   checked, but does not reproduce a particular multithreaded screenshot's order.
2. Update and GL submission are serial. Neotolis has no parallel command-list
   recording, device selection or source swapchain controls. CPU/GPU results are
   not equivalent benchmark comparisons against source DirectX/Metal/Vulkan.
3. Eighty-byte per-draw instance attributes replace source constant-buffer layout;
   three 2D texture bindings replace the source array/descriptor representation.
   The source object count, texture data and per-object draw count are retained.
4. Six 2D sky samplers perform cubemap direction lookup; filtering across cube
   edges is not seamless. No source image resampling/downsampling is used.
5. Source noise ClampToZero is emulated analytically with nearest-mip bilinear
   sampling and edge coverage. The selected filtering reflects the pinned
   DirectX/Vulkan backend mapping of MinMagLinear/NotMipmapped. Mip generation and
   derivatives can differ across graphics implementations.
6. GL clip conversion preserves mathematical reversed-Z mapping, but the default
   window depth format may differ from source D32 precision.
7. Typed punctuation and F4 HUD toggle differ from source physical-held keys and
   title-bar HUD modes. The source-style text HUD remains under `--ui 0`; the default `--ui 1` adds
   the requested native Neotolis control panel without changing scene algorithms.
8. DejaVu Sans Mono replaces source font assets with retained full license.
9. Source release auto-complexity selection is not reproduced; use explicit
   `--complexity`. Optional source subdivision/texture-array/parallel CLI switches
   are not present. Unsupported paths are reported, never silently emulated as
   the original backend.

## Verification status

- Passed: literal geometry and scene-data oracles, repeated exports, all ten level
  records, all 48 source runtime matrix/LOD fixtures, source camera/depth tests.
- Passed: native Debug source scene capture at complexity8, actual 35,000 objects
  and 35,003 frame draws with HUD. Captured under OSMesa software GL, not hardware.
- Passed: focused core/builder tests; actual GL GEQUAL and raw-sRGB upload, update,
  filtering and mip tests (ASTC format test skipped on unsupported software GL).
- Passed: max-complexity repeated 1→9→0→8→1→9→0 transitions; all ten native GL
  suites under OSMesa (132 tests, zero failures, one unsupported ASTC skip).
- Passed: stage-two native control clicks, staged complexity then Apply to 50,000,
  Statistics/Help tabs; scene-only `--ui 0` pixels and all 17 scene packs remain
  byte-identical to the preserved baseline. The same exact pixel comparison also
  passed with native UI initialized and hidden (`--ui 1 --hide-hud 1`).
- Passed: 15 scene/control/telemetry unit cases, including modal/zero-size input
  interruptions and keyboard resets during active drags; native Settings/Help at
  390×844 and an 80-pixel-wide stress layout. These are resized native windows,
  not phone-device tests. UI frame storage peaked at 4,166,292 vertex bytes and
  21,792 index bytes in the 50,000-object control replay, within configured limits.
- Passed: final native Debug/Release and WASM Debug/Release builds. Closure remains
  enabled; the example explicitly includes a missing fullscreen helper dependency
  in the pinned Emscripten 4.0.19 SDK. Missing UI packs fail WASM builds and are
  regenerated by native builds.
- Unverified: actual WebGL browser runtime/context restoration and hardware/mobile
  performance. Browser launch is blocked by this executor's socket restrictions.
- Full aggregate status is not green: eight existing Linux lint diagnostics in
  unchanged files, plus unavailable native window/clipboard tests. Ten GL suites
  passed through the supported NULL-platform/OSMesa path; the atlas fixture test
  passed after fetching its original Git LFS bytes. The existing font PIN_BLOB
  teardown diagnostic remains visible; no logging is suppressed.
- Integration base is master `b9efb7b3a44b70436727bab6e381dfae5b17581f`.
  Builds and renders used its predecessor `8d6233866ebdd4c6053bf79b82e13c64373507d6`;
  the sole upstream change is performance documentation. Patch application and
  byte identity of every delivered code file were checked in a separate current-
  master worktree. No commits, pushes, PRs or deployments were performed.
