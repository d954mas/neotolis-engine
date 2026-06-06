# 34. Asset Production Batch C

Purpose: future visual content library for tile families, aul upgrades, FTUE/death memory, archetype panel dolls, and P1 feedback FX.

Batch C is not required for the first minimal loop, but it prevents the art pipeline from stopping after the first playable. These assets are stable placeholder targets for future systems and later polished art replacement.

## Status

Status: placeholder generation contract.

Generator:

```text
gamedesign/tools/generate_batch_c_assets.py
```

Preview:

```text
gamedesign/assets/concept/production_batch_c/batch_c_runtime_preview.png
```

Manifest:

```text
gamedesign/assets/concept/production_batch_c/batch_c_runtime_manifest.md
```

Generated PNGs are runtime-ready placeholders, not final polished art. Replace them with the same filenames, sizes, alpha rules, and folders.

## Runtime Target Folders

```text
games/turkic-jam-2026/raw/tiles/
games/turkic-jam-2026/raw/cards/
games/turkic-jam-2026/raw/aul/
games/turkic-jam-2026/raw/hero/
games/turkic-jam-2026/raw/fx/
games/turkic-jam-2026/raw/icons/
```

## File List

### Future Tile Families

| File | Target folder | Size | Background | Role |
| --- | --- | --- | --- | --- |
| `tile_well_01.png` | `raw/tiles/` | `128x128` | alpha | help path family |
| `tile_small_camp_01.png` | `raw/tiles/` | `128x128` | alpha | clan shelter family |
| `tile_clan_camp_01.png` | `raw/tiles/` | `128x128` | alpha | upgraded shelter |
| `tile_watchtower_01.png` | `raw/tiles/` | `128x128` | alpha | route/risk control |
| `tile_hunting_trail_01.png` | `raw/tiles/` | `128x128` | alpha | transformed beast danger |
| `tile_pack_01.png` | `raw/tiles/` | `128x128` | alpha | beast pressure / pack |
| `tile_vision_01.png` | `raw/tiles/` | `128x128` | alpha | desert deception |
| `tile_false_path_01.png` | `raw/tiles/` | `128x128` | alpha | desert deception risk |
| `tile_buried_spring_01.png` | `raw/tiles/` | `128x128` | alpha | storm/source transformation |

### Future Card Art

| File | Target folder | Size | Background | Role |
| --- | --- | --- | --- | --- |
| `card_art_oasis_64.png` | `raw/cards/` | `64x64` | alpha | oasis card art |
| `card_art_mirage_64.png` | `raw/cards/` | `64x64` | alpha | mirage card art |
| `card_art_storm_64.png` | `raw/cards/` | `64x64` | alpha | storm card art |
| `card_art_last_tamga_64.png` | `raw/cards/` | `64x64` | alpha | last tamga/memory card |
| `card_art_well_64.png` | `raw/cards/` | `64x64` | alpha | well card art |
| `card_art_watchtower_64.png` | `raw/cards/` | `64x64` | alpha | watchtower card art |

### Aul Upgrade Stages

| File | Target folder | Size | Background | Role |
| --- | --- | --- | --- | --- |
| `aul_tamga_post_01.png` | `raw/aul/` | `128x128` | alpha | clan sign post |
| `aul_stage_01_camp.png` | `raw/aul/` | `256x256` | alpha | starting camp/stand |
| `aul_stage_02_settlement.png` | `raw/aul/` | `256x256` | alpha | small settlement |
| `aul_stage_03_village.png` | `raw/aul/` | `256x256` | alpha | village |
| `aul_stage_04_fortified_aul.png` | `raw/aul/` | `256x256` | alpha | fortified aul |
| `aul_stage_05_steppe_capital.png` | `raw/aul/` | `256x256` | alpha | capital-scale stage |

### Hero Panel Archetypes

| File | Target folder | Size | Background | Role |
| --- | --- | --- | --- | --- |
| `hero_body_panel.png` | `raw/hero/` | `128x192` | alpha | Body archetype panel doll |
| `hero_mind_panel.png` | `raw/hero/` | `128x192` | alpha | Mind archetype panel doll |
| `hero_spirit_panel.png` | `raw/hero/` | `128x192` | alpha | Spirit archetype panel doll |

### FTUE / Death / Memory FX

| File pattern | Target folder | Size | Background | Frames | Role |
| --- | --- | --- | --- | --- | --- |
| `fx_intro_sand_00.png` ... `fx_intro_sand_05.png` | `raw/fx/` | `128x128` | alpha | 6 | first dark-screen sand reveal |
| `fx_fire_glow_00.png` ... `fx_fire_glow_03.png` | `raw/fx/` | `128x128` | alpha | 4 | FTUE campfire glow |
| `fx_last_tamga_spawn_00.png` ... `fx_last_tamga_spawn_05.png` | `raw/fx/` | `128x128` | alpha | 6 | death memory marker |
| `fx_storm_veil_00.png` ... `fx_storm_veil_05.png` | `raw/fx/` | `128x128` | alpha | 6 | lap road rebuild veil |
| `fx_card_reward_00.png` ... `fx_card_reward_03.png` | `raw/fx/` | `128x128` | alpha | 4 | card reward reveal |
| `fx_near_death_00.png` ... `fx_near_death_03.png` | `raw/fx/` | `128x128` | alpha | 4 | low stamina / danger pulse |

### Future Icons

| File | Target folder | Size | Background | Role |
| --- | --- | --- | --- | --- |
| `icon_aul_upgrade_32.png` | `raw/icons/` | `32x32` | alpha | aul upgrade |
| `icon_deck_32.png` | `raw/icons/` | `32x32` | alpha | deck |
| `icon_map_32.png` | `raw/icons/` | `32x32` | alpha | map |
| `icon_memory_32.png` | `raw/icons/` | `32x32` | alpha | memory |
| `icon_warning_32.png` | `raw/icons/` | `32x32` | alpha | warning |
| `icon_card_gain_32.png` | `raw/icons/` | `32x32` | alpha | card gain |

## Acceptance Criteria

1. Tile-family sprites read as future content but do not compete with P0 tiles.
2. Aul stages communicate growth from camp to capital without Arabic palace/gold-fantasy motifs.
3. Archetype panel dolls are variants of the same wayfarer, not unrelated characters.
4. FX are transparent and can be layered over map/UI without hiding gameplay.
5. Future icons read at `24x24`.
6. No real sacred/tribal tamgas are copied; tamga marks remain fictional.
7. All filenames are stable English ids.

## Code Request

Code should not promote Batch C into the active missing report until its target systems are ready.

Recommended order:

1. Keep Batch C raw files available.
2. Register only when a specific feature starts: aul upgrades, future cards, FTUE FX, death memory, or archetype panel.
3. Keep Batch A/B reports separate from Batch C to avoid noisy missing/found totals.

