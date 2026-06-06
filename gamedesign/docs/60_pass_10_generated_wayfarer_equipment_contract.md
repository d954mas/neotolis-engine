# Pass 10 Generated Wayfarer And Equipment Contract

## Goal

Replace the remaining player-facing wayfarer and equipment technical art with generated bitmap / painted-source bitmap assets.

This pass must not change gameplay, ids, pack schema, UI layout, or balance. It only replaces existing runtime PNG files with the same filenames, dimensions, and RGBA format.

## Allowed Runtime Files

Hero map sprites, exact `128x128 RGBA`:

```text
games/turkic-jam-2026/raw/hero/hero_wayfarer_idle_s.png
games/turkic-jam-2026/raw/hero/hero_wayfarer_walk_s.png
games/turkic-jam-2026/raw/hero/hero_wayfarer_walk_e.png
games/turkic-jam-2026/raw/hero/hero_wayfarer_walk_n.png
games/turkic-jam-2026/raw/hero/hero_wayfarer_walk_w.png
```

Hero panel doll, exact `160x220 RGBA`:

```text
games/turkic-jam-2026/raw/hero/hero_wayfarer_panel.png
```

Equipment item icons, exact `64x64 RGBA`:

```text
games/turkic-jam-2026/raw/equipment/equip_weapon_staff_01.png
games/turkic-jam-2026/raw/equipment/equip_clothes_cloak_01.png
games/turkic-jam-2026/raw/equipment/equip_tamga_charm_01.png
games/turkic-jam-2026/raw/equipment/equip_tool_satchel_01.png
```

Equipment slot frames may be repainted only if they remain simple UI surfaces and stay exact `64x64 RGBA`:

```text
games/turkic-jam-2026/raw/equipment/equip_slot_weapon_01.png
games/turkic-jam-2026/raw/equipment/equip_slot_clothes_01.png
games/turkic-jam-2026/raw/equipment/equip_slot_tamga_01.png
games/turkic-jam-2026/raw/equipment/equip_slot_tool_01.png
```

## Art Direction

The wayfarer is the first archetype: practical, average, readable, not a named special hero.

Visual language:

- grounded Turkic-nomadic traveler;
- simple cloak, boots, belt, small satchel, staff;
- warm felt/leather/turquoise accent;
- clear silhouette first, costume detail second;
- map sprites must read at `46x56` and `48x58` in the current QA/game scale;
- panel doll may have more detail, but must match the map sprite outfit.

Avoid:

- fantasy wizard, knight, rogue, assassin, palace guard;
- Arabic fairy-tale stereotypes;
- gold palace costume;
- real sacred tamgas or copied clan symbols;
- excessive tiny ornament that becomes noise at runtime scale.

## Source Rule

Production source must be generated bitmap or painted bitmap.

SVG/script/procedural art is not accepted for the hero/equipment subject in this pass.

If background removal leaves visible chroma, checkerboard, or cutout halos, the file is not accepted as final. It may remain as a concept/source only.

## Required Concept Outputs

Save generated source and contact sheet here:

```text
gamedesign/assets/concept/pass_10_generated_wayfarer_equipment/
```

Required contact sheet contents:

- five map sprites at native `128x128`;
- five map sprites at runtime preview `48x58`;
- hero panel at native `160x220`;
- hero panel at current right-panel preview `96x132`;
- four equipment item icons at native `64x64`;
- four equipment item icons at runtime preview `34x34`;
- slot frames at `64x64` and `52x52` if changed.

## Verification

Required checks before Code/GDD review:

```text
all allowed files exist
exact dimensions match contract
RGBA mode
non-empty alpha
pack builder found counts unchanged or improved
desktop L5 screenshot after pack rebuild
```

Expected runtime proof after pack rebuild:

```text
SCENE_VISUAL_QA
hero/equipment panel visible
hero map sprite visible in gameplay composition
no fallback miss labels
```

Do not call Pass 10 final accepted until desktop L5 screenshot/readability review passes.

