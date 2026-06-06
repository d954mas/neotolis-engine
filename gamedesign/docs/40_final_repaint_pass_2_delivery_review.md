# 40. Final repaint pass 2 delivery review

Status: delivered, needs runtime screenshot QA.

Contact sheet:

```text
gamedesign/assets/concept/final_repaint_pass_2/final_repaint_pass_2_contact_sheet.png
```

## Delivered files

Art replaced 24 exact runtime PNG files.

Hero:

```text
raw/hero/hero_wayfarer_walk_s.png       128x128 RGBA
raw/hero/hero_wayfarer_walk_e.png       128x128 RGBA
raw/hero/hero_wayfarer_walk_n.png       128x128 RGBA
raw/hero/hero_wayfarer_walk_w.png       128x128 RGBA
raw/hero/hero_wayfarer_panel.png        160x220 RGBA
```

Equipment:

```text
raw/equipment/equip_slot_weapon_01.png      64x64 RGBA
raw/equipment/equip_slot_clothes_01.png     64x64 RGBA
raw/equipment/equip_slot_tool_01.png        64x64 RGBA
raw/equipment/equip_slot_tamga_01.png       64x64 RGBA
raw/equipment/equip_weapon_staff_01.png     64x64 RGBA
raw/equipment/equip_clothes_cloak_01.png    64x64 RGBA
raw/equipment/equip_tool_satchel_01.png     64x64 RGBA
raw/equipment/equip_tamga_charm_01.png      64x64 RGBA
```

HUD and card utility:

```text
raw/icons/icon_body_32.png                   32x32 RGBA
raw/icons/icon_mind_32.png                   32x32 RGBA
raw/icons/icon_spirit_32.png                 32x32 RGBA
raw/icons/icon_circle_32.png                 32x32 RGBA
raw/icons/icon_day_32.png                    32x32 RGBA
raw/icons/icon_speed_32.png                  32x32 RGBA
raw/icons/icon_last_tamga_32.png             32x32 RGBA
raw/cards/card_badge_count_32.png            32x32 RGBA
raw/cards/card_placement_roadside_32.png     32x32 RGBA
raw/cards/card_placement_field_32.png        32x32 RGBA
raw/cards/card_placement_special_32.png      32x32 RGBA
```

No forbidden alias files were created:

```text
raw/equipment/equip_slot_weapon_64.png
raw/equipment/equip_slot_clothes_64.png
raw/equipment/equip_slot_tool_64.png
raw/equipment/equip_slot_tamga_64.png
```

## Technical verification

Local validation:

```text
py -3.12 tmp/validate_final_repaint_pass_2.py
OK final repaint pass 2
```

Pack builder:

```text
build_turkic_jam_packs:
Batch A found 44 / missing 0
Batch B found 47 / missing 0
optional production sprites found 91 / missing 0
atlas 95 sprites, 1 page
CRC32 0xD16B6542
```

Native target:

```text
cmake --build build/_cmake/native-debug --target turkic_jam
passed
```

## Art lead review

Accepted as candidate final art for the next runtime QA pass.

What improved:

- hero walk directions now share one readable traveler silhouette;
- hero panel has a clearer torso/cloak/tamga read than the previous placeholder;
- equipment slots and items are distinct enough for a first playable;
- card placement icons are much stronger than the placeholder set;
- no wrong `_64` slot files were added.

Risks to check in real UI:

- `icon_mind_32.png` is visually small/thin and may disappear at 24px;
- `icon_speed_32.png` may read as generic fast-forward instead of speed;
- `card_placement_field_32.png` and `card_placement_special_32.png` need to be checked inside the real card surface;
- hero walk sprites must be verified in the actual map scale and orientation.

## Code QA request

Run the same evidence ladder:

```text
raw PNG -> builder found -> atlas/runtime bind -> drawn in gameplay/UI state -> screenshot QA
```

Required screenshots:

- HUD with Pass 2 icons at actual 24px scale;
- bottom card hand with badge and placement icons;
- right hero panel with hero portrait and equipment slots/items;
- hero on map using whichever walk/idle sprite the current runtime draws.

Until screenshots prove readability, Pass 2 remains candidate final art, not final accepted art.

