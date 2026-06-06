# Pass 11 Generated HUD And Utility Icons Contract

## Goal

Replace remaining HUD/resource/utility technical icon art with generated bitmap / painted-source bitmap icons.

This pass keeps existing filenames, dimensions, ids, pack schema, UI layout, and balance.

## Allowed Runtime Files

All files must remain exact `32x32 RGBA`:

```text
games/turkic-jam-2026/raw/icons/icon_stamina_32.png
games/turkic-jam-2026/raw/icons/icon_supplies_32.png
games/turkic-jam-2026/raw/icons/icon_wisdom_32.png
games/turkic-jam-2026/raw/icons/icon_glory_32.png
games/turkic-jam-2026/raw/icons/icon_circle_32.png
games/turkic-jam-2026/raw/icons/icon_day_32.png
games/turkic-jam-2026/raw/icons/icon_body_32.png
games/turkic-jam-2026/raw/icons/icon_mind_32.png
games/turkic-jam-2026/raw/icons/icon_spirit_32.png
games/turkic-jam-2026/raw/icons/icon_last_tamga_32.png
games/turkic-jam-2026/raw/icons/icon_settings_32.png
games/turkic-jam-2026/raw/icons/icon_speed_32.png
games/turkic-jam-2026/raw/icons/icon_aul_upgrade_32.png
games/turkic-jam-2026/raw/icons/icon_card_gain_32.png
games/turkic-jam-2026/raw/icons/icon_deck_32.png
games/turkic-jam-2026/raw/icons/icon_map_32.png
games/turkic-jam-2026/raw/icons/icon_memory_32.png
games/turkic-jam-2026/raw/icons/icon_warning_32.png
```

## Icon Meanings

```text
stamina      small waterskin / endurance
supplies     tied provision sack
wisdom       turquoise bead / thought marker
glory        small star/standard
circle       circular road loop
day          sun disc
body         sturdy torso/boot/strength mark
mind         eye/map thought mark
spirit       flame/wind mark
last_tamga   fictional memorial mark
settings     simple toggle/gear-like tool, not modern cog if possible
speed        double arrow / travel pace
aul_upgrade  camp/yurt upgrade arrow
card_gain    new card spark
deck         small stacked cards
map          folded map strip
memory       tied memory token
warning      desert hazard triangle
```

## Art Direction

- generated bitmap / painted-source bitmap;
- readable at `24x24` in top HUD chips;
- high contrast silhouette;
- warm tan/leather/gold with muted turquoise accents;
- simple enough to read as icons, but not flat SVG shapes;
- no text, no numerals, no labels;
- no real sacred tamgas;
- no modern UI icon set look;
- no Arabian palace/genie/lamp/flying carpet/bazaar.

## Required Outputs

Source and contact sheet folder:

```text
gamedesign/assets/concept/pass_11_generated_hud_icons/
```

Required contact sheet:

- all 18 icons at native `32x32`;
- all 18 icons at gameplay preview `24x24`;
- labels below for review only.

## Verification

Required before delivery review:

```text
all files exact 32x32
all files RGBA
all files non-empty alpha
pack builder found counts unchanged or improved
desktop L5 screenshot shows HUD chips and future icon strip
```

Final acceptance requires user/GDD readability review at actual HUD scale.

