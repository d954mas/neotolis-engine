# Pass 16 Designer Selection Overlay P0

Status: Designer proposal candidate for GDD review. No raw/runtime integration.

## Component Inventory

Component family:

- `selection_overlay`

Existing sources checked:

- `gamedesign/assets/concept/pass_12_generated_ui_surfaces/pass_12_ui_material_source.png`
- `gamedesign/assets/concept/pass_12_generated_ui_surfaces/pass_12_generated_ui_surfaces_contact_sheet.png`
- `gamedesign/assets/concept/pass_selection_overlay/*` as rough/negative context only, not approved art
- `tmp/normal_gameplay_placement_state.png` as the release gameplay placement context

New generated source needed:

- Yes. Existing UI material sheets provide felt/leather/woven language, but not map-cell local dusty placement feedback with transparent centers.

Runtime files touched:

- None. These are proposal-only candidates under this concept pass.

Proposal runtime candidates:

- `proposed_runtime/ui/ui_valid_cell_overlay_128.png`
- `proposed_runtime/ui/ui_invalid_cell_overlay_128.png`
- `proposed_runtime/ui/ui_hover_cell_overlay_128.png`

## Source Sheet

Source files:

- `selection_overlay_source_chromakey.png` - generated painterly bitmap source on removable chroma background
- `selection_overlay_source_alpha.png` - alpha-cleaned source sheet used for exports

The first generated attempt was rejected before export because invalid-state motifs could read as cross/warning/icon language. This pass uses the cleaner second generated family: dust breaks, broken corner strokes, and edge/corner material language only.

## Contact Proof

Contact sheet:

- `selection_overlay_contact_over_normal_gameplay.png`

Proof context:

- Composited over `tmp/normal_gameplay_placement_state.png`
- Runtime candidates shown at exact source size and at actual gameplay map-cell scale, measured from the screenshot as 54px cells

Readability result:

- `valid` reads as dusty teal permission.
- `invalid` reads as warm red broken dust/corner rejection.
- `hover` reads as warm ochre/gold focus.

## Reuse Map

| Source component | Runtime candidate | Used by | Notes |
| --- | --- | --- | --- |
| Dusty teal edge + sand rim | `ui_valid_cell_overlay_128.png` | all valid buildable cells, selected target pulse/tint | Quiet permission signal; center remains open for map/tile art. |
| Warm red broken dust edge | `ui_invalid_cell_overlay_128.png` | blocked/no-build/road-buffer feedback | Broken rim and corners only; no X, icon, warning sign, rune, arrow, or full-cell fill. |
| Warm ochre/gold dust edge | `ui_hover_cell_overlay_128.png` | hover/focus preview | Same family as valid/invalid; lower semantic intensity than valid. |

## Risks

- The generated family is slightly more ornate at corners than a pure dust-only overlay. At 54px gameplay scale it reads as corner material rather than icons, but GDD should confirm it does not feel too decorative.
- The valid and hover states both use warm dust around the rim; their hue separation is readable in the contact sheet, but runtime alpha/tint choices should preserve teal-vs-ochre contrast.
- The source cleanup intentionally clears the center after chroma removal. A few antialias/dust pixels remain near the inner rim, but no state repaints the tile center.
- This is only an art proposal. Code should copy/integrate these filenames into `games/turkic-jam-2026/raw/ui/` only after GDD approval and then produce the normal pack/runtime proof.

## GDD/Code Integration Boundary

Do not integrate before approval:

- no writes to `games/turkic-jam-2026/raw/`
- no runtime C/schema/build-script changes
- no new placement-state filenames or one-off card-specific overlays

After GDD approval, Code should integrate only:

- `ui_valid_cell_overlay_128.png`
- `ui_invalid_cell_overlay_128.png`
- `ui_hover_cell_overlay_128.png`
