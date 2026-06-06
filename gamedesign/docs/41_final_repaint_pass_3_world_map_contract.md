# 41. Final repaint pass 3 world map contract

Status: open, waiting for Art delivery.

Purpose: replace the remaining visible world/map placeholders with candidate final runtime art. This pass is independent from UI screenshot fixes: it targets map readability, road/buffer clarity, aul identity, and active tile objects.

## Decision

Pass 3 focuses on:

- base sand and quiet decor cells;
- loop road and no-build road buffer;
- central aul objects;
- active tile sprites except `tile_saxaul_01.png`, which already has a Pass 1 repaint candidate.

Art must replace exact files under `games/turkic-jam-2026/raw/...`. Code must keep the same ids and filenames.

## Required files

Ground and decor:

```text
raw/ground/ground_sand_base_01.png      128x128 opaque
raw/decor/decor_dune_01.png             128x128 RGBA
raw/decor/decor_stones_01.png           128x128 RGBA
raw/decor/decor_dry_grass_01.png        128x128 RGBA
raw/decor/decor_tracks_01.png           128x128 RGBA
raw/decor/decor_bones_01.png            128x128 RGBA
raw/decor/decor_cracks_01.png           128x128 RGBA
```

Road and road buffer:

```text
raw/road/road_straight_ns.png           128x128 RGBA
raw/road/road_straight_ew.png           128x128 RGBA
raw/road/road_corner_ne.png             128x128 RGBA
raw/road/road_corner_es.png             128x128 RGBA
raw/road/road_corner_sw.png             128x128 RGBA
raw/road/road_corner_wn.png             128x128 RGBA
raw/road/road_entry_aul.png             128x128 RGBA
raw/road/road_current_highlight.png     128x128 RGBA
raw/road/buffer_edge_stones_01.png      128x128 RGBA
raw/road/buffer_packed_sand_01.png      128x128 RGBA
raw/road/buffer_stakes_01.png           128x128 RGBA
raw/road/buffer_cart_marks_01.png       128x128 RGBA
```

Aul:

```text
raw/aul/aul_ground_2x2.png              256x256 opaque
raw/aul/aul_yurt_small_01.png           128x128 RGBA
raw/aul/aul_yurt_small_02.png           128x128 RGBA
raw/aul/aul_fire_01.png                 128x128 RGBA
```

Active tile objects:

```text
raw/tiles/tile_yurt_01.png              128x128 RGBA
raw/tiles/tile_tamga_stone_01.png       128x128 RGBA
raw/tiles/tile_wolf_track_01.png        128x128 RGBA
raw/tiles/tile_oasis_01.png             128x128 RGBA
raw/tiles/tile_mirage_01.png            128x128 RGBA
raw/tiles/tile_storm_01.png             128x128 RGBA
raw/tiles/tile_last_tamga_01.png        128x128 RGBA
```

Do not repaint in this pass:

```text
raw/tiles/tile_saxaul_01.png
raw/hero/*
raw/ui/*
raw/icons/*
raw/cards/*
raw/equipment/*
```

## Art direction

World map priorities:

- readable at gameplay zoom;
- road and road buffer must be immediately distinguishable;
- buffer reads as no-build edge, not a wall/fence;
- quiet decor must stay quieter than active tile objects;
- no empty flat beige cells: base sand should be calm but alive;
- active tile objects must be transparent sprites over ground, not baked full sand tiles.

Aul:

- small warm starting camp, not a city;
- packed earth, 1-2 yurts, fire as home/readiness center;
- no palace, domes, bazaar, gold fantasy, or large city silhouettes.

Tile specifics:

- `tile_yurt_01`: support/standing place, smaller than aul core yurt, clear placed object.
- `tile_tamga_stone_01`: fictional memory stone, not copied real sacred/clan sign.
- `tile_wolf_track_01`: player-facing "Звериная тропа"; use trail/marks/risk path, not a literal giant wolf or magic totem.
- `tile_oasis_01`: stronger and rarer than saxaul; may be more lush, but no palm-resort fantasy.
- `tile_mirage_01`: readable shimmer/false shape, not opaque object clutter.
- `tile_storm_01`: wind/sand pressure, readable as risk.
- `tile_last_tamga_01`: death/memory marker, fictional tamga, clear inheritance target.

## Delivery format for Art

Art report must include:

- exact updated files and dimensions;
- contact sheet path: `gamedesign/assets/concept/final_repaint_pass_3_world_map/`;
- what remains placeholder;
- whether files are ready for programmer pack rebuild;
- which screenshots are needed for final visual acceptance.

## Code QA request

After Art delivery, Code should rerun:

```text
raw PNG -> builder found -> atlas/runtime bind -> drawn in gameplay/map state -> screenshot QA
```

Required screenshot states:

- normal gameplay map with aul, road, buffer, decor cells and hero;
- road turns/corners and aul entry visible;
- at least one placed active tile;
- if possible, a map debug/test state showing yurt, tamga stone, beast trail, oasis, mirage, storm and last tamga.

Until screenshots prove readability, Pass 3 is candidate final art, not final accepted art.

