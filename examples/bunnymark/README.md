# Bunnymark Demo

CPU sprite-batching stress test for Neotolis Engine. Mirrors the
[PixiJS](https://github.com/pixijs/bunny-mark) and
[britzl/defold-bunnymark](https://github.com/britzl/defold-bunnymark) layouts
for fair side-by-side benchmark comparison on the same hardware/browser.

## Test Conditions (DEMO-07)

| Parameter      | Value                                                            |
| -------------- | ---------------------------------------------------------------- |
| Viewport       | 800 x 600 (default; resizable)                                   |
| Sprite size    | ~26 x 37 px (SD) / ~52 x 74 px (HD, 2x source)                   |
| Blend mode     | Premultiplied alpha (D-24)                                       |
| Depth test     | Off (D-24)                                                       |
| Atlas pages    | 1                                                                |
| Vertex format  | float3 pos + float2 uv + uint8[4] color = 24 B (SPRITE-05)       |
| Pixels per unit | 1.0 (SD) / 2.0 (HD) — runtime ipu = 1/ppu bake (D-32)            |
| Browser / GPU  | Logged at startup (`gpu=unknown` until engine exposes caps)      |
| Bunny pool cap | 16384 (uint16_t entity slot bound — Phase 50 Plan 06 deviation)  |

The startup conditions log line follows the schema:

```
Bunnymark conditions: viewport=WxH sprite_size=~26x37 px (SD) blend=premultiplied atlas=SD|HD pages=1 hold_rate=R bunny_max=M hd_available=0|1 gpu=...
```

## Controls

| Action                              | Input                                              |
| ----------------------------------- | -------------------------------------------------- |
| Spawn 1 bunny at cursor / touch     | Click / tap canvas                                 |
| Spawn 50/frame at cursor (hold)     | Hold left mouse / hold tap                         |
| +100 / -100 bunnies (random pos)    | Arrow Up / Arrow Down                              |
| +1000 / -1000 bunnies               | Shift + Arrow Up / Shift + Arrow Down              |
| Toggle SD <-> HD                    | `H` key OR top-right "Quality" tap zone (~120x40)  |
| Quit                                | `Esc` (native only — web ignores)                  |

**Note on bulk add/remove keys:** the canonical Bunnymark uses `+` / `-`, but
the engine input enum currently does not expose `NT_KEY_PLUS` / `NT_KEY_MINUS`.
Arrow Up / Arrow Down were substituted in Phase 50 Plan 06 (Rule 3 deviation).
A future input-enum extension will restore the canonical keys.

## Physics

PixiJS canonical constants (verified against `pixijs/bunny-mark` `src/Bunny.js`
in Phase 50 Plan 06 — Pitfall 2 corrects the CONTEXT D-45 baseline):

| Constant       | Value          | Note                                                                |
| -------------- | -------------- | ------------------------------------------------------------------- |
| gravity        | 0.75 px/frame  | downward; corrects CONTEXT D-45 baseline (was 0.5)                  |
| vx initial     | [0, 10)        | uniform random                                                       |
| vy initial     | [-5, +5)       | uniform random; corrects CONTEXT D-45 baseline (was 0..-10)         |
| Bottom bounce  | vy *= -0.85    | + 50% chance of additional `rand * 6` upward kick                   |
| Top edge       | vy = 0 (clamp) | no bounce — gravity returns the bunny                               |
| Side edges     | vx *= -1       | identical-magnitude reflection + clamp to edge                      |

Coordinate convention: y-up, bottom-left origin (D-25). The PixiJS source uses
y-down; `bunny_physics.h` flips the sign convention so gravity decrements vy
and bottom is `y < 0`.

The physics is fixed-timestep (60 fps assumed) for fair direct comparison
with the canonical Bunnymark variants.

The pure-C `bunny_physics.h` header is libm + stdint only — no engine deps —
and is exercised by `tests/unit/test_bunnymark_physics.c` with a fixed-seed
xorshift64* PRNG so the 6 tests are deterministic across platforms (DEMO-02
closure).

## Spawn Rate Tuning

`BUNNY_HOLD_SPAWN_RATE` (default **50**, CONTEXT D-43) controls the per-frame
spawn count while the left mouse / tap is held. PixiJS canonical is **100**
per held frame. Set the define to 100 in `main.c` for direct apples-to-apples
comparison; the default 50 gives smoother visual feedback at low bunny counts.

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
   Recommended size: 2x the SD originals (the HD pack uses
   `pixels_per_unit = 2.0`, so HD sprites at 2x source pixels render at the
   same on-screen size as SD).
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

The 5 SD PNGs are fetched on demand by `tools/fetch_bunnymark_art.{ps1,sh}`
(idempotent — re-runs are a no-op when the files already exist).

## Throughput Log Schema (DEMO-06)

`nt_stats` (engine module — `engine/stats/nt_stats.h`) emits a per-period
console log line:

```
frame=N fps=F.f cpu=C.c ms gpu=G.g ms draws=D bunnies=B atlas=SD|HD
```

The default period is 60 frames (`nt_stats_desc_t.throughput_log_period`).
GPU time is `-1.0` (logged as `N/A`) when `EXT_disjoint_timer_query_webgl2`
is unavailable or the timer query is disjoint (Pitfall 5: no estimation, no
fallback heuristic). The wider Phase 50 milestone delivers `nt_stats` as a
reusable engine module — wiring it into the demo's frame loop and overlay is
a separate workstream (see Plan 07 SUMMARY for the deferred-overlay note).

## Comparison Targets

Use the same workload (canvas size, spawn rate, browser, hardware) and plot
FPS vs bunny count from the throughput log against the upstream variants:

- **PixiJS**: https://github.com/pixijs/bunny-mark
- **Defold**: https://github.com/britzl/defold-bunnymark

The FPS curve degradation point and the CPU/draws ratio at saturation are the
two most informative comparison metrics.
