# 94. Pass 15 Runtime UI Reuse Review

Status: accepted as runtime reuse proof; not final gameplay UI acceptance.

Reviewed:

```text
tmp/pass15_ui_reuse_runtime_proof.png
games/turkic-jam-2026/coordination/FROM_CODE.md
gamedesign/docs/93_pass_15_ui_contact_review.md
```

## Decision

```text
PASS 15 RUNTIME UI REUSE PROOF ACCEPTED
NOT FINAL GAMEPLAY UI ACCEPTANCE
NO NEW UI GENERATION AUTHORIZED
NEXT: NORMAL GAMEPLAY PLACEMENT-STATE PROOF
```

## What Is Proven

The desktop/native QA screenshot proves that the current runtime can draw the approved Pass 12 UI families in one frame:

```text
ui_panel_felt_dark_96
ui_panel_felt_light_96
ui_button_dark_64
ui_card_playable_96x128
ui_card_selected_96x128
ui_card_back_96x128
ui_slot_equipment_64
ui_chip_resource_64
ui_tooltip_dark_64
```

This is enough to validate the design-system direction:

```text
same chip family for HUD counters
same card families for playable/selected/back states
same slot family for equipment holders
same panel materials for dark/light surfaces
same tooltip/button surfaces visible in runtime
no one-off UI art added
no new raw UI ids added
```

## What Is Not Proven

This screenshot is a visual QA harness, not a normal production gameplay screen.

It does not yet prove:

```text
final 16:9 gameplay layout acceptance
production card-hover tooltip behavior
production selected-card placement flow
valid/invalid cell overlay readability in the actual map
final balance between map, log, card hand and right hero panel
```

The QA surface swatches are useful for proof, but they are not player-facing UI.

## GDD Readability Notes

Accepted for current proof:

```text
top HUD chips read as a shared component family
card hand shows selected/playable/back states clearly enough
right hero/equipment panel reuses the same slot/panel language
left log is present and useful
Russian text is readable in the QA frame
```

Risks to check in normal gameplay:

```text
map must remain the largest and most important object
log should not grow into a dominant chat window
tooltip should be contextual, not a permanent instruction slab
valid/invalid feedback may need a reusable `selection_overlay` family, but only after screenshot-backed failure
right panel must not expose unexplained storage/relic/meta systems before they are taught
```

## Art Decision

Do not generate new UI art yet.

Pass 12 UI families remain the approved baseline for current playable UI:

```text
panel_dark
panel_light
card_face
card_selected
card_back
button
resource_chip
equipment_slot
tooltip
```

Potential future families stay queued only:

```text
selection_overlay_128
log_row_marker_32
panel_header_trim
card_badge_frame
```

Only `selection_overlay_128` is a likely near-term candidate, and only if the next normal gameplay proof shows that runtime-only placement feedback is not readable.

## Required Next Code Proof

Produce a normal gameplay placement-state screenshot, not a UI swatch board.

Target:

```text
one selected card in bottom hand
valid buildable cells highlighted on the actual map
at least one invalid/no-build feedback state if supported
small contextual tooltip or hint for selected card/cell
top HUD chips visible
left log visible but not dominant
right hero/equipment/stats visible
map remains the largest object
```

Constraints:

```text
no new UI art
no new UI ids
no broad layout redesign
no gameplay mechanics changes beyond exposing the proof state
desktop/native screenshot only
```

Report back with:

```text
screenshot path
commands/checks
changed files, if any
confirmation that no raw UI art was added
observed UI gap, if any
```

## Acceptance Gate For Future UI Art

Future UI generation is accepted only if:

```text
normal gameplay proof shows a concrete readable-state problem
GDD authorizes exactly one missing reusable family
Art provides generated bitmap source sheet
Art provides runtime slice proposal
Art provides contact sheet and reuse map
Code integrates only approved filenames
desktop runtime proof confirms it in gameplay
```

Rejected:

```text
new one-off buttons
new one-off card frames
new one-off panels
visible SVG/vector/procedural production art
raw overwrite before GDD approval
```
