# 53. Final repaint pass 7 - hero archetype panels delivery review

Status: corrected generated bitmap candidate art, registered in pack/runtime QA, pending L5 screenshot/readability.

Owner: GDD/Art lead review.

Source contract:

```text
51_final_repaint_pass_7_hero_archetype_panels_contract.md
```

## Delivered files

Hero archetype panels, `128x192`, RGBA:

```text
raw/hero/hero_body_panel.png
raw/hero/hero_mind_panel.png
raw/hero/hero_spirit_panel.png
```

Icon readability fixes, `32x32`, RGBA:

```text
raw/icons/icon_aul_upgrade_32.png
raw/icons/icon_settings_32.png
```

Contact sheet:

```text
gamedesign/assets/concept/final_repaint_pass_7_hero_archetype_panels/final_repaint_pass_7_hero_archetype_panels_contact_sheet.png
```

Superseded script generator:

```text
gamedesign/tools/final_repaint_pass_7_hero_archetype_panels.py
```

Corrected generated bitmap repaint generator:

```text
gamedesign/tools/final_repaint_pass_7_imagegen_repaint.py
```

Generated bitmap source:

```text
gamedesign/assets/concept/final_repaint_pass_7_hero_archetype_panels/final_repaint_pass_7_imagegen_source.png
```

Validator:

```text
tmp/validate_final_repaint_pass_7_hero_archetype_panels.py
```

## Technical verification

GDD ran:

```text
py -3.12 tmp/validate_final_repaint_pass_7_hero_archetype_panels.py
```

Result after corrected generated bitmap repaint:

```text
hero/hero_body_panel.png: 128x192 RGBA bbox=(9, 8, 119, 188)
hero/hero_mind_panel.png: 128x192 RGBA bbox=(10, 8, 93, 188)
hero/hero_spirit_panel.png: 128x192 RGBA bbox=(6, 9, 122, 188)
icons/icon_aul_upgrade_32.png: 32x32 RGBA bbox=(2, 0, 30, 32)
icons/icon_settings_32.png: 32x32 RGBA bbox=(2, 6, 30, 26)
gamedesign/assets/concept/final_repaint_pass_7_hero_archetype_panels/final_repaint_pass_7_hero_archetype_panels_contact_sheet.png: 880x610 RGBA
OK final repaint pass 7 hero archetype panels
```

GDD also reran pack builder and native target after delivery:

```text
Batch A found 44 / missing 0
Batch B found 47 / missing 0
Batch C aul progression found 6 / missing 0
Pass 6 future library found 21 / missing 0
Optional production sprites found 118 / missing 0
Atlas packed 122 sprites, 121 unique, 1 page
Generated merged header 129 assets
CRC32 0xFC0CEAF1

cmake --build build/_cmake/native-debug --target turkic_jam
PASS
```

Initial delivery note: before Code registry, the three hero panel dolls were not included in the atlas. This is now resolved by the separate `Batch C hero archetype panels` group. The two icon readability fixes were already in registered groups:

- `icon_settings_32` is Batch B;
- `icon_aul_upgrade_32` is Pass 6 future library.

## Art review

Accepted after correction:

- Body/Mind/Spirit now use generated bitmap / painted source, not the rejected SVG-like script art.
- Body/Mind/Spirit read as variants of the same grounded traveler language.
- Silhouettes differ without becoming wizard/knight/fantasy class cosplay.
- Mind is the clearest archetype at reduced UI scale because the map strip is readable.
- Spirit keeps the fictional tamga/charm language and does not become a magic rune character.
- Body reads as sturdier and practical; the equipment silhouette works in the right-panel doll role.
- The aul upgrade icon is stronger than the previous tiny silhouette.

Risks:

- `icon_settings_32.png` at 24px may still read as a small object/eye rather than settings. Keep as runtime QA risk.
- `hero_mind_panel.png` map detail may become too small in the real heir-select panel if the UI uses smaller than the contact-sheet reduced scale.
- `hero_spirit_panel.png` side flag/cloth may be read as a generic banner if cropped tightly.
- Need real right-panel/heir-select background proof; the contact sheet uses a dark preview background only.

Rejected / not present:

- No Arabic palace/gold-fantasy direction.
- No copied real sacred tamga.
- No genre-breaking RPG class costume.

## Source-policy decision

The original script-shaped Pass 7 delivery is superseded. The active runtime files are from generated bitmap source plus cleanup/slicing. This matches `54_art_source_policy_generated_bitmap.md`.

## L-level decision

```text
L1 raw PNG exists: PASS for 5 delivered files.
L2 builder found: PASS for icon files and 3 hero archetype panels.
L3 bind: PASS for Batch C hero panels 3/3 in desktop devapi.
L4 drawn: QA-ONLY DEVAPI/TREE PROVEN for visual QA harness; not production heir-select UI.
L5 screenshot/readability: NOT PROVEN.
```

## Code registry result

Code registered the three hero archetype panel dolls as a separate optional Batch C group:

```text
hero_body_panel
hero_mind_panel
hero_spirit_panel
```

Verified evidence:

```text
Batch C hero archetype panels found 3 / missing 0
Optional production sprites found 121 / missing 0
Atlas packed 125 sprites, 124 unique, 1 page
Generated merged header 132 assets
CRC32 0xEC459C00
cmake --build build/_cmake/native-debug --target turkic_jam: PASS
visual_qa.status: batch_c_hero_panels [3,3]
```

Rules still active:

1. Keep them inactive in normal gameplay.
2. Do not change balance, heir logic, or normal boot flow.
3. Visual QA display is QA/debug only.
4. Do not call final accepted until screenshot/readability proof exists.

## Next review gate

The next GDD acceptance gate is not another repaint. It is runtime proof:

```text
heir/archetype panel or QA view showing Body/Mind/Spirit dolls at final UI scale
24px proof for icon_settings_32 and icon_aul_upgrade_32
```
