# Pass 13 World Foundation Reuse Map V2

Status: slicing proposal only. Runtime raw overwrite is not approved.

## Component Inventory

- ground sand base
- decor overlays: dune, stones, dry_grass, tracks, cracks
- road material: straight/corner pieces with consistent mask width
- road buffer: stones and packed edge
- aul core: packed earth and small fire

## Existing Generated Sources To Reuse

- `pass_13_world_foundation_source.png`: accepted generated bitmap world material family source

## New Generated Source Needed

No. V2 reuses the accepted Pass 13 source sheet only.

## Runtime Files Touched

None. V2 writes only to `proposed_runtime_v2/` for GDD review.

## Reuse Map

### `ground/ground_sand_base_01.png`

- source crop / component reference: sand material crop (183, 15, 349, 170)
- reuse operation: accepted source crop plus cleanup/downscale; no new generation
- proposed concept export: `proposed_runtime_v2/ground/ground_sand_base_01.png`
- size/mode: `128x128 RGB`
- role: quiet sand base
- risk: noise
- new generation: no

### `decor/decor_dune_01.png`

- source crop / component reference: single dune crop (548, 30, 725, 150)
- reuse operation: accepted source crop plus cleanup/downscale; no new generation
- proposed concept export: `proposed_runtime_v2/decor/decor_dune_01.png`
- size/mode: `128x128 RGBA`
- role: single quiet low dune overlay
- risk: active-object-read / halo
- new generation: no

### `decor/decor_stones_01.png`

- source crop / component reference: small stone crop (595, 174, 720, 252)
- reuse operation: accepted source crop plus cleanup/downscale; no new generation
- proposed concept export: `proposed_runtime_v2/decor/decor_stones_01.png`
- size/mode: `128x128 RGBA`
- role: small stones overlay
- risk: active-object-read / halo
- new generation: no

### `decor/decor_dry_grass_01.png`

- source crop / component reference: small grass crop (760, 215, 915, 302)
- reuse operation: accepted source crop plus cleanup/downscale; no new generation
- proposed concept export: `proposed_runtime_v2/decor/decor_dry_grass_01.png`
- size/mode: `128x128 RGBA`
- role: small dry grass overlay
- risk: active-object-read / halo
- new generation: no

### `decor/decor_tracks_01.png`

- source crop / component reference: quiet tracks crop (236, 352, 420, 450)
- reuse operation: accepted source crop plus cleanup/downscale; no new generation
- proposed concept export: `proposed_runtime_v2/decor/decor_tracks_01.png`
- size/mode: `128x128 RGBA`
- role: old quiet cart/foot tracks, not beast trail
- risk: active-object-read / halo
- new generation: no

### `decor/decor_cracks_01.png`

- source crop / component reference: cracks crop (835, 365, 958, 455)
- reuse operation: accepted source crop plus cleanup/downscale; no new generation
- proposed concept export: `proposed_runtime_v2/decor/decor_cracks_01.png`
- size/mode: `128x128 RGBA`
- role: dry cracks overlay
- risk: active-object-read / halo
- new generation: no

### `road/road_straight_ns.png`

- source crop / component reference: clean inner road material crop (1100, 25, 1162, 168) + 46px road mask
- reuse operation: accepted road material crop reused with rotation where needed plus shared 46px technical alpha mask for width/alignment proof
- proposed concept export: `proposed_runtime_v2/road/road_straight_ns.png`
- size/mode: `128x128 RGBA`
- role: consistent packed road north-south
- risk: road-width mismatch
- new generation: no

### `road/road_straight_ew.png`

- source crop / component reference: same clean road material crop, rotated + 46px road mask
- reuse operation: accepted road material crop reused with rotation where needed plus shared 46px technical alpha mask for width/alignment proof
- proposed concept export: `proposed_runtime_v2/road/road_straight_ew.png`
- size/mode: `128x128 RGBA`
- role: consistent packed road east-west
- risk: road-width mismatch
- new generation: no

### `road/road_corner_ne.png`

- source crop / component reference: same clean road material crop + 46px L mask
- reuse operation: accepted road material crop reused with rotation where needed plus shared 46px technical alpha mask for width/alignment proof
- proposed concept export: `proposed_runtime_v2/road/road_corner_ne.png`
- size/mode: `128x128 RGBA`
- role: consistent packed road corner NE
- risk: road-width mismatch
- new generation: no

### `road/road_corner_es.png`

- source crop / component reference: same clean road material crop + 46px L mask
- reuse operation: accepted road material crop reused with rotation where needed plus shared 46px technical alpha mask for width/alignment proof
- proposed concept export: `proposed_runtime_v2/road/road_corner_es.png`
- size/mode: `128x128 RGBA`
- role: consistent packed road corner ES
- risk: road-width mismatch
- new generation: no

### `road/road_corner_sw.png`

- source crop / component reference: same clean road material crop + 46px L mask
- reuse operation: accepted road material crop reused with rotation where needed plus shared 46px technical alpha mask for width/alignment proof
- proposed concept export: `proposed_runtime_v2/road/road_corner_sw.png`
- size/mode: `128x128 RGBA`
- role: consistent packed road corner SW
- risk: road-width mismatch
- new generation: no

### `road/road_corner_wn.png`

- source crop / component reference: same clean road material crop + 46px L mask
- reuse operation: accepted road material crop reused with rotation where needed plus shared 46px technical alpha mask for width/alignment proof
- proposed concept export: `proposed_runtime_v2/road/road_corner_wn.png`
- size/mode: `128x128 RGBA`
- role: consistent packed road corner WN
- risk: road-width mismatch
- new generation: no

### `road/buffer_edge_stones_01.png`

- source crop / component reference: low stone strip crop (8, 505, 245, 555)
- reuse operation: accepted source crop plus cleanup/downscale; no new generation
- proposed concept export: `proposed_runtime_v2/road/buffer_edge_stones_01.png`
- size/mode: `128x128 RGBA`
- role: stone no-build edge, not wall
- risk: wall-read
- new generation: no

### `road/buffer_packed_sand_01.png`

- source crop / component reference: packed edge crop (245, 505, 520, 558)
- reuse operation: accepted source crop plus cleanup/downscale; no new generation
- proposed concept export: `proposed_runtime_v2/road/buffer_packed_sand_01.png`
- size/mode: `128x128 RGBA`
- role: packed no-build sand edge, not road
- risk: wall-read / road-read
- new generation: no

### `aul/aul_ground_2x2.png`

- source crop / component reference: large aul ground crop (5, 705, 370, 1018), softened
- reuse operation: accepted source crop plus cleanup/downscale; no new generation
- proposed concept export: `proposed_runtime_v2/aul/aul_ground_2x2.png`
- size/mode: `256x256 RGB`
- role: small packed-earth aul ground
- risk: noise / active-object-read
- new generation: no

### `aul/aul_fire_01.png`

- source crop / component reference: campfire-only crop (1058, 890, 1188, 1015)
- reuse operation: accepted source crop plus cleanup/downscale; no new generation
- proposed concept export: `proposed_runtime_v2/aul/aul_fire_01.png`
- size/mode: `128x128 RGBA`
- role: small first-camp fire only, no rack structure
- risk: halo / active-object-read
- new generation: no

## V2 Notes

- Road family uses generated road material as visible texture and technical masks only for consistent width/alignment.
- Decor candidates were reduced in scale/alpha to avoid active-object read.
- Fire candidate avoids the rack/structure and keeps only the small campfire area.
- Deferred assets remain deferred: bones, stakes, cart marks, yurts, current highlight.