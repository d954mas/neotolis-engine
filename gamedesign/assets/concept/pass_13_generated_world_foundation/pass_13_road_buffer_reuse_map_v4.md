# Pass 13 Road/Buffer Reuse Map V4

Status: proposal-only. No runtime raw overwrite.

## Production Inventory

- visible production-facing asset: yes, road/buffer runtime candidates
- visible material source: `pass_13_road_buffer_source_v3.png` labeled `generated_bitmap_source`
- new source sheet needed: no new full generation for V4; V4 reuses the partially accepted V3 road/buffer source
- runtime assets to slice later after GDD approval: six `road_*` files and two `buffer_*` files
- guides/masks: shared road centerline/body masks labeled `technical_mask_only`
- final-looking proposal files: `proposed_runtime_v4/road/*.png` labeled `generated_bitmap_slice` until GDD approves raw copy

## Runtime Files Touched

None. V4 writes only to `proposed_runtime_v4/road/` for review.

## Reuse Map

### `road/road_straight_ns.png`

- proposed concept export: `proposed_runtime_v4/road/road_straight_ns.png`
- label: `generated_bitmap_slice`
- visible source: generated road material crops from V3 source, visible fill; shared 54px body / 74px soft edge technical masks
- geometry/guide: top center + bottom center
- technical mask: `technical_mask_only`, used only for alignment/width
- size/mode: `128x128 RGBA`
- new per-asset generation: no
- raw overwrite: no

### `road/road_straight_ew.png`

- proposed concept export: `proposed_runtime_v4/road/road_straight_ew.png`
- label: `generated_bitmap_slice`
- visible source: generated road material crops from V3 source, visible fill; shared 54px body / 74px soft edge technical masks
- geometry/guide: left center + right center
- technical mask: `technical_mask_only`, used only for alignment/width
- size/mode: `128x128 RGBA`
- new per-asset generation: no
- raw overwrite: no

### `road/road_corner_ne.png`

- proposed concept export: `proposed_runtime_v4/road/road_corner_ne.png`
- label: `generated_bitmap_slice`
- visible source: generated road material crops from V3 source, visible fill; shared 54px body / 74px soft edge technical masks
- geometry/guide: top center + right center
- technical mask: `technical_mask_only`, used only for alignment/width
- size/mode: `128x128 RGBA`
- new per-asset generation: no
- raw overwrite: no

### `road/road_corner_es.png`

- proposed concept export: `proposed_runtime_v4/road/road_corner_es.png`
- label: `generated_bitmap_slice`
- visible source: generated road material crops from V3 source, visible fill; shared 54px body / 74px soft edge technical masks
- geometry/guide: right center + bottom center
- technical mask: `technical_mask_only`, used only for alignment/width
- size/mode: `128x128 RGBA`
- new per-asset generation: no
- raw overwrite: no

### `road/road_corner_sw.png`

- proposed concept export: `proposed_runtime_v4/road/road_corner_sw.png`
- label: `generated_bitmap_slice`
- visible source: generated road material crops from V3 source, visible fill; shared 54px body / 74px soft edge technical masks
- geometry/guide: bottom center + left center
- technical mask: `technical_mask_only`, used only for alignment/width
- size/mode: `128x128 RGBA`
- new per-asset generation: no
- raw overwrite: no

### `road/road_corner_wn.png`

- proposed concept export: `proposed_runtime_v4/road/road_corner_wn.png`
- label: `generated_bitmap_slice`
- visible source: generated road material crops from V3 source, visible fill; shared 54px body / 74px soft edge technical masks
- geometry/guide: left center + top center
- technical mask: `technical_mask_only`, used only for alignment/width
- size/mode: `128x128 RGBA`
- new per-asset generation: no
- raw overwrite: no

### `road/buffer_edge_stones_01.png`

- proposed concept export: `proposed_runtime_v4/road/buffer_edge_stones_01.png`
- label: `generated_bitmap_slice`
- visible source: generated sparse stone buffer crop, reduced alpha/contrast
- geometry/guide: sparse no-build stone edge
- technical mask: `technical_mask_only`, used only for alignment/width
- size/mode: `128x128 RGBA`
- new per-asset generation: no
- raw overwrite: no

### `road/buffer_packed_sand_01.png`

- proposed concept export: `proposed_runtime_v4/road/buffer_packed_sand_01.png`
- label: `generated_bitmap_slice`
- visible source: generated packed-sand buffer crop, reduced alpha/contrast
- geometry/guide: quiet no-build packed sand edge
- technical mask: `technical_mask_only`, used only for alignment/width
- size/mode: `128x128 RGBA`
- new per-asset generation: no
- raw overwrite: no

## V4 Grid Proof

- `pass_13_road_buffer_grid_preview_v4.png` shows a 3x3 closed-loop segment.
- All road exits use centerline `64px` and body width `54px`.
- Straight and corner exits intentionally extend to tile edges.

## Risks

- Runtime zoom may still require lowering edge contrast.
- Buffer strips may need alternate placement rules if repeated every road-buffer cell.
- V4 remains proposal-only until GDD approves raw copy and Code screenshot QA validates the loop.