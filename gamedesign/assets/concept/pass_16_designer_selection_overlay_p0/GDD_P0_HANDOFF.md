# P0 Selection Overlay Handoff

Status: conditionally accepted concept/proposal art for Code integration after GDD approval. Final acceptance still depends on GDD review of a live-runtime normal gameplay screenshot.

## Component Inventory

Component family:

- `selection_overlay`

Source assets:

- `selection_overlay_source_chromakey.png`
- `selection_overlay_source_alpha.png`

Proof assets:

- `selection_overlay_contact_over_normal_gameplay.png`
- `selection_overlay_runtime_metrics.csv`

## Approved Files For Code Integration

Code should integrate only these proposal files, preserving names and dimensions:

- `proposed_runtime/ui/ui_valid_cell_overlay_128.png`
- `proposed_runtime/ui/ui_invalid_cell_overlay_128.png`
- `proposed_runtime/ui/ui_hover_cell_overlay_128.png`

Do not integrate any other source, contact, or scratch files.

## Reuse Map

| Source component | Runtime candidate | Used by | Notes |
| --- | --- | --- | --- |
| Dusty teal edge + sand rim | `ui_valid_cell_overlay_128.png` | valid buildable cells, selected target feedback | Quiet permission signal with transparent center. |
| Warm red broken dust edge | `ui_invalid_cell_overlay_128.png` | blocked/no-build/road-buffer feedback | Rejection signal through broken corners and edge dust only; no icon, X, warning mark, or center symbol. |
| Warm ochre/gold dust edge | `ui_hover_cell_overlay_128.png` | hover/focus preview | Focus signal from the same overlay family. |

## Acceptance Risks

- The family is visually heavier and more decorative than an ideal quiet dust-only overlay. Code should integrate at current scale/alpha first, then GDD should review in live gameplay before final approval.
- Valid and hover both use warm sand/dust support color, so runtime alpha/tint should preserve the teal-vs-ochre distinction.
- This is proposal art only until GDD approves the live-runtime screenshot after pack/runtime integration.

## Boundary

No files in `games/turkic-jam-2026/raw/` were changed by Designer. No runtime C code, schemas, build scripts, or coordination files were changed.
