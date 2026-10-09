# Methane Asteroids — Neotolis port

A Neotolis C17 port of [Methane Asteroids by Evgeny Gorodetskiy](https://github.com/MethanePowered/MethaneAsteroids),
revision `16a5751e835dd0776d976e51438604dc8de27d16`. The scene and benchmark data
were preserved first. The default control panel now uses existing native Neotolis
UI components; `--ui 0` retains the source-style baseline presentation.
See [CREDITS.md](CREDITS.md) for source history, authors and licenses.

Galaxy panorama: ESO/S. Brunier, CC BY 4.0; adapted as a cubemap in Methane Asteroids.
[Original panorama](https://www.eso.org/public/images/eso0932a/) ·
[ESO image-use terms](https://www.eso.org/public/outreach/copyright/).
This supplemental attribution is supported by reproducible image comparison,
not by an explicit photographer credit in the upstream demo. Evgeny Gorodetskiy
is the original demo author under Apache 2.0.

## Preserved workload

| Complexity | Asteroids | Unique meshes | Noise textures |
|---:|---:|---:|---:|
| 0 | 1,000 | 35 | 10 |
| 1 | 2,000 | 50 | 10 |
| 2 | 3,000 | 75 | 20 |
| 3 | 4,000 | 100 | 20 |
| 4 | 5,000 | 200 | 30 |
| 5 | 10,000 | 300 | 30 |
| 6 | 15,000 | 400 | 40 |
| 7 | 20,000 | 500 | 40 |
| 8 | 35,000 | 750 | 50 |
| 9 | 50,000 | 1,000 | 50 |

Every unique mesh has the original four independently generated LODs. Every
noise texture retains three 256×256 layers. The baseline submits **one draw per
asteroid**, with no frustum culling or batch merging. Packing mesh variants into
bounded GLB chunks changes storage, not geometry, draw ranges or draw count.

Original camera, orbit/scale distributions, palettes, light, spin/orbit animation,
LOD equation, planet geometry and Mars/Galaxy images are retained. Raw sRGB Mars
sampling and reversed-Z GEQUAL are explicit minimal graphics prerequisites.
The source engine's DirectX/Metal/Vulkan backends are not imported.

## Build and run

See [build instructions](../../docs/build.md#asteroids-baseline). The first native
build compiles a source-only C++20 exporter and uses Python3 for validation, then the existing Neotolis builder
emits 17 scene NTPACKs plus a 17.7 KiB UI NTPACK (about 340 MiB total); C17 runtime code has no C++ dependency.
The exact SSE2 exporter requires an x86 build host. On ARM, first copy the 17
packs produced on x86 into `build/examples/asteroids`; native runtime and WASM
compilation do not need the exporter. Configuration reports this explicitly.
Only core, space, selected complexity and selected noise packs are mounted.
Galaxy faces remain 2048×2048, and Mars remains 2048×1024.

Run from `build/examples/asteroids/native-debug` so relative asset paths resolve:

```sh
./asteroids                         # original Debug default: complexity 1
./asteroids --complexity 8          # official screenshot's 35,000-object level
./asteroids --complexity 9          # explicit maximum stress level
./asteroids --complexity 8 --paused 1
./asteroids --complexity 8 --ui 0    # source-style baseline HUD
./asteroids --complexity 8 --frames 300 --ui 0 --hide-hud 1
```

`-c` aliases `--complexity`. `--ui 0` does not initialize or mount optional UI
resources; `--ui 1` is the default. Hiding the HUD preserves scene-only captures. `--frames` is Neotolis's fixed-step native diagnostic
harness: 60 warmup frames, measured CPU frame time/counters, final framebuffer
checksum, then exit. `--paused 1` and hidden HUD make image comparisons repeatable.
This harness is additional instrumentation, not an upstream benchmark mode.

## Optimized rendering

Reference rendering remains the default (`--optimized 0`). Enable **Optimized
rendering** in Settings, or start with `--optimized 1`. The mode can change while
paused, so both paths can render the same camera, animation time and scene.

The optimized path uses the pinned engine's existing typed radix sort,
per-frame vertex storage, indexed instancing and state cache. It sorts opaque
asteroids by their exact texture set and selected geometry subset, packs their
unchanged instance attributes in that order, and draws each compatible run once.
Texture bindings are applied at texture-set transitions. Different meshes, LODs
or textures never share an instanced draw. Per-object colors remain per-instance.

There is no frustum culling, object-count reduction, lower-resolution texture,
different geometry, changed LOD threshold or shader simplification. Both modes
update and submit every source asteroid. Planet and sky rendering are unchanged.
All source scene packs stay byte-identical. Sorting changes opaque draw order;
pixel comparison is part of validation, rather than an assumed consequence of
matching triangle totals.

The current OSMesa check found small framebuffer differences from opaque draw
ordering: 16–62 of 256,000 pixels across six 640×400 cases (54 pixels at the
initial 50,000-object scene). The strict pixel-equality check therefore fails for
Optimized; it is not claimed pixel-identical. Reference matches the preserved
original byte-for-byte in all six cases, including after switching back.
In three diagnostic cases, retained depth buffers matched bit-for-bit and
sorted, unbatched draws produced exactly the instanced image. This isolates the
change to draw ordering/equal-depth winners under GEQUAL in the confirmed
24-bit software depth buffer. Keep Reference when original tie ordering matters.

With 50,000 asteroids at the source camera and time zero, the actual scene-only
native draw count was 50,002 → 40,582 (18.8% fewer), and issued GL calls were
674,575 → 284,810 (57.8% fewer). All 50,000 asteroids and 3,346,120 asteroid
triangles remained; scene vertex uploads stayed at 4,000,080 bytes. These are
single-thread llvmpipe correctness/counter results with a hidden HUD, not a
hardware timing benchmark.

The added work is a bounded sort plus an instance-data copy each optimized
frame. Preallocated CPU storage totals 5,600,000 bytes (5.34 MiB): 4 MB for
instance staging and two 800 KB sort buffers. The sorter uses an 8 KB stack
histogram. No heap allocation occurs in this path, and the scene's GPU upload
remains 80 bytes per asteroid plus the planet. Reference mode skips the sort
and copy. The built-in generic mesh renderer has a different instance ABI
(world transform plus packed color) and draws whole meshes; this sample retains
its specialized 80-byte material attributes and chunk subset ranges through
the existing graphics primitives instead of adding a new engine API.

For same-scene comparisons, keep complexity, resolution, camera, animation time,
LOD controls and HUD visibility identical. DC counts recorded draws; GL includes
issued state, upload and query calls. CPU is callback wall time through submission
and GPU is delayed scene-only timing. A reduction in command counts alone is not
a measured FPS or hardware-GPU speedup. Use the engine's
[phone measurement protocol](../../docs/perf-measurement.md) for sustained mobile
performance; software OSMesa results cannot establish that result.

## Native control panel

Settings, Statistics and Help are built with `nt_ui`, existing sliders, toggles,
buttons, tabs, scroll containers and the text/sprite renderers. There is no HTML
UI overlay. The 3D viewport and source camera remain full-frame.

The compact top strip keeps the scene visible; Settings opens the initially
collapsed controls. It uses one row on wide screens and two on narrow screens.
The complexity slider previews a preset; Apply is the only UI action that loads
it. Pause, LOD colors, the source LOD threshold and reset controls are immediate.
CPU frame time, delayed GPU scene time, DC and GL counts remain visible in the
top bar, including when the controls are collapsed and on narrow screens. The
Statistics tab opens with timings and command counts, including actual GL draw
calls, and identifies each measurement's scope and unknown/pending state.
Help contains controls, source authorship, license references and the Galaxy
provenance caveat. The panel scrolls; small screens use a collapsible native modal.

F3 collapses/reopens controls; F4 hides all overlays. Scene-origin and UI-origin
pointer gestures remain separately owned through release. Browser shortcuts are
reserved only on the focused example canvas where the browser permits it; the
native buttons provide alternatives. Browser runtime behavior is still unverified.

## Controls

- 0–9 or `[` / `]`: complexity
- Ctrl+P: pause animation; L: source LOD colors
- WASD / Page Up / Page Down: camera translation; arrow keys: camera rotation
- Left drag: camera rotation; middle drag: pan; right drag: light rotation
- Wheel or `-` / `=`: zoom; `,` / `.`: roll
- Touch (native UI mode): one finger orbits; two-finger pinch zooms, including while paused
- Settings offers pause, complexity, LOD, reset view/light, and the render-path toggle
- Alt+P: camera pivot; Alt+R: reset camera; Ctrl+L: reset light
- `;` / `'`: double / halve minimum screen size for LOD
- F1: help; F2: supported command-line options; F3: controls/parameters; F4: HUD
- Ctrl+F: fullscreen; Escape or Ctrl+Q: exit native

P reports that parallel command-list recording is unavailable in this backend.
Typed punctuation depends on keyboard layout; it is not an exact physical-held-key
translation. Source device/swapchain controls and `--subdiv-count`,
`--texture-array`, `--parallel-render` options are not implemented.

Touch gestures use the engine's existing unified pointers. Only scene-owned
fingers participate; a drag beginning on UI stays excluded until release.
Adding/removing a finger rebases the gesture without a camera jump. Three or more
scene fingers suspend camera gestures until one or two remain. Pinch distance is
clamped to the same 60–400 range as mouse zoom; two-finger pan/roll are not mapped.
The original `--ui 0` path retains its source-style pointer behavior. Native
input-sequence tests cover these rules; actual phone/WebGL runtime is unverified.

## Measurements and compatibility

The HUD reports frame interval, CPU callback duration, asynchronous **scene-only**
GPU time, previous-frame gfx draw calls, source workload and LOD counts. The
native UI also sums the existing per-call GL counters: DC is recorded gfx draws,
while GL is actual issued backend calls, including binds, uploads and queries.
Both counts include the HUD and come from the same completed frame. CPU time is
callback wall time through end-frame, not process CPU utilization or GPU time. Native
memory is process RSS; web memory is allocator in-use bytes. These are different
quantities. GPU timing compiled out displays `disabled` in the native UI; an
unsupported timer displays `unavailable`, and an initial query displays `pending`.
Fixed-step runs display
memory `not sampled`. Neither value is estimated from unrelated counters.

A cloud OSMesa software-OpenGL capture verifies the actual rendered scene; it is
not a hardware/mobile performance measurement. Debug sanitizers materially affect
CPU and memory results. Always report renderer/device, build, resolution,
complexity, timing scope and diagnostic options beside any measured result.

[PARITY.md](PARITY.md) records exact source mappings and unavoidable backend
representations. In particular serial source RNG ordering, serial GL submission,
six 2D sky samplers instead of seamless cubemap sampling, default GL depth
precision and buffer-binding layouts prevent a claim of performance equivalence
to Methane's native RHI backends.

## Data generation and verification

Vendored, pinned FastNoise2/FastSIMD/HLSL++ sources are used only by the offline
exporter. Normal builds do not download them. The source algorithms, original
method-body oracles, source hashes and full licenses live in
[reference_generator](reference_generator/README.md) and
[reference_scene_data](reference_scene_data/README.md).

Validation checks exact geometry float bits, indices and depth ranges; all
145,000 instances across ten levels; all procedural texture layers; repeated
exports; and 48 runtime world/LOD fixtures. Runtime tests also pin the source
camera/depth convention. Generated intermediate GLBs and raw records stay in the
build directory; the existing builder produces native/WebGL-compatible packs.
