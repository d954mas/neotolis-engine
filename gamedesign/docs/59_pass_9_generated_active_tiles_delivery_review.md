# Pass 9 Generated Active Tiles Delivery Review

## Decision

Pass 9 replaces the SVG/script-looking active tile direction with generated bitmap source.

This is a candidate runtime pass, not final acceptance. Final acceptance is blocked until the current map rendering migration is complete and a fresh desktop L5 screenshot shows the active tiles in the real gameplay map.

## Source

Rejected source attempt:

```text
gamedesign/assets/concept/pass_9_generated_active_tiles/pass_9_active_tile_source_sheet.png
```

Reason: it used a magenta/chroma background and left visible pink fringe after cutting.

Accepted source for slicing:

```text
gamedesign/assets/concept/pass_9_generated_active_tiles/pass_9_active_tile_source_sheet_v2.png
```

Reason: no magenta, stronger generated bitmap painting, better Turkic desert object read.

## Runtime Files

Generated active tile candidates written as `128x128 RGBA`:

```text
games/turkic-jam-2026/raw/tiles/tile_saxaul_01.png
games/turkic-jam-2026/raw/tiles/tile_yurt_01.png
games/turkic-jam-2026/raw/tiles/tile_tamga_stone_01.png
games/turkic-jam-2026/raw/tiles/tile_wolf_track_01.png
games/turkic-jam-2026/raw/tiles/tile_oasis_01.png
games/turkic-jam-2026/raw/tiles/tile_mirage_01.png
games/turkic-jam-2026/raw/tiles/tile_storm_01.png
games/turkic-jam-2026/raw/tiles/tile_last_tamga_01.png
```

Slicing tool:

```text
gamedesign/tools/pass_9_generated_active_tiles_runtime.py
```

Contact sheet:

```text
gamedesign/assets/concept/pass_9_generated_active_tiles/pass_9_generated_active_tiles_runtime_contact_sheet.png
```

## Pack Evidence

After pack rebuild:

```text
Optional production sprites: found 121 / missing 0
Atlas: 125 sprites
Generated merged header: 132 assets
CRC32: 0xF236ACF9
```

Fresh desktop QA dump:

```text
tmp/visual_qa_l5_pass9_active_tiles.png
1280x720
all_black=0
all_white=0
all_same=0
rgb_range=0..255
checksum=0xBE525B7D
```

## GDD Visual Review

Direction accepted:

- generated bitmap source is visibly better than script/SVG-like assets;
- `saxaul`, `yurt`, `tamga_stone`, `oasis`, `last_tamga` now have stronger object identity;
- `storm` reads more clearly as sand/wind than the older abstract version.

Risks:

- v2 source had a baked checkerboard, so the slicing tool removes near-white neutral background;
- remaining pale edges can read as cutout halos, especially on `mirage`, `storm`, `trail`;
- detail may be too high for 56px map scale;
- `wolf_track` / beast trail still needs design review for name, meaning, and silhouette;
- active tiles should eventually be generated as true transparent objects, not checkerboard-cleaned objects.

## Current Code Coordination

Do not request more Code work for this pass right now.

The Code thread is paused because another agent is migrating the map from Clay to sprites. After that migration, GDD should request one fresh L5 map screenshot with these generated active tile candidates in the actual sprite map.

Acceptance remains:

```text
not final
candidate runtime art
pending post-map-migration L5 review
```

