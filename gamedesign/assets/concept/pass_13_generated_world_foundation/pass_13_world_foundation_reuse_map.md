# Pass 13 World Foundation Reuse Map

Status: slicing proposal only. Runtime raw overwrite is not approved.

## Component Inventory

- ground sand base
- decor overlays: dune, stones, dry_grass, tracks, cracks
- road material: straight/corner pieces
- road buffer: stones and packed edge
- aul core: packed earth and fire

## Existing Sources Reused

- `pass_13_world_foundation_source.png`: generated bitmap world material family source
- Current `games/turkic-jam-2026/raw/*`: comparison only, not overwritten

## New Generated Source Needed

No additional generation for this slicing proposal. Use one coherent Pass 13 source sheet.

## Runtime Files Touched

None. Proposed exports are written only under `gamedesign/assets/concept/pass_13_generated_world_foundation/proposed_runtime/`.

## Reuse Map

- source crop `(183, 15, 349, 170)` -> proposed `ground/ground_sand_base_01.png` -> role: quiet sand base
- source crop `(535, 18, 760, 160)` -> proposed `decor/decor_dune_01.png` -> role: quiet low dune overlay
- source crop `(520, 165, 760, 286)` -> proposed `decor/decor_stones_01.png` -> role: small stones overlay
- source crop `(758, 168, 938, 305)` -> proposed `decor/decor_dry_grass_01.png` -> role: small dry grass overlay
- source crop `(16, 350, 220, 458)` -> proposed `decor/decor_tracks_01.png` -> role: old tracks, not wolf trail
- source crop `(775, 350, 960, 466)` -> proposed `decor/decor_cracks_01.png` -> role: dry cracks overlay
- source crop `(981, 20, 1118, 274)` -> proposed `road/road_straight_ns.png` -> role: packed road north-south
- source crop `(975, 224, 1220, 358)` -> proposed `road/road_straight_ew.png` -> role: packed road east-west
- source crop `(1222, 42, 1390, 206)` -> proposed `road/road_corner_ne.png` -> role: packed road corner NE
- source crop `(1368, 42, 1532, 206)` -> proposed `road/road_corner_es.png` -> role: packed road corner ES
- source crop `(1222, 222, 1390, 386)` -> proposed `road/road_corner_sw.png` -> role: packed road corner SW
- source crop `(1368, 222, 1532, 386)` -> proposed `road/road_corner_wn.png` -> role: packed road corner WN
- source crop `(14, 505, 245, 565)` -> proposed `road/buffer_edge_stones_01.png` -> role: stone no-build edge, not wall
- source crop `(248, 505, 520, 565)` -> proposed `road/buffer_packed_sand_01.png` -> role: packed no-build sand edge
- source crop `(5, 705, 370, 1018)` -> proposed `aul/aul_ground_2x2.png` -> role: small packed-earth aul ground
- source crop `(1050, 805, 1185, 950)` -> proposed `aul/aul_fire_01.png` -> role: small campfire

## Per-Asset Proposal

### `ground/ground_sand_base_01.png`

- source crop / component reference: `(183, 15, 349, 170)` from `pass_13_world_foundation_source.png`
- reuse operation: reuse accepted Pass 13 source crop; cleanup/crop/downscale only; no new generation
- export proposal: `128x128 RGB` under `proposed_runtime/`
- role: quiet sand base
- adjustment: `quiet_ground`
- risk: noise

### `decor/decor_dune_01.png`

- source crop / component reference: `(535, 18, 760, 160)` from `pass_13_world_foundation_source.png`
- reuse operation: reuse accepted Pass 13 source crop; cleanup/crop/downscale only; no new generation
- export proposal: `128x128 RGBA` under `proposed_runtime/`
- role: quiet low dune overlay
- adjustment: `quiet_overlay`
- risk: active-object-read / halo

### `decor/decor_stones_01.png`

- source crop / component reference: `(520, 165, 760, 286)` from `pass_13_world_foundation_source.png`
- reuse operation: reuse accepted Pass 13 source crop; cleanup/crop/downscale only; no new generation
- export proposal: `128x128 RGBA` under `proposed_runtime/`
- role: small stones overlay
- adjustment: `quiet_overlay`
- risk: active-object-read / halo

### `decor/decor_dry_grass_01.png`

- source crop / component reference: `(758, 168, 938, 305)` from `pass_13_world_foundation_source.png`
- reuse operation: reuse accepted Pass 13 source crop; cleanup/crop/downscale only; no new generation
- export proposal: `128x128 RGBA` under `proposed_runtime/`
- role: small dry grass overlay
- adjustment: `quiet_overlay`
- risk: active-object-read / halo

### `decor/decor_tracks_01.png`

- source crop / component reference: `(16, 350, 220, 458)` from `pass_13_world_foundation_source.png`
- reuse operation: reuse accepted Pass 13 source crop; cleanup/crop/downscale only; no new generation
- export proposal: `128x128 RGBA` under `proposed_runtime/`
- role: old tracks, not wolf trail
- adjustment: `quiet_overlay`
- risk: active-object-read / halo

