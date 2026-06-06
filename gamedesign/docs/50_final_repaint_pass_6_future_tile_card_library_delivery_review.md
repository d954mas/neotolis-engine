# 50. Final repaint pass 6 - future tile/card library delivery review

Status: accepted as candidate-final raw art.

Date: 2026-06-06.

Source contract:

- `49_final_repaint_pass_6_future_tile_card_library_contract.md`

## Delivery

Art delivered 22 exact PNG replacements.

Card art, `64x64 RGBA`:

```text
raw/cards/card_art_oasis_64.png
raw/cards/card_art_mirage_64.png
raw/cards/card_art_storm_64.png
raw/cards/card_art_last_tamga_64.png
raw/cards/card_art_well_64.png
raw/cards/card_art_watchtower_64.png
```

Tile sprites, `128x128 RGBA`:

```text
raw/tiles/tile_well_01.png
raw/tiles/tile_watchtower_01.png
raw/tiles/tile_pack_01.png
raw/tiles/tile_small_camp_01.png
raw/tiles/tile_clan_camp_01.png
raw/tiles/tile_hunting_trail_01.png
raw/tiles/tile_vision_01.png
raw/tiles/tile_false_path_01.png
raw/tiles/tile_buried_spring_01.png
```

Utility icons, `32x32 RGBA`:

```text
raw/icons/icon_aul_upgrade_32.png
raw/icons/icon_card_gain_32.png
raw/icons/icon_deck_32.png
raw/icons/icon_map_32.png
raw/icons/icon_memory_32.png
raw/icons/icon_settings_32.png
raw/icons/icon_warning_32.png
```

Contact sheet:

```text
gamedesign/assets/concept/final_repaint_pass_6_future_tile_card_library/final_repaint_pass_6_future_tile_card_library_contact_sheet.png
```

## GDD verification

Command:

```text
py -3.12 tmp/validate_final_repaint_pass_6_future_tile_card_library.py
```

Result:

```text
OK final repaint pass 6 future tile/card library
```

Verified:

- 22 / 22 exact filenames exist.
- Dimensions match contract.
- All files are RGBA.
- Alpha is non-empty.
- Contact sheet exists.

Pack builder check after delivery:

```text
Batch A found 44 / missing 0
Batch B found 47 / missing 0
Batch C aul progression found 6 / missing 0
Optional production sprites found 97 / missing 0
Atlas 101 sprites
CRC32 0xB966229B
```

Important: Pass 6 files are not yet registered as a separate future tile/card/icon group. The total remains `97 / 0`. The CRC changed because `icon_settings_32` is already part of Batch B and was replaced in this pass.

## Art review

Accepted as candidate-final raw art.

Strengths:

- Future card art now reads as intentional assets, not tiny placeholders.
- Future tile objects are transparent active sprites, not baked sand tiles.
- Well/watchtower/pack/camp tiles stay grounded and practical.
- No Aladdin/palace/genie/flying-carpet drift.
- Vision/false path use the fairy-tale direction without becoming generic neon magic.

Risks waiting for runtime QA:

- `icon_settings_32.png` is very small on the contact sheet and may fail at 24px.
- `icon_aul_upgrade_32.png` may need a larger silhouette if used as a primary upgrade button.
- `tile_vision_01.png` and `tile_false_path_01.png` need proof on actual sand/decor backgrounds.
- `card_art_storm_64.png` must read as sandstorm in the real card frame, not generic terrain.
- Future camp tiles may be too close to aul/yurt language if used near the central aul.

## Code request

Add a separate optional registry group for Pass 6 future tile/card/icon library, without activating gameplay features.

Scope:

```text
card_art_oasis_64
card_art_mirage_64
card_art_storm_64
card_art_last_tamga_64
card_art_well_64
card_art_watchtower_64
tile_well_01
tile_watchtower_01
tile_pack_01
tile_small_camp_01
tile_clan_camp_01
tile_hunting_trail_01
tile_vision_01
tile_false_path_01
tile_buried_spring_01
icon_aul_upgrade_32
icon_card_gain_32
icon_deck_32
icon_map_32
icon_memory_32
icon_warning_32
```

`icon_settings_32` is already registered in Batch B. Keep it in Batch B; just treat the replaced PNG as needing L5 icon readability QA.

Expected Code report:

```text
Batch C future tile/card/icon group found X / missing 0
Optional total increased from 97 to expected new total
Generated header ids present
Runtime bind/readiness if low-risk
No production gameplay activation
L4/L5 pending desktop visual QA
```

Do not call these final accepted until screenshot/readability proof exists.
