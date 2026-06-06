# 76. Pass 13 partial runtime integration review

Status: delivered partial runtime integration and screenshot review.

## Integrated Runtime Candidate Files

Accepted Pass 13 V2 subset copied into runtime raw:

```text
games/turkic-jam-2026/raw/ground/ground_sand_base_01.png
games/turkic-jam-2026/raw/decor/decor_dune_01.png
games/turkic-jam-2026/raw/decor/decor_stones_01.png
games/turkic-jam-2026/raw/decor/decor_dry_grass_01.png
games/turkic-jam-2026/raw/decor/decor_tracks_01.png
games/turkic-jam-2026/raw/decor/decor_cracks_01.png
games/turkic-jam-2026/raw/aul/aul_ground_2x2.png
games/turkic-jam-2026/raw/aul/aul_fire_01.png
```

Rejected V2 road/buffer files were not copied.

## Verification

```text
build/games/turkic-jam-2026/native-debug/build_turkic_jam_packs.exe build/games/turkic-jam-2026
-> PASS, optional production sprites 121 / missing 0

cmake --build build/_cmake/native-debug --target turkic_jam
-> PASS

runtime dump:
tmp/pass13_v2_partial_runtime.png
-> PASS, nonblank 1280x720, checksum 0xEBA3164C
```

## Screenshot Review

Runtime screenshot:

```text
tmp/pass13_v2_partial_runtime.png
```

Decision:

```text
PARTIAL PASS
```

What improved:

- sand/decor/aul candidates are visible in actual gameplay;
- world cells no longer all read as flat orange technical placeholders;
- aul ground is more material and less UI-box-like;
- campfire is smaller and better aligned with FTUE first-camp mood.

Remaining issues:

```text
road is still old technical art because V2 road/buffer was rejected
green buildable squares still dominate the map
some Russian UI text still has mojibake
```

Important finding:

The green squares are not failed raw decor art. They are runtime overlay logic in `view.c::draw_build_slots()`, which fills buildable cells with bright green while a card is held.

## Next Code Task

Change build-slot overlay so it supports the art instead of covering it:

```text
idle valid slot: transparent or very subtle warm/teal hint
hover valid slot: visible teal/gold placement feedback
pressed valid slot: short placement response
invalid/no-build: quiet dark/dusty feedback
```

Do not change mechanics. This is a visual/UI feedback pass.

## Next Art Task

Create Pass 13 V3 only for:

```text
road straight/corner family
road buffer/no-build edge
```

Do not regenerate accepted ground/decor/aul subset unless the next runtime screenshot fails it.
