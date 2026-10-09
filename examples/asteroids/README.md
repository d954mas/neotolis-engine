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
  atlas (16-bit fBm value in R and G). After the frame the tiles are read back,
  normalized to their own range and uploaded as 256 × 256 R8 with mipmaps, which
  a render target cannot hold. The rock shader samples them triplanar.
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
single atlas readback plus R8 uploads about 65–95 ms. Phones are unmeasured. The
packs contain only shaders, the font, the UI atlas, Mars and the six sky faces,
all Basis ETC1S (about 3.4 MiB).

## Rendering

Every asteroid is updated and submitted each frame, with no culling. Draw items
are radix-sorted by (texture, LOD, shape); each run of equal keys becomes one
indexed instanced draw, and the noise texture is bound only on a texture change.
Instances go to their own frame vertex stream. With 50 textures × 1,000 shapes ×
4 LODs, most runs hold one asteroid, so complexity 9 issues about 40,000 draws:
the scene is a draw-call benchmark.

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
- WASD / Page Up / Page Down: move the camera; arrow keys: rotate it
- Left drag: orbit; middle drag: pan; right drag: rotate the light
- Wheel or `-` / `=`: zoom; `,` / `.`: roll
- Touch: one finger orbits; two-finger pinch zooms
- Alt+P: camera pivot; Alt+R: reset camera; Ctrl+L: reset light
- `;` / `'`: double / halve the minimum screen size for LOD
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
