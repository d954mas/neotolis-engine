# Pass 13 Road/Buffer Reuse Map V3

Status: road/buffer slicing proposal only. Runtime raw overwrite is not approved.

## Component Inventory

- road material: straight NS/EW and four corners
- road buffer: sparse stones edge and packed sand edge

## Existing Generated Sources To Reuse

- `pass_13_road_buffer_source_v3.png`: generated bitmap road/buffer source sheet for this rejected subfamily
- accepted Pass 13 ground/decor/aul candidates are not touched

## New Generated Source Needed

Yes, one narrow road/buffer generated bitmap source sheet. No full world foundation regeneration.

## Runtime Files Touched

None. V3 writes only to `proposed_runtime_v3/road/` for GDD review.

## Reuse Map

### `road/road_straight_ns.png`

- source crop / component reference: V3 road source vertical road crop (64, 64, 300, 620)
- reuse operation: generated vertical road component, fit to 128 tile with shared 50px road body target
- proposed concept export: `proposed_runtime_v3/road/road_straight_ns.png`
- size/mode: `128x128 RGBA`
- risk: road-width mismatch / edge halo
- new generation: no per-asset one-off; all files reuse one V3 generated source sheet

### `road/road_straight_ew.png`

- source crop / component reference: V3 road source horizontal road crop (395, 190, 815, 378)
- reuse operation: generated horizontal road component, fit to 128 tile with same road body target
- proposed concept export: `proposed_runtime_v3/road/road_straight_ew.png`
- size/mode: `128x128 RGBA`
- risk: road-width mismatch / edge halo
- new generation: no per-asset one-off; all files reuse one V3 generated source sheet

### `road/road_corner_ne.png`

- source crop / component reference: V3 road source corner crop (870, 125, 1160, 392)
- reuse operation: generated corner component, cleaned and fit to 128 tile
- proposed concept export: `proposed_runtime_v3/road/road_corner_ne.png`
- size/mode: `128x128 RGBA`
- risk: corner connection / edge halo
- new generation: no per-asset one-off; all files reuse one V3 generated source sheet

### `road/road_corner_es.png`

- source crop / component reference: V3 road source corner crop (1215, 125, 1500, 392)
- reuse operation: generated corner component, cleaned and fit to 128 tile
- proposed concept export: `proposed_runtime_v3/road/road_corner_es.png`
- size/mode: `128x128 RGBA`
- risk: corner connection / edge halo
- new generation: no per-asset one-off; all files reuse one V3 generated source sheet

### `road/road_corner_sw.png`

- source crop / component reference: V3 road source corner crop (500, 445, 755, 710)
- reuse operation: generated corner component, cleaned and fit to 128 tile
- proposed concept export: `proposed_runtime_v3/road/road_corner_sw.png`
- size/mode: `128x128 RGBA`
- risk: corner connection / edge halo
- new generation: no per-asset one-off; all files reuse one V3 generated source sheet

### `road/road_corner_wn.png`

- source crop / component reference: V3 road source corner crop (855, 445, 1115, 705)
- reuse operation: generated corner component, cleaned and fit to 128 tile
- proposed concept export: `proposed_runtime_v3/road/road_corner_wn.png`
- size/mode: `128x128 RGBA`
- risk: corner connection / edge halo
- new generation: no per-asset one-off; all files reuse one V3 generated source sheet

### `road/buffer_edge_stones_01.png`

- source crop / component reference: V3 buffer source sparse stones crop (60, 805, 515, 942)
- reuse operation: generated stone buffer strip, reduced contrast/alpha and centered as no-build edge overlay
- proposed concept export: `proposed_runtime_v3/road/buffer_edge_stones_01.png`
- size/mode: `128x128 RGBA`
- risk: wall-read / repeated line
- new generation: no per-asset one-off; all files reuse one V3 generated source sheet

### `road/buffer_packed_sand_01.png`

- source crop / component reference: V3 buffer source packed sand crop (1110, 810, 1485, 945)
- reuse operation: generated dusty packed edge, low alpha and shorter than road
- proposed concept export: `proposed_runtime_v3/road/buffer_packed_sand_01.png`
- size/mode: `128x128 RGBA`
- risk: road-read / repeated strip
- new generation: no per-asset one-off; all files reuse one V3 generated source sheet

## V3 Notes

- Road pieces come from visible generated bitmap road components, not procedural strips.
- Crops are reduced into one 128x128 grid family; straight and corner files remain proposal-only.
- Buffer files are intentionally lower-alpha and shorter than road pieces to avoid road/wall read.
- No raw files are overwritten in this pass.