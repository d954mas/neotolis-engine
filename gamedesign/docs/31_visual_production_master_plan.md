# 31. Visual Production Master Plan

Purpose: single production plan for all visual work needed to move "Song of Tamga" from prototype shapes to an art-complete first playable.

This document coordinates GDD, art/design and code. It does not replace `art_bible_v0_1.md` or `ui_design_system_v0_1.md`; it turns them into batches, deliverables and acceptance gates.

## Current Decision

Approved visual direction:

```text
flat_readable_jam_style
Turkic nomadic identity
warm readable desert
clear layer model
transparent active tile sprites over ground/decor
reusable 9-slice UI kit
```

Approved references:

```text
gamedesign/docs/art_bible_v0_1.md
gamedesign/docs/ui_design_system_v0_1.md
gamedesign/docs/ui_fake_shots_review_01.md
gamedesign/docs/playable_art_integration_brief.md
gamedesign/assets/concept/ui_fake_shot_h_approved_style_reference.png
```

Approved fake-shots are style locks, not open exploration:

```text
ui_fake_shot_h_approved_style_reference.png = approved gameplay UI/style reference
earlier UI fake-shots A-G = exploration/history; use only their documented lessons
fake_shot_gameplay_01.png = map composition reference, not final asset source
```

Next art work must not produce more generic fake-shots unless GDD asks for a specific unresolved screen. The next step is production asset kits that can be packed into the game.

## Non-Negotiable Rules

1. Concept images and fake-shots are references, not runtime assets.
2. Production sprites live under `games/turkic-jam-2026/raw/...`.
3. Active gameplay objects are transparent sprites over ground/base decor.
4. Ground/base decor is separate from active tile sprites.
5. The game must keep fallbacks while sprites are missing.
6. UI must be built from reusable 9-slice pieces, not one-off painted panels.
7. No Arabic-palace, genie, lamp, flying carpet, bazaar or gold-fantasy language.
8. `wolf_track` is internal id; player-facing risk card is `Звериная тропа`.

## Production Folder Contract

```text
games/turkic-jam-2026/raw/ground/
games/turkic-jam-2026/raw/decor/
games/turkic-jam-2026/raw/road/
games/turkic-jam-2026/raw/tiles/
games/turkic-jam-2026/raw/aul/
games/turkic-jam-2026/raw/hero/
games/turkic-jam-2026/raw/cards/
games/turkic-jam-2026/raw/equipment/
games/turkic-jam-2026/raw/icons/
games/turkic-jam-2026/raw/fx/
games/turkic-jam-2026/raw/ui/
```

## Runtime Layer Order

```text
1. ground sand/base
2. quiet base decor for empty buildable cells
3. road_path
4. road_buffer no-build edge
5. active tile object sprites
6. aul objects / hero / current cell marker
7. placement highlight / trigger FX / battle bubble
8. gameplay UI
```

## Full Visual Scope

### UI Design System

P0 production assets:

| Asset id | Purpose |
| --- | --- |
| `ui_panel_dark_9s` | left log, right hero panel, bottom containers |
| `ui_panel_warm_9s` | active/highlight panel |
| `ui_chip_9s` | top HUD resources |
| `ui_card_face_9s` | playable card |
| `ui_card_selected_9s` | selected playable card |
| `ui_card_back_9s` | card back / empty card slot |
| `ui_slot_9s` | equipment/tool slot |
| `ui_tooltip_9s` | compact contextual tooltip |
| `ui_button_9s` | pause/speed/settings buttons |

P0 UI states:

```text
normal run
card hover
card selected
valid placement
invalid placement
current event/check
low stamina warning
```

P1 UI states:

```text
card reward choice
death / Last Tamga result
aul upgrade screen
settings/pause polish
```

### World Ground And Road

P0 assets:

| Asset id | Notes |
| --- | --- |
| `ground_sand_base_01` | base sand |
| `road_straight_ns` | packed road |
| `road_straight_ew` | packed road |
| `road_corner_ne` | road corner |
| `road_corner_es` | road corner |
| `road_corner_sw` | road corner |
| `road_corner_wn` | road corner |
| `road_entry_aul` | aul-to-road exit |
| `road_current_highlight` | warm ring/dust under hero |
| `buffer_edge_stones_01` | no-build edge |
| `buffer_packed_sand_01` | no-build packed sand |
| `buffer_stakes_01` | rare edge detail |
| `buffer_cart_marks_01` | rare edge detail |

