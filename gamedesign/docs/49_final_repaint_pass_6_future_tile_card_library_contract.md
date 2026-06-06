# 49. Final repaint pass 6 - future tile/card library contract

Status: active art contract.

Owner: Art thread.

Purpose: replace the remaining obvious placeholder PNGs for future tile families, future card art, and utility icons without changing ids or runtime filenames.

This pass is production asset preparation, not fake-shot work. These assets may not all appear in the first playable yet, but they should be ready for later config/gameplay use.

## Rules

1. Replace only exact files listed below.
2. Do not rename files.
3. Do not add new ids.
4. Do not touch Pass 1-5 candidate-final PNGs.
5. Keep exact dimensions and alpha rules.
6. Every RGBA sprite must have visible pixels and useful transparent bounds.
7. Create a contact sheet in `gamedesign/assets/concept/final_repaint_pass_6_future_tile_card_library/`.
8. Report exact changed files, dimensions, what remains placeholder, ready-for-programmer status, and screenshot QA needs.

## Required future card art

All files are `64x64`, RGBA.

```text
raw/cards/card_art_oasis_64.png
raw/cards/card_art_mirage_64.png
raw/cards/card_art_storm_64.png
raw/cards/card_art_last_tamga_64.png
raw/cards/card_art_well_64.png
raw/cards/card_art_watchtower_64.png
```

Style notes:

- Oasis: rare strong help, but no palm resort fantasy.
- Mirage: readable desert illusion, not generic magic swirl.
- Storm: sand/wind pressure, not lightning fantasy.
- Last Tamga: solemn fictional memory marker, not copied real sacred sign.
- Well: practical survival object, stronger than saxaul but smaller than oasis.
- Watchtower: steppe lookout/watch post, no medieval stone castle tower.

## Required future tile sprites

All files are `128x128`, RGBA.

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

Style notes:

- These are active tile objects on transparent backgrounds, placed over ground/base decor.
- They must be more readable than quiet decor cells.
- Hunting trail should not become a giant animal/totem; use path marks, stakes, tracks, or small hunting sign.
- Vision/false path can be slightly fairy-tale, but keep them grounded in sand, wind, traces, and memory.
- Buried spring should read as hidden/partly covered resource, not a full oasis.

## Required utility icons

All files are `32x32`, RGBA.

```text
raw/icons/icon_aul_upgrade_32.png
raw/icons/icon_card_gain_32.png
raw/icons/icon_deck_32.png
raw/icons/icon_map_32.png
raw/icons/icon_memory_32.png
raw/icons/icon_settings_32.png
raw/icons/icon_warning_32.png
```

Style notes:

- Must read at 24px in HUD/action UI.
- Use the same warm felt/sand/teal/tamga visual language as Pass 1-5.
- Avoid thin lines. Prefer bold silhouettes.
- Settings can be a simple tool/knotted toggle motif, not a modern gear if a better readable icon exists.

## Optional review target

These are present but not activated in this contract:

```text
raw/hero/hero_body_panel.png
raw/hero/hero_mind_panel.png
raw/hero/hero_spirit_panel.png
```

Leave them untouched unless GDD opens a separate archetype/panel pass.

## Delivery checklist

```text
Exact listed files replaced:
Dimensions/mode verified:
Non-empty alpha verified:
Contact sheet created:
Pass 1-5 untouched:
Ready for programmer:
Runtime QA needs:
```

## Cultural guardrails

- No Aladdin/palace/genie/flying carpet/bazaar/gold fantasy.
- No copied real sacred tamgas.
- No generic magic rune language.
- Keep the feeling: Turkic-nomadic game-fairy-tale, road, aul, fire, trace, memory, survival in desert/steppe.
