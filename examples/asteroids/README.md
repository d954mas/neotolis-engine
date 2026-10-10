# Methane Asteroids — Neotolis port

A Neotolis C17/WebGL 2 port of [Methane Asteroids by Evgeny Gorodetskiy](https://github.com/MethanePowered/MethaneAsteroids)
(Apache 2.0), revision `16a5751e835dd0776d976e51438604dc8de27d16`. It is a
rendering benchmark: up to 50,000 individually animated asteroids around Mars,
each drawn with one of up to 1,000 procedural shapes in four LODs and one of up
to 50 procedural noise textures. See [CREDITS.md](CREDITS.md) for authors and licenses.

Galaxy panorama: ESO/S. Brunier, CC BY 4.0; adapted as a cubemap in Methane Asteroids.
[Original panorama](https://www.eso.org/public/images/eso0932a/) ·
[ESO image-use terms](https://www.eso.org/public/outreach/copyright/).

## Workload

| Complexity | Asteroids | Unique shapes | Noise textures |
|---:|---:|---:|---:|
| 0 | 1,000 | 35 | 10 |
| 1 (default) | 2,000 | 50 | 10 |
| 2 | 3,000 | 75 | 20 |
| 3 | 4,000 | 100 | 20 |
| 4 | 5,000 | 200 | 30 |
| 5 | 10,000 | 300 | 30 |
| 6 | 15,000 | 400 | 40 |
| 7 | 20,000 | 500 | 40 |
| 8 | 35,000 | 750 | 50 |
| 9 | 50,000 | 1,000 | 50 |

Each asteroid picks a shape and a texture at random and has its own transform,
spin, orbit and color pair. The camera, light, LOD equation, ring
distributions, palettes and shaders follow the original.

## Procedural content

As in the original, the content is generated at startup, not loaded. Shapes and
textures depend only on their index, so all 1,000 shapes and 50 textures of the
largest level are generated once; a complexity change only regenerates the
instances (about 9 ms for 50,000).

- **Shapes (CPU):** a subdivided icosahedron displaced by 4-octave simplex fBm to radii
  0.525–0.9. Subdivision only appends vertices, so the four LODs of one shape share
  one noise evaluation and keep the same silhouette. All shapes live in one
  vertex/index buffer (snorm16 positions, snorm8 normals, 12 bytes per vertex).
- **Textures (GPU):** one pass renders all 50 noise textures as tiles of an RGBA8
  atlas (16-bit fBm value in R and G). After the frame the atlas is read back,
  each 256 × 256 tile is normalized to its own range, and the result is uploaded
  as one 2048 × 2048 R8 atlas with mipmaps, which a render target cannot hold.
  Tiles are power-of-two aligned, so mip reduction never mixes tiles; the rock
  shader clamps half a mip texel inside its tile and samples it triplanar.
- **Instances:** PCG32 from seed 1123, using the original distributions.

Generation blocks one frame after a notice is shown; Statistics reports its time.
On a desktop PC (native Release) it takes about 80 ms: shapes about 35 ms, buffer
upload about 15 ms, textures about 25 ms (about 390 ms when they were generated on
the CPU). The shape noise runs four vertices at a time with clang/gcc vector
extensions on SSE2, NEON and wasm simd128 (the paired web build's SIMD binary);
other builds use the identical scalar code, about 145 ms for the shapes.

In desktop Chrome (paired release build, same PC) startup generation takes about
220 ms with the SIMD binary and about 540 ms with the baseline one: shapes about
95–125 ms (SIMD) or 290–400 ms (baseline), buffer upload about 30 ms, and the
single atlas readback plus R8 uploads about 65–95 ms. Phone startup is unmeasured. The
packs contain only shaders, the font, the UI atlas, Mars and the six sky faces,
all Basis ETC1S (about 3.4 MiB).

## Rendering

Every asteroid is updated and submitted each frame, with no culling. The noise
texture is an atlas tile chosen per instance, so draw items are radix-sorted by
(LOD, shape) only; each run of equal keys becomes one indexed instanced draw.
Instances go to their own frame vertex stream.

WebGL2 has no base instance, so an instanced draw is located by pointing the
instance attributes at its run's offset. Each key therefore has its own vertex
input (as a game has one per mesh) over the shared rock buffers, and a fixed
slot range of the stream with a quarter spare plus one slot: an asteroid that
crosses an LOD threshold changes one count without moving the other runs, so
each vertex input keeps its pointers from frame to frame. The layout is rebuilt
only when a key outgrows its range, about once per 60–120 frames at complexity 9.

The per-frame update builds each instance's rows in closed form (spin, scale,
ring translation and the orbit about +Y), picks the LOD with three distance
compares instead of logarithms, and computes the spin and orbit sin/cos four
asteroids at a time on SIMD builds.

Measured at complexity 9 (50,000 asteroids) on one desktop PC with Intel UHD
and RTX 4080 Laptop GPUs (which GPU each run used was not checked), ABBA runs:

| | Start | Now |
|---|---:|---:|
| Draws / GL calls per frame | 40,000 / 242,000 | 3,000 / 6,100 |
| Native Release frame (CPU) | 11–14 ms | about 6 ms |
| Chrome render submission (SIMD build) | not measured | 1.3–2 ms |
| Chrome CPU per frame (SIMD build) | about 26 ms (single vertex input) | 3.6–5 ms |

Per-key vertex inputs trade the attribute re-pointing for a vertex array switch
per draw. In Chrome that cut submission from 22–25 ms to under 2 ms; natively
the switch costs more than re-pointing (submission 0.8 → 4.7 ms). WebGL is the
target, so the demo keeps per-key inputs.

On the reference phone (Huawei P40, Mali-G76, Chromium 156, vsync off, warm,
ABBA per [measuring performance on phones](../../docs/perf-measurement.md)),
complexity 9 went from 1.8–2.3 FPS (p95 616–735 ms) to 18.8–24.2 FPS (p95
100–122 ms), 0.41 → 3.9–4.7 FPS per 100 GPU MHz. The phone is limited by
Chrome's GPU-process main thread (`CrGpuMain`) executing the ~3,000 draws: it is
~99% busy, the page thread waits on it for over half the frame, and without the
rock draws the same page runs at ~97 FPS. The instance update (~11 ms) and the
5 MB instance upload are secondary.

## Build and run

See [build instructions](../../docs/build.md#asteroids). Run from the output
directory so relative asset paths resolve:

```sh
./asteroids                         # complexity 1
./asteroids --complexity 9          # 50,000 asteroids
./asteroids --complexity 8 --paused 1
./asteroids --complexity 8 --frames 300 --hide-hud 1
```

`-c` aliases `--complexity`. `--frames` is the native fixed-step diagnostic
harness: 60 warmup frames, mean frame time and counters, a framebuffer checksum,
then exit.

## Controls

- 0–9 or `[` / `]`: complexity
- Ctrl+P: pause animation; L: LOD colors
- V: 10-second fly-through for recording (complexity 1 to 9, pull back, dive into the ring)
- WASD / Page Up / Page Down: move the camera; arrow keys: rotate it
- Left drag: orbit; middle drag: pan; right drag: rotate the light
- Wheel or `-` / `=`: zoom; `,` / `.`: roll
- Touch: one finger orbits; two-finger pinch zooms
- Alt+P: camera pivot; Alt+R: reset camera; Ctrl+L: reset light
- F1/F2: help; F3: controls panel; F4: hide all overlays
- Ctrl+F: fullscreen (native); Escape or Ctrl+Q: exit (native)
- Web: the page's Fullscreen button

Settings, Statistics and Help are built with `nt_ui`. A drag that starts on the
UI stays with the UI until release. Typed punctuation depends on the keyboard
layout.

## Measurements

The top bar shows FPS, CPU time, delayed scene-only GPU time, recorded draws (DC)
and issued GL calls (GL). DC and GL are previous-frame totals including the UI.
CPU is callback wall time through end-frame. Native memory is process RSS; web
memory is allocator bytes in use. For phone comparisons follow
[measuring performance on phones](../../docs/perf-measurement.md).
