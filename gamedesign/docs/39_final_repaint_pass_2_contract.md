# 39. Final repaint pass 2 contract

Status: open, waiting for Art delivery.

Purpose: turn the next visible placeholder group into candidate final runtime art. This pass is about actual game PNGs, not fake-shots or broad style exploration.

## Decision

Pass 2 focuses on:

- hero movement and hero panel;
- right-panel equipment slots/items;
- remaining HUD stat icons;
- card utility icons used in the hand.

Art must replace exact files under `games/turkic-jam-2026/raw/...`. Code must keep the same ids and filenames.

## Required files

Hero:

```text
raw/hero/hero_wayfarer_walk_s.png       128x128
raw/hero/hero_wayfarer_walk_e.png       128x128
raw/hero/hero_wayfarer_walk_n.png       128x128
raw/hero/hero_wayfarer_walk_w.png       128x128
raw/hero/hero_wayfarer_panel.png        160x220
```

Equipment:

```text
raw/equipment/equip_slot_weapon_01.png      64x64
raw/equipment/equip_slot_clothes_01.png     64x64
raw/equipment/equip_slot_tool_01.png        64x64
raw/equipment/equip_slot_tamga_01.png       64x64
raw/equipment/equip_weapon_staff_01.png     64x64
raw/equipment/equip_clothes_cloak_01.png    64x64
raw/equipment/equip_tool_satchel_01.png     64x64
raw/equipment/equip_tamga_charm_01.png      64x64
```

HUD and card utility:

```text
raw/icons/icon_body_32.png              32x32
raw/icons/icon_mind_32.png              32x32
raw/icons/icon_spirit_32.png            32x32
raw/icons/icon_circle_32.png            32x32
raw/icons/icon_day_32.png               32x32
raw/icons/icon_speed_32.png             32x32
raw/icons/icon_last_tamga_32.png        32x32
raw/cards/card_badge_count_32.png       32x32
raw/cards/card_placement_roadside_32.png 32x32
raw/cards/card_placement_field_32.png    32x32
raw/cards/card_placement_special_32.png  32x32
```

## Art direction

Use the approved readable Turkic UI direction:

- bright enough for the game screen;
- felt, woven trim, simple tamga-like fictional marks;
- no generic dark fantasy;
- no heavy leather UI;
- no Aladdin/palace/genie/flying carpet/bazaar motifs;
- no copied real sacred or clan signs.

Hero:

- first archetype is `wayfarer` / first traveler, not a named hero;
- readable small silhouette;
- felt or cloth cloak, satchel, staff, small fictional tamga charm;
- not knight, anime hero, arabian prince, or ornate RPG portrait.

Tiny icons:

- must read at 24 px in HUD;
- thick silhouettes;
- no fine internal ornament;
- contrast first, decoration second.

## Delivery format for Art

Art report must include:

- exact updated files and dimensions;
- contact sheet path: `gamedesign/assets/concept/final_repaint_pass_2/`;
- what remains placeholder;
- whether files are ready for programmer pack rebuild;
- which screenshots are needed for final visual acceptance.

## Code QA request

After Art delivery, Code should rerun the same evidence ladder:

```text
raw PNG -> builder found -> atlas/runtime bind -> drawn in gameplay/UI state -> screenshot QA
```

Required screenshot states:

- HUD with body/mind/spirit/circle/day/speed/last tamga icons at actual scale;
- bottom card hand with badge and placement icons;
- right hero panel with hero portrait and 4 equipment slots/items;
- hero walking on the map if the current runtime state uses walk sprites.

Until screenshots prove readability, Pass 2 is candidate final art, not final accepted art.