### `decor/decor_cracks_01.png`

- source crop / component reference: `(775, 350, 960, 466)` from `pass_13_world_foundation_source.png`
- reuse operation: reuse accepted Pass 13 source crop; cleanup/crop/downscale only; no new generation
- export proposal: `128x128 RGBA` under `proposed_runtime/`
- role: dry cracks overlay
- adjustment: `quiet_overlay`
- risk: active-object-read / halo

### `road/road_straight_ns.png`

- source crop / component reference: `(981, 20, 1118, 274)` from `pass_13_world_foundation_source.png`
- reuse operation: reuse accepted Pass 13 source crop; cleanup/crop/downscale only; no new generation
- export proposal: `128x128 RGBA` under `proposed_runtime/`
- role: packed road north-south
- adjustment: `road`
- risk: road-width mismatch

### `road/road_straight_ew.png`

- source crop / component reference: `(975, 224, 1220, 358)` from `pass_13_world_foundation_source.png`
- reuse operation: reuse accepted Pass 13 source crop; cleanup/crop/downscale only; no new generation
- export proposal: `128x128 RGBA` under `proposed_runtime/`
- role: packed road east-west
- adjustment: `road`
- risk: road-width mismatch

### `road/road_corner_ne.png`

- source crop / component reference: `(1222, 42, 1390, 206)` from `pass_13_world_foundation_source.png`
- reuse operation: reuse accepted Pass 13 source crop; cleanup/crop/downscale only; no new generation
- export proposal: `128x128 RGBA` under `proposed_runtime/`
- role: packed road corner NE
- adjustment: `road`
- risk: road-width mismatch

### `road/road_corner_es.png`

- source crop / component reference: `(1368, 42, 1532, 206)` from `pass_13_world_foundation_source.png`
- reuse operation: reuse accepted Pass 13 source crop; cleanup/crop/downscale only; no new generation
- export proposal: `128x128 RGBA` under `proposed_runtime/`
- role: packed road corner ES
- adjustment: `road`
- risk: road-width mismatch

### `road/road_corner_sw.png`

- source crop / component reference: `(1222, 222, 1390, 386)` from `pass_13_world_foundation_source.png`
- reuse operation: reuse accepted Pass 13 source crop; cleanup/crop/downscale only; no new generation
- export proposal: `128x128 RGBA` under `proposed_runtime/`
- role: packed road corner SW
- adjustment: `road`
- risk: road-width mismatch

### `road/road_corner_wn.png`

- source crop / component reference: `(1368, 222, 1532, 386)` from `pass_13_world_foundation_source.png`
- reuse operation: reuse accepted Pass 13 source crop; cleanup/crop/downscale only; no new generation
- export proposal: `128x128 RGBA` under `proposed_runtime/`
- role: packed road corner WN
- adjustment: `road`
- risk: road-width mismatch

### `road/buffer_edge_stones_01.png`

- source crop / component reference: `(14, 505, 245, 565)` from `pass_13_world_foundation_source.png`
- reuse operation: reuse accepted Pass 13 source crop; cleanup/crop/downscale only; no new generation
- export proposal: `128x128 RGBA` under `proposed_runtime/`
- role: stone no-build edge, not wall
- adjustment: `buffer`
- risk: wall-read

### `road/buffer_packed_sand_01.png`

- source crop / component reference: `(248, 505, 520, 565)` from `pass_13_world_foundation_source.png`
- reuse operation: reuse accepted Pass 13 source crop; cleanup/crop/downscale only; no new generation
- export proposal: `128x128 RGBA` under `proposed_runtime/`
- role: packed no-build sand edge
- adjustment: `buffer`
- risk: wall-read

### `aul/aul_ground_2x2.png`

- source crop / component reference: `(5, 705, 370, 1018)` from `pass_13_world_foundation_source.png`
- reuse operation: reuse accepted Pass 13 source crop; cleanup/crop/downscale only; no new generation
- export proposal: `256x256 RGB` under `proposed_runtime/`
- role: small packed-earth aul ground
- adjustment: `aul_ground`
- risk: noise / active-object-read

### `aul/aul_fire_01.png`

- source crop / component reference: `(1050, 805, 1185, 950)` from `pass_13_world_foundation_source.png`
- reuse operation: reuse accepted Pass 13 source crop; cleanup/crop/downscale only; no new generation
- export proposal: `128x128 RGBA` under `proposed_runtime/`
- role: small campfire
- adjustment: `aul_object`
- risk: noise / active-object-read

## Global Risks

- Sand base may still be too noisy at the increased map scale.
- Decor overlays must stay quieter than Pass 9 active tile objects.
- Road straight/corner widths need GDD review before runtime slicing.
- Buffer stones/packed edge should read no-build, not road or wall.
- Aul ground and fire are proposed first; yurts stay deferred because source yurts are large/detailed.
- Magenta cleanup around thin decor must be validated again before raw export.