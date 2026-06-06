# Pass 10 Generated Wayfarer And Equipment Delivery Review

## Status

Pass 10 is delivered as generated bitmap candidate runtime art.

It is not final accepted art yet. It passed pack/runtime L5 visibility, but still needs GDD/user readability and tone review.

## Source

Concept/reference sources:

```text
gamedesign/assets/concept/pass_10_generated_wayfarer_equipment/pass_10_wayfarer_sand_source.png
gamedesign/assets/concept/pass_10_generated_wayfarer_equipment/pass_10_equipment_sand_source.png
```

Runtime slicing sources:

```text
gamedesign/assets/concept/pass_10_generated_wayfarer_equipment/pass_10_wayfarer_chroma_source.png
gamedesign/assets/concept/pass_10_generated_wayfarer_equipment/pass_10_equipment_chroma_source.png
```

Slicing/validation/contact tool:

```text
gamedesign/tools/pass_10_generated_wayfarer_equipment_runtime.py
```

Contact sheet:

```text
gamedesign/assets/concept/pass_10_generated_wayfarer_equipment/pass_10_generated_wayfarer_equipment_contact_sheet.png
```

## Runtime Files

Hero map sprites, `128x128 RGBA`:

```text
games/turkic-jam-2026/raw/hero/hero_wayfarer_idle_s.png
games/turkic-jam-2026/raw/hero/hero_wayfarer_walk_s.png
games/turkic-jam-2026/raw/hero/hero_wayfarer_walk_e.png
games/turkic-jam-2026/raw/hero/hero_wayfarer_walk_n.png
games/turkic-jam-2026/raw/hero/hero_wayfarer_walk_w.png
```

Hero panel doll, `160x220 RGBA`:

```text
games/turkic-jam-2026/raw/hero/hero_wayfarer_panel.png
```

Equipment items and slot frames, `64x64 RGBA`:

```text
games/turkic-jam-2026/raw/equipment/equip_weapon_staff_01.png
games/turkic-jam-2026/raw/equipment/equip_clothes_cloak_01.png
games/turkic-jam-2026/raw/equipment/equip_tamga_charm_01.png
games/turkic-jam-2026/raw/equipment/equip_tool_satchel_01.png
games/turkic-jam-2026/raw/equipment/equip_slot_weapon_01.png
games/turkic-jam-2026/raw/equipment/equip_slot_clothes_01.png
games/turkic-jam-2026/raw/equipment/equip_slot_tamga_01.png
games/turkic-jam-2026/raw/equipment/equip_slot_tool_01.png
```

All files validated as RGBA with non-empty alpha and exact target dimensions.

## Pack And L5 Evidence

Pack builder after Pass 10:

```text
Batch A found 44 / missing 0
Batch B found 47 / missing 0
Batch C aul progression found 6 / missing 0
Batch C hero archetype panels found 3 / missing 0
Pass 6 future library found 21 / missing 0
Optional production sprites found 121 / missing 0
Atlas 125 sprites
Generated merged header 132 assets
CRC32 0xE159B025
```

Important pack note:

The normal build output initially produced a valid pack but the first L5 frame still showed old hero/equipment art. A fresh cache-miss pack output was built in:

```text
tmp/pass10_pack_fresh/
```

That pack was copied to:

```text
build/games/turkic-jam-2026/native-debug/assets/turkic_jam.ntpack
```

Then desktop L5 showed the new generated hero/equipment correctly.

Fresh L5 proof:

```text
tmp/visual_qa_l5_pass10_wayfarer_equipment_freshpack.png
1280x720
all_black=0
all_white=0
all_same=0
rgb_range=0..255
checksum=0xED970691
```

Runtime bind counts:

```text
Batch A 44/44
Batch B 47/47
Batch C aul 6/6
Batch C hero archetype panels 3/3
Pass 6 future library 21/21
```

## GDD Visual Review

Accepted for direction:

- the wayfarer now reads as painted/generated bitmap art, not a flat technical puppet;
- map sprite, panel doll, and equipment share a coherent costume/equipment language;
- right hero panel is much closer to RPG/Loop Hero style;
- equipment items are readable at panel scale;
- slot frames are stronger and more thematic.

Risks:

- the first wayfarer may read slightly too heroic/elite for a “first average traveler”;
- map sprite detail may be dense at `46x56` in real gameplay;
- the staff/charm silhouette is strong but can add visual clutter on road cells;
- generated costume symbols must remain fictional and should not be treated as researched real tamgas;
- final acceptance still needs user/GDD review after the map migration is complete.

## Next Gate

Do not send a new Code task while the map migration is paused/active in another agent.

After map migration completes, request one fresh desktop L5 screenshot of normal gameplay/map + right panel using:

```text
hero_wayfarer_* sprites
hero_wayfarer_panel
equip_* slots and items
```

Final status:

```text
candidate generated bitmap runtime art
L1/L2/L3/L5 visible in QA: pass
final accepted: no
```