Acceptance:

```text
road_path readable before decor
road_buffer visible as no-build, not as a wall
roadside_build and field_build visually distinct
```

### Empty Buildable Decor

P0 assets:

| Asset id | Notes |
| --- | --- |
| `decor_dune_01` | quiet sand shape |
| `decor_stones_01` | not confused with Tamga stone |
| `decor_dry_grass_01` | lower/smaller than Saxaul |
| `decor_tracks_01` | old tracks, not wolf paw |
| `decor_bones_01` | small fragments, not full skeleton |
| `decor_cracks_01` | dry clay cracks |

Acceptance:

```text
no empty buildable cells
decor is quieter than active tiles
empty cells do not look occupied
```

### Active Tile Sprites

P0 assets:

| Internal id | Sprite id | Player-facing name | Notes |
| --- | --- | --- | --- |
| `saxaul` | `tile_saxaul_01` | Саксаул | low shrub/brushwood, first help |
| `yurt` | `tile_yurt_01` | Юрта | field economy/support |
| `tamga_stone` | `tile_tamga_stone_01` | Камень Тамги | memory/wisdom |
| `wolf_track` | `tile_wolf_track_01` | Звериная тропа | risk trail/marks, not wolf body/totem |

P1 assets:

| Internal id | Sprite id | Player-facing name |
| --- | --- | --- |
| `oasis` | `tile_oasis_01` | Оазис |
| `mirage` | `tile_mirage_01` | Мираж |
| `storm` | `tile_storm_01` | Песчаная буря |
| `last_tamga` | `tile_last_tamga_01` | Последняя Тамга |

P2 future tile families:

```text
well
small_camp
clan_camp
watchtower
hunting_trail
pack
vision
false_path
buried_spring
```

### Cards

P0 requirement:

Cards use UI surfaces plus tile art, not separate fully painted one-off cards.

Card assets:

```text
ui_card_face_9s
ui_card_selected_9s
ui_card_back_9s
card_badge_count
card_placement_roadside
card_placement_field
card_placement_special
```

P0 card content:

```text
Саксаул
Юрта
Камень Тамги
Звериная тропа
```

P1:

```text
Оазис
Мираж
Песчаная буря
Последняя Тамга / memory card if needed
```

### Hero And Characters

P0 hero assets:

| Asset id | Size | Frames | Notes |
| --- | --- | --- | --- |
| `hero_wayfarer_idle_s` | 64x64 | 2 | road idle |
| `hero_wayfarer_walk_s` | 64x64 | 4 | south |
| `hero_wayfarer_walk_e` | 64x64 | 4 | east |
| `hero_wayfarer_walk_n` | 64x64 | 4 | north |
| `hero_wayfarer_walk_w` | 64x64 | 4 | west |
| `hero_wayfarer_panel` | 128x192 | 1 | right panel doll |

P1 archetype panel variants:

```text
hero_body_panel
hero_mind_panel
hero_spirit_panel
```

Acceptance:

```text
hero readable at gameplay scale
not a yellow dot anymore
panel doll supports equipment overlays later
```

### Equipment, Inventory And Hero Panel

P0 equipment is mostly UI affordance, not a full inventory system.

P0 assets:

| Asset id | Purpose |
| --- | --- |
| `equip_slot_weapon_01` | slot icon/silhouette |
| `equip_slot_clothes_01` | slot icon/silhouette |
| `equip_slot_tamga_01` | slot icon/silhouette |
| `equip_slot_tool_01` | slot icon/silhouette |
| `equip_weapon_staff_01` | simple first item |
| `equip_clothes_cloak_01` | simple first item |
| `equip_tamga_charm_01` | memory/tamga item |
| `equip_tool_satchel_01` | supplies/tool item |

P1 assets:

```text
inventory_panel_9s
inventory_cell_9s
item_bone_whistle
item_water_skin
item_woven_belt
item_tamga_shard
```

Rule:

