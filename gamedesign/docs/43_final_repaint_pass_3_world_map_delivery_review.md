# 43. Final Repaint Pass 3 World Map Delivery Review

Status: candidate final art, waiting for runtime screenshot QA.

Date: 2026-06-06.

## Scope

Pass 3 replaced the world/map runtime PNGs from `41_final_repaint_pass_3_world_map_contract.md`.

This pass covers:

- ground and quiet decor;
- road and road buffer;
- central aul objects;
- active world tile sprites except `tile_saxaul_01.png`.

This pass did not repaint:

```text
raw/tiles/tile_saxaul_01.png
raw/hero/*
raw/ui/*
raw/icons/*
raw/cards/*
raw/equipment/*
```

## Delivered Files

```text
raw/ground/ground_sand_base_01.png      128x128 RGB opaque
raw/decor/decor_dune_01.png             128x128 RGBA
raw/decor/decor_stones_01.png           128x128 RGBA
raw/decor/decor_dry_grass_01.png        128x128 RGBA
raw/decor/decor_tracks_01.png           128x128 RGBA
raw/decor/decor_bones_01.png            128x128 RGBA
raw/decor/decor_cracks_01.png           128x128 RGBA
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
raw/aul/aul_ground_2x2.png              256x256 RGB opaque
raw/aul/aul_yurt_small_01.png           128x128 RGBA
raw/aul/aul_yurt_small_02.png           128x128 RGBA
raw/aul/aul_fire_01.png                 128x128 RGBA
raw/tiles/tile_yurt_01.png              128x128 RGBA
raw/tiles/tile_tamga_stone_01.png       128x128 RGBA
raw/tiles/tile_wolf_track_01.png        128x128 RGBA
raw/tiles/tile_oasis_01.png             128x128 RGBA
raw/tiles/tile_mirage_01.png            128x128 RGBA
raw/tiles/tile_storm_01.png             128x128 RGBA
raw/tiles/tile_last_tamga_01.png        128x128 RGBA
```

Contact sheet:

```text
gamedesign/assets/concept/final_repaint_pass_3_world_map/final_repaint_pass_3_world_map_contact_sheet.png
```

## Technical Verification

GDD-side verification passed:

```text
py -3.12 tmp/validate_final_repaint_pass_3_world_map.py
OK final repaint pass 3 world map

build\games\turkic-jam-2026\native-debug\build_turkic_jam_packs.exe build\games\turkic-jam-2026
Batch A found 44 / missing 0
Batch B found 47 / missing 0
Optional production sprites found 91 / missing 0
Atlas 95 sprites, one page
CRC32 0x5A31FA1F

cmake --build build/_cmake/native-debug --target turkic_jam
passed / no work to do
```

## Art Lead Review

Accepted as candidate final art for first playable.

Strong points:

- road and road-buffer have separate readable identities;
- aul reads as a small warm starting camp, not a city or palace;
- quiet decor is calmer than active tile objects;
- active tiles are separated sprite objects over the ground layer;
- no Arabic palace/genie/bazaar/flying-carpet language is present.

Known risks waiting for runtime screenshot QA:

- `tile_wolf_track_01.png` may be too dark on the dark runtime map background;
- `tile_mirage_01.png` and `tile_storm_01.png` are abstract and need proof at gameplay zoom;
- road-buffer needs screenshot proof that players understand it as no-build edge, not a wall;
- `tile_oasis_01.png` must feel stronger than Saxaul but not like a resort/palm fantasy;
- `tile_last_tamga_01.png` must stay fictional and not read as copied real sacred sign.

## Runtime QA Request

Code should include these files in the same visual QA ladder:

```text
raw PNG -> builder found -> atlas/runtime bind -> drawn state -> screenshot QA
```

Required screenshots:

1. Normal gameplay map with aul, road, buffer, quiet decor and hero.
2. Road corner and `road_entry_aul` visible at the same time.
3. Placed active tile state for `tile_yurt_01`, `tile_tamga_stone_01`, `tile_wolf_track_01`, `tile_oasis_01`, `tile_mirage_01`, `tile_storm_01`.
4. Last Tamga state using `tile_last_tamga_01`.
5. Same map at actual first playable camera/zoom, not only a debug atlas view.

Do not call this pass final accepted art until L5 screenshot/readability is approved.
