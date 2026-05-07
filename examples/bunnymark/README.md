# Bunnymark Demo

CPU sprite-batching stress test for Neotolis Engine. The primary reference is
[britzl/defold-bunnymark](https://github.com/britzl/defold-bunnymark),
specifically `bunnymark/update_native_position_velocity`.

## Test Conditions (DEMO-07)

| Parameter      | Value                                                            |
| -------------- | ---------------------------------------------------------------- |
| Viewport       | 800 x 600 (default; resizable)                                   |
| Sprite size    | ~26 x 37 px on screen (SD or HD)                                 |
| Blend mode     | Premultiplied alpha (D-24)                                       |
| Depth test     | Off (D-24)                                                       |
| Atlas pages    | 1                                                                |
| Vertex format  | float3 pos + float2 uv + uint8[4] color = 24 B (SPRITE-05)       |
| Pixels per unit | 1.0 (SD) / 17.0 (HD) — runtime ipu = 1/ppu bake (D-32)           |
| Browser / GPU  | Logged at startup (`gpu=unknown` until engine exposes caps)      |
| Bunny pool cap | 60000 (uint16_t entity/component storage bound)                  |

The startup conditions log line follows the schema:

```
Bunnymark conditions: viewport=WxH sprite_size=~26x37 px blend=premultiplied atlas=SD|HD pages=1 initial=I click=C hold_rate=R bunny_max=M hd_available=0|1 gpu=...
```

## Controls

| Action                              | Input                                              |
| ----------------------------------- | -------------------------------------------------- |
| Spawn 500 bunnies                   | Click / tap canvas                                 |
| Spawn 50/frame at cursor (hold)     | Hold left mouse / hold tap                         |
| +100 / -100 bunnies (random pos)    | Arrow Up / Arrow Down                              |
| +1000 / -1000 bunnies               | Shift + Arrow Up / Shift + Arrow Down              |
| Toggle SD <-> HD                    | `H` key OR top-right "Quality" tap zone (~120x40)  |
| Toggle GPU timing queries           | `T` key                                            |
| Quit                                | `Esc` (native only — web ignores)                  |

**Note on bulk add/remove keys:** the canonical Bunnymark uses `+` / `-`, but
the engine input enum currently does not expose `NT_KEY_PLUS` / `NT_KEY_MINUS`.
Arrow Up / Arrow Down were substituted in Phase 50 Plan 06 (Rule 3 deviation).
A future input-enum extension will restore the canonical keys.

## Physics

The primary reference is `bunnymark/update_native_position_velocity` from
`britzl/defold-bunnymark`.

| Constant / behavior | Value |
| ------------------- | ----- |
| Spawn position      | `x=random(viewport_width)`, `y=random(930, 1030)` |
| Initial velocity    | `vy=-random(200)` |
| Acceleration        | `vy -= 1200 * dt` |
| Position update     | `y += vy * dt` |
| Floor bounce        | if `y < 50`: `y = 50`, `vy = -vy` |
| Horizontal motion   | none in this reference path |

Coordinate convention: y-up, bottom-left origin (D-25), matching the engine
ortho setup. The pure-C `bunny_physics.h` header is stdint-only.

## Spawn Rate Tuning

`BUNNY_CLICK_SPAWN_COUNT` (default **500**) matches the Defold reference click
behavior. `BUNNY_HOLD_SPAWN_RATE` (default **50**) controls the extra per-frame
spawn count while the left mouse / tap is held.

## Adding HD Art (enabling the H toggle)

The `H` key + top-right "Quality" tap zone toggle SD <-> HD packs at runtime.
Phase 48 atlas merge re-maps regions in place so live SpriteComponent
`region_index` values stay valid across the toggle — no `set_region`
re-binding is needed (DEMO-08).

To enable HD:

1. Drop 5 high-res PNGs into `examples/bunnymark/raw/hd/` with the SAME
   filenames as the SD variants:
   - `bunny_red.png`
   - `bunny_green.png`
   - `bunny_blue.png`
   - `bunny_yellow.png`
   - `bunny_purple.png`
   The current HD art is about 17x the SD originals (the HD pack uses
   `pixels_per_unit = 17.0`, so HD sprites render at the same on-screen size
   as SD).
2. Re-run cmake configure: `cmake --preset native-debug`. The configure step
   detects the `raw/hd/` directory and adds `bunnymark_hd.ntpack` to the
   build (sets `BUNNYMARK_HD_AVAILABLE = 1`).
3. Rebuild: `cmake --build build/_cmake/native-debug --target bunnymark_demo`.
4. The H key + tap-zone toggle will then mount/unmount the HD pack at
   runtime via the Phase 48 atlas merge.

When `raw/hd/` is absent, the demo still builds (SD-only) and the toggle logs
a warning ("HD pack not available — toggle is a no-op") instead of crashing.

## Asset License

5 of the 12 bunny PNG variants from
[britzl/defold-bunnymark](https://github.com/britzl/defold-bunnymark)
(`assets/images/rabbitv3*.png`), originally from the
[PixiJS bunny-mark sample](https://github.com/pixijs/bunny-mark). Both
upstream sources are MIT-licensed.

The 5 SD PNGs ship with the repo at `examples/bunnymark/raw/sd/`. HD art is
optional — see "Adding HD Art" above to drop in higher-resolution variants.

## Throughput Log Schema

Bunnymark emits its own per-60-frame console log line (game owns the format,
engine `nt_stats` only provides accessors):

```
fps=F.f cpu=C.c ms gpu=G.g ms draws=D bunnies=B atlas=SD|HD
```

GPU time is `N/A` when `EXT_disjoint_timer_query_webgl2` is unavailable or
the WebGL timer query is disjoint (no estimation, no fallback heuristic).

## Comparison Targets

Use the same workload (canvas size, spawn rate, browser, hardware) and plot
FPS vs bunny count from the throughput log against the upstream variants:

- **PixiJS**: https://github.com/pixijs/bunny-mark
- **Defold**: https://github.com/britzl/defold-bunnymark

The FPS curve degradation point and the CPU/draws ratio at saturation are the
two most informative comparison metrics.