```text
Normal gameplay HUD shows hero doll + stats + 3-4 equipment slots.
Large storage/backpack lives in post-run/inter-run screen, not normal run.
Relic and Last Tamga are hidden/locked until systems exist.
```

### Aul

P0 assets:

| Asset id | Notes |
| --- | --- |
| `aul_ground_2x2` | central ground |
| `aul_yurt_small_01` | yurt |
| `aul_yurt_small_02` | yurt variant |
| `aul_fire_01` | 2-3 frame fire |
| `aul_tamga_post_01` | clan sign |

P1 upgrade stages:

```text
aul_stage_01_camp
aul_stage_02_settlement
aul_stage_03_village
aul_stage_04_fortified_aul
aul_stage_05_steppe_capital
```

### Icons

P0 icons:

```text
icon_stamina
icon_supplies
icon_wisdom
icon_glory
icon_circle
icon_day
icon_body
icon_mind
icon_spirit
icon_last_tamga
icon_settings
icon_speed
```

P1 icons:

```text
icon_aul_upgrade
icon_deck
icon_map
icon_memory
icon_warning
icon_card_gain
```

### FX And Feedback

P0 assets:

| Asset id | Frames | Purpose |
| --- | --- | --- |
| `fx_dust_step` | 4 | hero movement |
| `fx_tile_placed` | 4 | card placement |
| `fx_tile_trigger` | 4 | active tile effect |
| `fx_gain_popup` | 3 | small resource gain |
| `fx_invalid_cell` | 2 | invalid placement |

P1 assets:

```text
fx_last_tamga_spawn
fx_storm_veil
fx_card_reward
fx_near_death
```

## Batch Plan

### Batch 0: Production System Contract

Owner: GDD + Code.

Deliverables:

```text
raw folder contract
optional atlas registration
sprite fallback rendering
asset id list
one manifest/checklist for missing assets
```

### Batch 1: Map Readability

Owner: Art + Code.

Deliverables:

```text
ground_sand_base_01
decor_dune_01 / stones / dry_grass / tracks / bones / cracks
road_path set
road_buffer set
aul P0
hero placeholder
tile_saxaul_01
```

### Batch 2: Playable Cards And Hero Panel

Owner: UI designer + Code.

Deliverables:

```text
9-slice UI kit P0
card surfaces and states
HUD icons P0
hero panel doll + equipment slots
first card visuals
```

### Batch 3: First Choice And Risks

Owner: Art + GDD + Code.

Deliverables:

```text
tile_yurt_01
tile_tamga_stone_01
tile_wolf_track_01
card art for Юрта / Камень Тамги / Звериная тропа
trigger marker for wolf track
```

### Batch 4: FTUE And Death Memory

Owner: Art + Narrative + Code.

Deliverables:

```text
intro reveal visuals
fire animation
first traveler exit
last_tamga sprite
death result panel
memory pickup FX
```

### Batch 5: Future Content Library

Owner: Art + GDD.

Deliverables:

```text
oasis / mirage / storm
future tile family concepts
equipment items
inventory cells
aul upgrade stages
```

## Review Gates

Every art batch must pass:

1. Reads at 64x64 gameplay scale.
2. Works on `sand_base`.
3. Does not confuse decor with active tile.
4. Has transparent background if active object.
5. Has stable English asset id.
6. Fits palette and no-forbidden-motifs rules.
7. Can be packed by builder without runtime parsers.

Every UI batch must pass:

1. Text fits Russian strings.
2. UI does not reduce map readability.
3. Cards look playable, not inventory plaques.
4. Hero panel explains stats/equipment without opening inventory.
5. Reuses 9-slice pieces.

## Immediate Tasks

Art thread:

```text
Use approved fake-shots as visual reference.
Generate production-ready Batch 1 and Batch 2 source PNGs, not more fake-shots.
Use transparent active sprites.
Provide exact crop grid/sprite ids.
Create review sheet and per-asset notes.
```

Code thread:

```text
Finish optional atlas/fallback pipeline.
Create raw folder contract.
Wire first sprites into runtime if files exist.
Report missing asset ids.
Keep game playable without art.
```

GDD thread:

```text
Review generated art against gates.
Approve/reject each sprite.
Keep FROM_GDD/FROM_CODE current.
Update site only after decisions are stable.
```
