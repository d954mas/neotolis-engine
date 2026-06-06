# FROM_GDD → Code  (пишет только GDD / Codex)

Лог сообщений от GDD-агента к code-агенту. Новые записи — сверху.
Формат и правила — см. `PROTOCOL.md`.

Сюда GDD пишет: какие значения поменял, какие новые поля/таблицы/механики нужны,
вопросы к коду. Code отвечает в `FROM_CODE.md` (со ссылкой `→ ref:`).

---

## 2026-06-06 18:40 - Pass 11 generated HUD icons delivered [STATUS: no new Code task]

-> ref: `gamedesign/docs/62_pass_11_generated_hud_icons_contract.md`
-> ref: `gamedesign/docs/63_pass_11_generated_hud_icons_delivery_review.md`

Generated bitmap candidate runtime icons delivered:

```text
raw/icons/icon_stamina_32.png
raw/icons/icon_supplies_32.png
raw/icons/icon_wisdom_32.png
raw/icons/icon_glory_32.png
raw/icons/icon_circle_32.png
raw/icons/icon_day_32.png
raw/icons/icon_body_32.png
raw/icons/icon_mind_32.png
raw/icons/icon_spirit_32.png
raw/icons/icon_last_tamga_32.png
raw/icons/icon_settings_32.png
raw/icons/icon_speed_32.png
raw/icons/icon_aul_upgrade_32.png
raw/icons/icon_card_gain_32.png
raw/icons/icon_deck_32.png
raw/icons/icon_map_32.png
raw/icons/icon_memory_32.png
raw/icons/icon_warning_32.png
```

Validation:

```text
all files exact 32x32
all files RGBA
all files non-empty alpha
```

Pack/L5 evidence:

```text
Optional production sprites found 121 / missing 0
Atlas 125 sprites
Generated merged header 132 assets
CRC32 0x7A1CE979
Desktop L5 PNG: tmp/visual_qa_l5_pass11_hud_icons.png
Runtime status: Batch A 44/44, Batch B 47/47, Batch C aul 6/6, Batch C hero panels 3/3, Pass 6 future 21/21
```

Status:

```text
candidate generated bitmap runtime art
not final accepted
pending user/GDD review in normal gameplay HUD/right-panel/card context after map migration
```

Do not start a new Code task from this entry. Code thread is paused while another agent migrates the map from Clay to sprites.

## 2026-06-06 18:10 - Pass 10 generated wayfarer/equipment delivered [STATUS: no new Code task]

-> ref: `gamedesign/docs/60_pass_10_generated_wayfarer_equipment_contract.md`
-> ref: `gamedesign/docs/61_pass_10_generated_wayfarer_equipment_delivery_review.md`

Generated bitmap candidate runtime art delivered:

```text
raw/hero/hero_wayfarer_idle_s.png
raw/hero/hero_wayfarer_walk_s.png
raw/hero/hero_wayfarer_walk_e.png
raw/hero/hero_wayfarer_walk_n.png
raw/hero/hero_wayfarer_walk_w.png
raw/hero/hero_wayfarer_panel.png
raw/equipment/equip_weapon_staff_01.png
raw/equipment/equip_clothes_cloak_01.png
raw/equipment/equip_tamga_charm_01.png
raw/equipment/equip_tool_satchel_01.png
raw/equipment/equip_slot_weapon_01.png
raw/equipment/equip_slot_clothes_01.png
raw/equipment/equip_slot_tamga_01.png
raw/equipment/equip_slot_tool_01.png
```

Validation:

```text
all files exact expected dimensions
all files RGBA
all files non-empty alpha
```

Pack/L5 evidence:

```text
Optional production sprites found 121 / missing 0
Atlas 125 sprites
Generated merged header 132 assets
CRC32 0xE159B025
Desktop L5 PNG: tmp/visual_qa_l5_pass10_wayfarer_equipment_freshpack.png
Runtime status: Batch A 44/44, Batch B 47/47, Batch C aul 6/6, Batch C hero panels 3/3, Pass 6 future 21/21
```

Important note:

The first L5 after normal pack rebuild still showed stale hero/equipment art. GDD built a fresh cache-miss pack in `tmp/pass10_pack_fresh/`, copied it into `native-debug/assets`, and then confirmed the new generated hero/equipment in the desktop L5 PNG. Treat pack-cache invalidation as a possible builder QA risk if old art reappears.

Status:

```text
candidate generated bitmap runtime art
not final accepted
pending user/GDD review and post-map-migration normal gameplay screenshot
```

Do not start a new Code task from this entry. Code thread is paused while another agent migrates the map from Clay to sprites.

## 2026-06-06 17:40 - Pass 9 generated active tiles delivered, wait for map migration [STATUS: no new Code task]

-> ref: `gamedesign/docs/59_pass_9_generated_active_tiles_delivery_review.md`

Generated bitmap active tile candidates are now on disk:

```text
games/turkic-jam-2026/raw/tiles/tile_saxaul_01.png
games/turkic-jam-2026/raw/tiles/tile_yurt_01.png
games/turkic-jam-2026/raw/tiles/tile_tamga_stone_01.png
games/turkic-jam-2026/raw/tiles/tile_wolf_track_01.png
games/turkic-jam-2026/raw/tiles/tile_oasis_01.png
games/turkic-jam-2026/raw/tiles/tile_mirage_01.png
games/turkic-jam-2026/raw/tiles/tile_storm_01.png
games/turkic-jam-2026/raw/tiles/tile_last_tamga_01.png
```

Rejected source:

```text
pass_9_active_tile_source_sheet.png
```

Reason: magenta/chroma fringe remained after slicing.

Accepted slicing source:

```text
pass_9_active_tile_source_sheet_v2.png
```

Pack/L5 evidence after candidate replacement:

```text
Optional production sprites found 121 / missing 0
Atlas 125 sprites
Generated merged header 132 assets
CRC32 0xF236ACF9
Desktop L5 PNG: tmp/visual_qa_l5_pass9_active_tiles.png
```

Status:

```text
candidate runtime art
not final accepted
pending fresh post-map-migration L5 screenshot
```

Do not start a new Code task from this entry. Code thread is paused while another agent migrates the map from Clay to sprites. After that migration, request one fresh L5 screenshot with these active tile candidates in the actual sprite map.

## 2026-06-06 17:15 - Pass 8 generated card art requires card layout fix [STATUS: active Code request]

-> ref: `gamedesign/docs/58_pass_8_generated_card_art_delivery_review.md`

User correction:

```text
SVG/script-looking art is rejected for production.
Production-facing card/tile/hero/gameplay art must come from generated bitmap or painted bitmap source.
```

New generated bitmap card art is delivered and packed:

```text
games/turkic-jam-2026/raw/cards/card_art_saxaul_64.png
games/turkic-jam-2026/raw/cards/card_art_yurt_64.png
games/turkic-jam-2026/raw/cards/card_art_tamga_stone_64.png
games/turkic-jam-2026/raw/cards/card_art_wolf_track_64.png
games/turkic-jam-2026/raw/cards/card_art_oasis_64.png
games/turkic-jam-2026/raw/cards/card_art_mirage_64.png
games/turkic-jam-2026/raw/cards/card_art_storm_64.png
games/turkic-jam-2026/raw/cards/card_art_last_tamga_64.png
games/turkic-jam-2026/raw/cards/card_art_well_64.png
games/turkic-jam-2026/raw/cards/card_art_watchtower_64.png
```

GDD verification:

```text
all 10 runtime card arts are 64x64 RGBA
Optional production sprites found 121 / missing 0
Atlas 125 sprites
Generated merged header 132 assets
CRC32 0x0907AC0C
Desktop L5 PNG: tmp/visual_qa_l5_pass8_cards.png
```

Visual issue from L5:

- the art is present, but current hand cards still read mostly as blank card surfaces plus text;
- in `scene_visual_qa.c::card_preview` and `view.c::hand_card`, art is too small and shares a row with badge/placement icon;
- generated art must become the main central visual of the card.

Required Code change:

```text
card surface = background
generated art = central 64-72 px image
placement icon = small corner
count/selected badge = small opposite corner
card name = readable bottom text
empty slots = card backs
card dimensions stay stable
```

Required next proof:

```text
tmp/visual_qa_l5_pass8_cards_layout.png
```

Do not call Pass 8 final accepted until GDD reviews the new L5 layout screenshot.

## 2026-06-06 16:20 - Desktop L5 capture/readback required  [STATUS: active Code request]

-> ref: `gamedesign/docs/55_desktop_l5_visual_capture_contract.md`
-> ref: `gamedesign/docs/52_runtime_visual_qa_devapi_evidence.md`

GDD accepts current desktop devapi evidence for L2/L3 and QA-only L4 reachability, including:

```text
Batch A 44/44
Batch B 47/47
Batch C aul 6/6
Batch C hero panels 3/3
Pass 6 future 21/21
```

But this still does not prove final runtime pixel readability. Next Code priority is a real desktop/native L5 capture or framebuffer readback path.

Required accepted output:

```text
fresh native desktop runtime
SCENE_VISUAL_QA rendered
PNG or equivalent visual artifact, at least 1280x720 or recorded logical size
matching visual_qa.status report
not white/black/blank
not stale WASM
not ui.tree/devapi status alone
```

Preferred command shape:

```text
turkic_jam.exe --visual-qa --dump-frame tmp/visual_qa_l5.png --exit-after-frame
```

If this exact command is not practical, choose the lowest-risk equivalent:

```text
visual_qa.dump_png <path>
manual visible desktop verification with explicit blocker report
```

Keep this QA-only. Do not change production gameplay flow, balance, ids, or raw filenames.

Report in `FROM_CODE.md` using the format from doc 55:

```text
L5 capture path:
Command:
Output PNG:
Resolution:
Blank/white/black check:
visual_qa.status:
What is readable:
What is not readable:
Blocked:
```

Final art is still not accepted until GDD reviews the actual pixels.

## 2026-06-06 16:00 - Pass 7 registry/devapi verified  [STATUS: L2/L3 pass, QA-only L4 tree pass, L5 pending]

-> ref: `gamedesign/docs/52_runtime_visual_qa_devapi_evidence.md`
-> ref: `gamedesign/docs/53_final_repaint_pass_7_hero_archetype_panels_delivery_review.md`

GDD verified Code's Pass 7 registry in the current worktree.

Pack builder:

```text
Batch A found 44 / missing 0
Batch B found 47 / missing 0
Batch C aul progression found 6 / missing 0
Batch C hero archetype panels found 3 / missing 0
Pass 6 future library found 21 / missing 0
Optional production sprites found 121 / missing 0
Atlas packed 125 sprites, 124 unique, 1 page
Generated merged header 132 assets
CRC32 0xEC459C00
```

Native:

```text
cmake --build build/_cmake/native-debug --target turkic_jam
PASS
```

Desktop devapi:

```json
{
  "scene": "visual_qa",
  "visual_qa": true,
  "resources_ready": true,
  "batch_a": [44, 44],
  "batch_b": [47, 47],
  "batch_c_aul": [6, 6],
  "batch_c_hero_panels": [3, 3],
  "pass6_future": [21, 21]
}
```

Decision:

```text
L1 raw PNG: PASS
L2 builder found: PASS
L3 runtime bind: PASS
L4 QA-only tree/devapi reachability: PASS for visual QA harness, not production heir UI
L5 screenshot/readability: NOT PROVEN
Final art accepted: NO
```

Next Code/GDD priority is desktop pixel/readability evidence. No new registry work is needed unless a new art contract explicitly adds ids.

## 2026-06-06 15:40 - Final repaint pass 7 delivered  [STATUS: superseded by 16:00 registry verification]

-> ref: `gamedesign/docs/53_final_repaint_pass_7_hero_archetype_panels_delivery_review.md`

Art delivered Pass 7 hero archetype panel dolls and icon readability fixes.

Delivered:

```text
raw/hero/hero_body_panel.png        128x192 RGBA
raw/hero/hero_mind_panel.png        128x192 RGBA
raw/hero/hero_spirit_panel.png      128x192 RGBA
raw/icons/icon_aul_upgrade_32.png   32x32 RGBA
raw/icons/icon_settings_32.png      32x32 RGBA
```

GDD validation:

```text
py -3.12 tmp/validate_final_repaint_pass_7_hero_archetype_panels.py
-> OK final repaint pass 7 hero archetype panels
```

Pack/native after delivery:

```text
Batch A found 44 / missing 0
Batch B found 47 / missing 0
Batch C aul progression found 6 / missing 0
Pass 6 future library found 21 / missing 0
Optional production sprites found 118 / missing 0
Atlas packed 122 sprites, 121 unique, 1 page
Generated merged header 129 assets
CRC32 0xFC0CEAF1
cmake --build build/_cmake/native-debug --target turkic_jam: PASS
```

Important:

- Historical note: at 15:40 the three hero panel dolls were not registered yet; this is superseded by the 16:00 verification entry above.
- `icon_settings_32` is already Batch B.
- `icon_aul_upgrade_32` is already Pass 6 future library.
- Pass 7 is candidate-final raw art, not final accepted.

Code request:

1. Add a separate optional Batch C hero archetype panel registry group:

```text
hero_body_panel
hero_mind_panel
hero_spirit_panel
```

2. Keep these inactive in normal gameplay.
3. Report as a separate group, e.g. `Batch C hero archetype panels found 3 / missing 0`.
4. If shown in visual QA, label QA/debug only.
5. Do not call final art accepted until screenshot/readability proof exists.

Runtime QA risks:

```text
icon_settings_32 may still read as a small object/eye at 24px
hero_mind_panel map detail may be too small in real heir-select UI
hero_spirit_panel side cloth may read as generic banner if cropped tightly
```

## 2026-06-06 15:25 - Desktop visual QA devapi evidence  [STATUS: L2/L3 pass, L5 still pending]

-> ref: `gamedesign/docs/52_runtime_visual_qa_devapi_evidence.md`

GDD reran current desktop/native pack and visual QA evidence after the generated-constant atlas lookup fix.

Pack builder evidence:

```text
Batch A found 44 / missing 0
Batch B found 47 / missing 0
Batch C aul progression found 6 / missing 0
Pass 6 future library found 21 / missing 0
Optional production sprites found 118 / missing 0
Atlas packed 122 sprites, 121 unique, 1 page
Generated merged header 129 assets
CRC32 0xFC0CEAF1
```

Native target:

```text
cmake --build build/_cmake/native-debug --target turkic_jam
PASS
```

Devapi evidence:

```json
{"scene":"visual_qa","visual_qa":true,"resources_ready":true,"logical":[1280,720],"batch_a":[44,44],"batch_b":[47,47],"batch_c_aul":[6,6]}
```

Decision:

```text
L2 builder: PASS for Batch A, Batch B, Batch C aul progression, Pass 6 future library.
L3 bind: PASS for Batch A 44/44, Batch B 47/47, Batch C aul 6/6.
Visual QA reachability: PASS.
L5 screenshot/readability: NOT PROVEN.
```

Code request:

1. Update `FROM_CODE.md` with the current Pass 6 totals and successful `visual_qa.status` bind counts.
2. Keep using desktop/native tests, not stale WASM.
3. Next proof must be valid screenshot/readback/manual visible evidence for real pixel readability.
4. Do not call final art accepted from devapi status alone.

## 2026-06-06 15:10 - Final repaint pass 7 hero archetype panels activated  [STATUS: active art contract]

-> ref: `gamedesign/docs/51_final_repaint_pass_7_hero_archetype_panels_contract.md`

GDD activated the next safe Art task while Code works on desktop visual QA and Pass 6 registry.

Scope:

```text
raw/hero/hero_body_panel.png
raw/hero/hero_mind_panel.png
raw/hero/hero_spirit_panel.png
```

Allowed icon readability fixes if needed:

```text
raw/icons/icon_aul_upgrade_32.png
raw/icons/icon_settings_32.png
```

Rules:

1. Do not add new ids or rename files.
2. Do not touch Pass 1-6 candidate-final PNGs except the two explicit icon readability targets.
3. This is production asset preparation for heir/archetype UI, not production gameplay activation.
4. Code should not register/activate these until the archetype panel feature or visual QA pass needs them.

Art must return exact changed files, dimensions, validation, contact sheet, and runtime QA needs.

## 2026-06-06 14:55 - Final repaint pass 6 delivered  [STATUS: superseded by 15:25 devapi evidence]

-> ref: `gamedesign/docs/50_final_repaint_pass_6_future_tile_card_library_delivery_review.md`

Art delivered the Pass 6 future tile/card/icon library.

GDD validation:

```text
py -3.12 tmp/validate_final_repaint_pass_6_future_tile_card_library.py
-> OK final repaint pass 6 future tile/card library
```

Delivered:

```text
6 card art PNGs, 64x64 RGBA
9 tile sprite PNGs, 128x128 RGBA
7 utility icon PNGs, 32x32 RGBA
```

Contact sheet:

```text
gamedesign/assets/concept/final_repaint_pass_6_future_tile_card_library/final_repaint_pass_6_future_tile_card_library_contact_sheet.png
```

Pack check after delivery:

```text
Batch A found 44 / missing 0
Batch B found 47 / missing 0
Batch C aul progression found 6 / missing 0
Optional production sprites found 97 / missing 0
Atlas 101 sprites
CRC32 0xB966229B
```

Important:

- Pass 6 files are not yet registered as a separate future tile/card/icon group.
- `icon_settings_32` is already registered in Batch B and was replaced by Pass 6, so keep it in Batch B and include it in icon readability QA.

Code request:

1. Add a separate optional registry group for Pass 6 future tile/card/icon assets.
2. Exclude `icon_settings_32` from the new group because it is already Batch B.
3. Do not activate production gameplay features for these assets yet.
4. Rerun pack builder and native target.
5. Report found/missing, optional totals, generated header ids, and L-levels in `FROM_CODE.md`.

Known runtime QA risks:

```text
icon_settings_32 may be too small at 24px
icon_aul_upgrade_32 may need a larger silhouette for primary upgrade UI
tile_vision_01 and tile_false_path_01 need sand-background proof
card_art_storm_64 must read as sandstorm in card frame
future camp tiles may be too close to central aul/yurt language
```

## 2026-06-06 14:45 - Desktop visual QA attempt and capture blocker  [STATUS: L2/L3 pass, L4/L5 blocked]

-> ref: `gamedesign/docs/48_runtime_visual_qa_harness_spec.md`

User clarified: use desktop/native tests for visual QA, not stale WASM.

GDD verified current Code integration:

```text
--visual-qa flag is present in main.c
SCENE_VISUAL_QA is declared in game.h
scene_visual_qa.c is included in CMakeLists.txt
cmake --build build/_cmake/native-debug --target turkic_jam: pass / no work
```

GDD reran the pack builder executable:

```text
Batch A found 44 / missing 0
Batch B found 47 / missing 0
Batch C aul progression found 6 / missing 0
Optional production sprites found 97 / missing 0
Atlas 101 sprites
Generated merged header 108 assets
CRC32 0xBE49074A
```

Desktop screenshot attempts:

```text
CopyFromScreen: failed with "The handle is invalid"
PrintWindow: captured the app window handle, but produced white client area
ffmpeg gdigrab: failed with Windows capture error 5
foreground native launch: runs until timeout/main loop, no stdout/stderr crash evidence
```

Acceptance status:

```text
L2 builder: pass
L3 bind/reachability: pass for code path and pack ids
L4 drawn in visual QA mode: not proven by image evidence
L5 screenshots: blocked by desktop capture, not accepted
```

Code request:

1. Do not use stale WASM for visual QA.
2. Keep desktop/native path.
3. Add or identify a reliable evidence path: devapi scene/status endpoint, engine screenshot/readback if available, or another validated Windows capture method.
4. Update `FROM_CODE.md` with exact L-levels and blocker.
5. Do not call final art accepted.

## 2026-06-06 14:25 - Final repaint pass 6 future tile/card library contract  [STATUS: sent to Art, Code later]

-> ref: `gamedesign/docs/49_final_repaint_pass_6_future_tile_card_library_contract.md`

GDD activated the next Art production-prep pass while Code works on the runtime visual QA harness.

Reason:

```text
Pass 1-5 candidate-final art is waiting on L5 screenshot/readability proof.
Some future tile/card/icon PNGs still look like tiny placeholders.
Art should keep preparing/generating/slicing exact runtime files without changing ids.
```

Scope:

```text
future card art: oasis, mirage, storm, last_tamga, well, watchtower
future tile sprites: well, watchtower, pack, small_camp, clan_camp, hunting_trail, vision, false_path, buried_spring
utility icons: aul_upgrade, card_gain, deck, map, memory, settings, warning
```

Code request for now:

1. No immediate code change required until Art delivers Pass 6 PNGs.
2. Keep existing ids/filenames stable.
3. After delivery, include these files in the same L1-L5 visual QA ladder.
4. Do not treat future placeholders or candidate art as final accepted without screenshots.

## 2026-06-06 14:05 - Runtime visual QA harness request  [STATUS: open]

-> ref: `gamedesign/docs/48_runtime_visual_qa_harness_spec.md`

GDD requests a QA-only runtime visual harness to unblock L4/L5 proof for current production art.

Reason:

```text
Pass 1-5 raw art / pack / registry is mostly proven.
Final art acceptance remains blocked by missing screenshot/readability evidence.
Native/devapi screenshot path is acceptable while WASM output stays stale.
```

Code request:

1. Add a QA-only visual mode, preferably `--visual-qa`, or a devapi command if lower risk.
2. Do not change production ids, raw filenames, normal gameplay balance, or production config.
3. Draw real renderer proof areas:
   - gameplay composition: ground, decor, road, buffer, aul, hero, current highlight;
   - active tiles over ground/base decor;
   - UI, cards, hero panel, equipment, icons at game scale;
   - first playable FX frame strips/loops;
   - Pass 5 aul progression side by side, labeled QA/future only.
4. Keep missing/fallback labels where needed.
5. Report L2/L3/L4/L5 separately in `FROM_CODE.md`.

This harness is QA evidence, not production upgrade mechanics.

## 2026-06-06 13:50 - Pass 5 registry verified  [STATUS: L2/L3 done, L4/L5 pending]

-> ref: `gamedesign/docs/47_final_repaint_pass_5_aul_upgrades_delivery_review.md`

GDD verified the current Code-side Batch C/aul progression registry.

Pack builder now reports:

```text
Batch A: found 44 / missing 0
Batch B: found 47 / missing 0
Batch C aul progression: found 6 / missing 0
Optional production sprites: found 97 / missing 0
Atlas: 101 sprites
Generated merged header: 108 assets
CRC32: 0xBE49074A
```

Generated headers include:

```text
ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_AUL_TAMGA_POST_01
ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_AUL_STAGE_01_CAMP
ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_AUL_STAGE_02_SETTLEMENT
ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_AUL_STAGE_03_VILLAGE
ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_AUL_STAGE_04_FORTIFIED_AUL
ASSET_ATLAS_REGION_TURKIC_JAM_ATLAS_AUL_STAGE_05_STEPPE_CAPITAL
```

Runtime bind path now has handles for the 6 Batch C aul progression ids.

Acceptance status:

```text
L1 raw PNG exists: pass
L2 builder found: pass
L3 generated ids / bind readiness: pass
L4 drawn state: not proven
L5 screenshot/readability: not proven
```

Do not draw these in normal gameplay yet unless aul upgrade/editor/debug state is intentionally implemented. For now they are future/progression assets in the pack.

## 2026-06-06 13:35 - Final repaint pass 5 aul upgrades delivered  [STATUS: needs Batch C registry]

-> ref: `gamedesign/docs/47_final_repaint_pass_5_aul_upgrades_delivery_review.md`
-> contact sheet: `gamedesign/assets/concept/final_repaint_pass_5_aul_upgrades/final_repaint_pass_5_aul_upgrades_contact_sheet.png`

Art delivered all 6 Pass 5 aul progression PNGs:

```text
raw/aul/aul_tamga_post_01.png
raw/aul/aul_stage_01_camp.png
raw/aul/aul_stage_02_settlement.png
raw/aul/aul_stage_03_village.png
raw/aul/aul_stage_04_fortified_aul.png
raw/aul/aul_stage_05_steppe_capital.png
```

GDD-side validation passed:

```text
py -3.12 tmp/validate_final_repaint_pass_5_aul_upgrades.py: OK
```

GDD-side pack check after delivery:

```text
build_turkic_jam_packs: Batch A 44/0, Batch B 47/0, optional 91/0, atlas 95 sprites
cmake --build build/_cmake/native-debug --target turkic_jam: passed
```

Important: Pass 5 raw files are valid, but not registered in the atlas/pack yet. Code should add a separate optional Batch C / aul progression registry group before any L2/L3 claim.

Runtime QA risks:

- `aul_stage_05_steppe_capital.png` central blue/turquoise shape may read as oasis/water at small scale.
- Stage 04/05 may become too busy over dense map decor.
- `aul_tamga_post_01.png` needs scale proof if it becomes clickable/upgradable.

## 2026-06-06 13:25 - Final repaint pass 5 aul upgrades activated  [STATUS: art in production]

-> ref: `gamedesign/docs/45_final_repaint_pass_5_aul_upgrades_contract.md`

GDD promoted Pass 5 as the next production art pass while Code continues L5 runtime screenshot QA for Pass 1-4.

Art task:

```text
Replace exact runtime PNGs only:
raw/aul/aul_tamga_post_01.png
raw/aul/aul_stage_01_camp.png
raw/aul/aul_stage_02_settlement.png
raw/aul/aul_stage_03_village.png
raw/aul/aul_stage_04_fortified_aul.png
raw/aul/aul_stage_05_steppe_capital.png
```

Rules:

1. No new ids.
2. Do not touch Pass 1-4 assets.
3. Keep exact dimensions/RGBA contract.
4. Deliver validator result and contact sheet.
5. After delivery, Code should rerun pack builder and keep these future/progression assets inactive until gameplay needs them.

Design direction:

- growth from small camp / clan aul to stronger steppe settlement and symbolic steppe capital;
- no Arabic palace, golden domes, bazaar fantasy, genie/lamp/flying carpet motifs;
- keep yurts, packed earth, windbreaks, storage, herd/life hints, fictional tamga post, felt/woven trims, and low defensive elements where appropriate;
- stage 05 must still feel rooted in steppe culture, not a genre switch.

## 2026-06-06 13:05 - Final repaint pass 4 FX delivered  [STATUS: needs runtime QA]

-> ref: `gamedesign/docs/46_final_repaint_pass_4_fx_delivery_review.md`
-> contact sheet: `gamedesign/assets/concept/final_repaint_pass_4_fx/final_repaint_pass_4_fx_contact_sheet.png`

Art delivered all 47 FX frames as exact runtime PNG replacements:

```text
Batch B 64x64:
fx_dust_step_00..03
fx_tile_placed_00..03
fx_tile_trigger_00..03
fx_gain_popup_00..02
fx_invalid_cell_00..01

Batch C 128x128:
fx_intro_sand_00..05
fx_fire_glow_00..03
fx_last_tamga_spawn_00..05
fx_storm_veil_00..05
fx_card_reward_00..03
fx_near_death_00..03
```

GDD-side verification passed:

```text
py -3.12 tmp/validate_final_repaint_pass_4_fx.py: OK, 47/47 frames
build_turkic_jam_packs: Batch A 44/0, Batch B 47/0, Batch B FX 17/0, optional 91/0
cmake --build build/_cmake/native-debug --target turkic_jam: passed
```

Code request:

1. Hook only Batch B first playable FX now:
   - dust step;
   - tile placed;
   - tile trigger;
   - gain popup;
   - invalid cell.
2. Keep Batch C future FX inactive until matching features start.
3. Capture screenshot or short visual evidence for each drawn FX.
4. Watch risks:
   - `fx_dust_step_*` may be too subtle on bright sand;
   - `fx_storm_veil_*` may cover too much map if used too long;
   - `fx_near_death_*` is very red and must not fight HUD/log readability;
   - `fx_gain_popup_*` must support text, not replace text clarity.
5. Do not call FX final accepted until L5 screenshot/readability evidence exists.

## 2026-06-06 12:45 - Final repaint pass 4 FX contract  [STATUS: open]

-> ref: `gamedesign/docs/44_final_repaint_pass_4_fx_contract.md`

Art task:

```text
Prepare/generate/repaint/slice exact runtime PNG files for all raw/fx frames.
This is not a fake-shot pass.
Do not rename files, add ids, or touch non-FX folders.
```

Scope:

- Batch B first playable feedback FX:
  - dust step;
  - tile placed;
  - tile trigger;
  - gain popup;
  - invalid cell.
- Batch C future FTUE/death/memory FX:
  - intro sand;
  - fire glow;
  - last tamga spawn;
  - storm veil;
  - card reward;
  - near death.

Code request after Art delivery:

1. Rerun pack builder and report Batch B FX found/missing.
2. Keep Batch C out of active missing report unless a matching feature starts.
3. Hook Batch B FX first: dust, tile placed, tile trigger, gain popup, invalid cell.
4. Capture screenshot or short visual evidence for each drawn FX.
5. Do not call FX final accepted until L5 screenshot/readability evidence exists.

## 2026-06-06 12:30 - Final repaint pass 3 world map delivered  [STATUS: needs runtime QA]

-> ref: `gamedesign/docs/43_final_repaint_pass_3_world_map_delivery_review.md`
-> contact sheet: `gamedesign/assets/concept/final_repaint_pass_3_world_map/final_repaint_pass_3_world_map_contact_sheet.png`

Art delivered 30 exact runtime PNG replacements for world/map:

```text
raw/ground/ground_sand_base_01.png
raw/decor/decor_dune_01.png
raw/decor/decor_stones_01.png
raw/decor/decor_dry_grass_01.png
raw/decor/decor_tracks_01.png
raw/decor/decor_bones_01.png
raw/decor/decor_cracks_01.png
raw/road/road_straight_ns.png
raw/road/road_straight_ew.png
raw/road/road_corner_ne.png
raw/road/road_corner_es.png
raw/road/road_corner_sw.png
raw/road/road_corner_wn.png
raw/road/road_entry_aul.png
raw/road/road_current_highlight.png
raw/road/buffer_edge_stones_01.png
raw/road/buffer_packed_sand_01.png
raw/road/buffer_stakes_01.png
raw/road/buffer_cart_marks_01.png
raw/aul/aul_ground_2x2.png
raw/aul/aul_yurt_small_01.png
raw/aul/aul_yurt_small_02.png
raw/aul/aul_fire_01.png
raw/tiles/tile_yurt_01.png
raw/tiles/tile_tamga_stone_01.png
raw/tiles/tile_wolf_track_01.png
raw/tiles/tile_oasis_01.png
raw/tiles/tile_mirage_01.png
raw/tiles/tile_storm_01.png
raw/tiles/tile_last_tamga_01.png
```

GDD-side verification passed:

```text
py -3.12 tmp/validate_final_repaint_pass_3_world_map.py: OK
build_turkic_jam_packs: Batch A 44/0, Batch B 47/0, optional 91/0
cmake --build build/_cmake/native-debug --target turkic_jam: passed / no work to do
```

Code request:

1. Include these 30 files in the same L2/L3/L4/L5 visual QA report.
2. Capture normal gameplay map with aul, road, road buffer, quiet decor and hero.
3. Capture road corners and `road_entry_aul`.
4. Capture placed active tiles: yurt, tamga stone, beast trail, oasis, mirage, storm, last tamga.
5. Watch risks: `tile_wolf_track_01` may be too dark; mirage/storm may be too abstract; buffer must read as no-build edge, not a wall.
6. Do not call Pass 3 final accepted art until L5 screenshot/readability evidence exists.

## 2026-06-06 12:15 - Visual completion board added  [STATUS: active]

-> ref: `gamedesign/docs/42_visual_completion_board.md`

Purpose:

```text
Track all visual passes, candidate-final files, runtime QA gaps and next owners.
```

Current top-level state:

- Pass 1 minimum: delivered, candidate final, needs L5.
- Pass 1 continuation: delivered, candidate final, needs L5.
- Pass 2: delivered, candidate final, needs L5.
- Pass 3 world/map: open in Art thread.
- Code L5 screenshot QA: waiting on native/devapi harness approval or another fresh visual evidence path.

## 2026-06-06 12:05 - Final repaint pass 3 world map contract  [STATUS: open]

-> ref: `gamedesign/docs/41_final_repaint_pass_3_world_map_contract.md`

Art task:

```text
Prepare/generate/repaint/slice actual runtime PNG files for the world map.
Do not deliver a fake-shot as the output.
Keep exact filenames, ids, dimensions and alpha rules.
```

Scope:

- ground and quiet decor;
- road and road buffer;
- central aul objects;
- active tile sprites except `tile_saxaul_01.png`.

Do not repaint in this pass:

```text
tile_saxaul_01.png
hero/*
ui/*
icons/*
cards/*
equipment/*
```

Code request after Art delivery:

1. Rerun pack builder and report Batch A found/missing.
2. Confirm runtime bind for changed map ids.
3. Confirm drawn states for map ground/decor/road/buffer/aul/tiles.
4. Capture screenshot evidence or mark L5 blocked/stale.

## 2026-06-06 11:50 - Final repaint pass 2 delivered  [STATUS: needs runtime QA]

-> ref: `gamedesign/docs/40_final_repaint_pass_2_delivery_review.md`
-> contact sheet: `gamedesign/assets/concept/final_repaint_pass_2/final_repaint_pass_2_contact_sheet.png`

Art delivered 24 exact runtime PNG replacements:

```text
raw/hero/hero_wayfarer_walk_s.png
raw/hero/hero_wayfarer_walk_e.png
raw/hero/hero_wayfarer_walk_n.png
raw/hero/hero_wayfarer_walk_w.png
raw/hero/hero_wayfarer_panel.png
raw/equipment/equip_slot_weapon_01.png
raw/equipment/equip_slot_clothes_01.png
raw/equipment/equip_slot_tool_01.png
raw/equipment/equip_slot_tamga_01.png
raw/equipment/equip_weapon_staff_01.png
raw/equipment/equip_clothes_cloak_01.png
raw/equipment/equip_tool_satchel_01.png
raw/equipment/equip_tamga_charm_01.png
raw/icons/icon_body_32.png
raw/icons/icon_mind_32.png
raw/icons/icon_spirit_32.png
raw/icons/icon_circle_32.png
raw/icons/icon_day_32.png
raw/icons/icon_speed_32.png
raw/icons/icon_last_tamga_32.png
raw/cards/card_badge_count_32.png
raw/cards/card_placement_roadside_32.png
raw/cards/card_placement_field_32.png
raw/cards/card_placement_special_32.png
```

GDD-side verification passed:

```text
py -3.12 tmp/validate_final_repaint_pass_2.py: OK
build_turkic_jam_packs: Batch A 44/0, Batch B 47/0, optional 91/0
cmake --build build/_cmake/native-debug --target turkic_jam: passed
```

No forbidden `equip_slot_*_64.png` aliases were created.

Code request:

1. Include these 24 files in the L2/L3/L4/L5 runtime QA report.
2. Check `icon_mind_32.png`, `icon_speed_32.png`, placement icons and hero walk sprites at real UI/map scale.
3. Use native devapi harness if WASM remains stale; label the evidence path clearly.

## 2026-06-06 09:20 - Final repaint pass 2 contract  [STATUS: open]

-> ref: `gamedesign/docs/39_final_repaint_pass_2_contract.md`

Art task:

```text
Prepare/generate/repaint/slice actual runtime PNG files.
Do not deliver another fake-shot as the output.
Keep exact filenames, ids, dimensions and RGBA.
```

Scope:

- hero walk sprites and hero panel;
- right-panel equipment slots/items;
- remaining HUD stat icons;
- card badge and placement icons.

Code request after Art delivery:

1. Rerun pack builder and report Batch A/B found/missing.
2. Confirm runtime bind for changed files.
3. Confirm drawn states for HUD, hand card utilities, hero panel and hero walk sprites where available.
4. Capture screenshot evidence or mark L5 blocked/stale.

## 2026-06-06 09:00 - Final repaint pass 1 continuation delivered  [STATUS: needs runtime QA]

-> ref: `gamedesign/docs/38_final_repaint_pass_1_delivery_review.md`
-> contact sheet: `gamedesign/assets/concept/final_repaint_pass_1/final_repaint_pass_1_continuation_contact_sheet.png`

Art delivered 11 more exact runtime PNG replacements:

```text
raw/ui/ui_panel_felt_light_96.png
raw/ui/ui_slot_equipment_64.png
raw/ui/ui_chip_resource_64.png
raw/ui/ui_tooltip_dark_64.png
raw/ui/ui_card_back_96x128.png
raw/ui/ui_button_dark_64.png
raw/cards/card_art_yurt_64.png
raw/cards/card_art_tamga_stone_64.png
raw/cards/card_art_wolf_track_64.png
raw/icons/icon_wisdom_32.png
raw/icons/icon_glory_32.png
```

GDD-side verification passed after continuation:

```text
build_turkic_jam_packs: Batch A 44/0, Batch B 47/0, optional 91/0
cmake --build build/_cmake/native-debug --target turkic_jam: passed
```

Code request:

1. Include these 11 files in the same L2/L3/L4/L5 runtime QA report.
2. Check `card_art_wolf_track_64.png` carefully in the real hand card; it may be too dark/subtle.
3. Check `icon_wisdom_32.png` and `icon_glory_32.png` at real HUD size.

## 2026-06-06 08:50 - Final repaint pass 1 minimum delivered  [STATUS: needs runtime QA]

-> ref: `gamedesign/docs/37_final_art_repaint_pass_1.md`
-> ref: `gamedesign/docs/38_final_repaint_pass_1_delivery_review.md`
-> contact sheet: `gamedesign/assets/concept/final_repaint_pass_1/final_repaint_pass_1_contact_sheet.png`

Art/GDD replaced the first 8 exact runtime PNG files from Final Repaint Pass 1:

```text
raw/ui/ui_panel_felt_dark_96.png
raw/ui/ui_card_playable_96x128.png
raw/ui/ui_card_selected_96x128.png
raw/cards/card_art_saxaul_64.png
raw/icons/icon_stamina_32.png
raw/icons/icon_supplies_32.png
raw/tiles/tile_saxaul_01.png
raw/hero/hero_wayfarer_idle_s.png
```

GDD-side verification passed:

```text
PNG size check: all expected sizes
build_turkic_jam_packs: Batch A 44/0, Batch B 47/0, optional 91/0
cmake --build build/_cmake/native-debug --target turkic_jam: passed
```

Decision:

```text
These are candidate final-art replacements, not final visual acceptance yet.
```

Code request:

1. Rerun runtime bind/readability QA with these replaced PNGs.
2. Report L2/L3/L4/L5 for the 8 files.
3. Capture screenshots for HUD icons, bottom card hand, placed Saxaul tile, hero, and dark panel.
4. Watch specifically whether `icon_stamina_32` and `icon_supplies_32` still read at 24px HUD scale.

## 2026-06-06 08:35 - Final art repaint pass 1 contract  [STATUS: open]

-> ref: `gamedesign/docs/37_final_art_repaint_pass_1.md`

GDD reviewed the Art delivery report. Decision:

```text
Batch A/B/C placeholder kits are useful and pipeline-ready, but they are not final art.
```

New task for Art:

1. Start Final Repaint Pass 1 using exact runtime filenames.
2. Do not create new ids or broad fake-shots.
3. Replace priority visible PNGs in `games/turkic-jam-2026/raw/...`.
4. Include a contact sheet for changed files.
5. Report exact changed files and what remains placeholder.

Pass 1 priority:

```text
UI surfaces -> card art -> HUD icons -> hero/equipment -> saxaul -> aul/road/buffer
```

Request to Code:

1. Keep consuming the same filenames.
2. After Art replaces files, rerun pack builder and report invalid/missing if any.
3. Do not treat placeholder art as final-art accepted until screenshot QA.

## 2026-06-06 08:15 - Visual asset status matrix  [STATUS: open]

-> ref: `gamedesign/docs/36_visual_asset_status_matrix.md`

GDD added a visual asset status matrix to separate three states that were getting mixed together:

```text
raw placeholder exists
runtime sprite path exists
final art accepted
```

Current factual inventory:

```text
154 PNG files in games/turkic-jam-2026/raw
Batch A: 44 placeholder runtime files
Batch B: 47 placeholder runtime files
Batch C: 60 future placeholder runtime files
Other/existing UI files: 3
```

Decision:

1. Batch A and Batch B are pipeline-ready, not final-art-ready.
2. Batch A/B must move to screenshot QA before GDD accepts runtime visual integration.
3. Batch C remains future library and should not pollute current playable acceptance.
4. Designer replacement reports must identify what is still placeholder.

## 2026-06-06 08:05 - Runtime visual QA evidence gates  [STATUS: open]

-> ref: `gamedesign/docs/35_runtime_visual_qa_checklist.md`

GDD added a runtime visual QA checklist so Art and Code can prove visual integration with evidence, not only file presence.

Key rule:

```text
raw PNG -> builder found -> atlas/runtime bind -> drawn in gameplay/UI state -> screenshot QA
```

Request to Code:

1. For the current Batch B runtime pass, report not only builder totals, but also bind count, drawn states and screenshots/evidence.
2. Keep fallback UI/procedural shapes until screenshot QA proves the sprite path is readable.
3. Use the checklist reporting format:
   - Batch
   - Builder
   - Bind
   - Drawn
   - Screenshots
   - Fallback kept
   - Missing/invalid
   - Blocked
4. Batch C remains future library unless a specific feature starts.

Request to Art:

1. Replacement PNGs are accepted only if size/alpha/readability pass the checklist.
2. Placeholder art can pass pipeline QA, but not final art QA.
3. Keep exact runtime filenames unless GDD updates the batch contract.

## 2026-06-06 07:35 - Batch C future visual library placeholder kit  [STATUS: open]

-> ref: `gamedesign/docs/34_asset_production_batch_c.md`
-> ref: `gamedesign/tools/generate_batch_c_assets.py`
-> ref: `gamedesign/assets/concept/production_batch_c/batch_c_runtime_preview.png`
-> ref: `gamedesign/assets/concept/production_batch_c/batch_c_runtime_manifest.md`

GDD generated Batch C placeholder PNGs for future visual scope:

```text
future tile families
future card art variants
aul upgrade stages
hero archetype panel dolls
FTUE/death/memory/storm/card reward/near-death FX
future icons
```

Result:

```text
runtime raw PNG files: 60 new Batch C files
status: placeholder art, not final polished art
```

Request to Code:

1. Do not promote all Batch C ids into the active missing report yet.
2. Keep the files in raw folders as future-ready assets.
3. Register Batch C ids only when the related feature starts: aul upgrades, future cards, FTUE FX, death memory, storm veil, or archetype panel.
4. Keep Batch A/B reports separate from Batch C so the current playable status remains readable.

Request to Art:

1. Treat Batch C as future replacement targets, not final art.
2. Highest priority if repainting: aul stages, `fx_intro_sand_*`, `fx_last_tamga_spawn_*`, `hero_body/mind/spirit_panel`, then future tile families.

## 2026-06-06 07:15 - Batch B cards/equipment/icons/fx placeholder kit  [STATUS: open]

-> ref: `gamedesign/docs/33_asset_production_batch_b.md`
-> ref: `gamedesign/tools/generate_batch_b_assets.py`
-> ref: `gamedesign/assets/concept/production_batch_b/batch_b_runtime_preview.png`
-> ref: `gamedesign/assets/concept/production_batch_b/batch_b_runtime_manifest.md`

GDD generated Batch B placeholder PNGs for the remaining gameplay UI visual scope:

```text
raw/ui: ui_card_back_96x128, ui_button_dark_64
raw/cards: card badges, placement icons, P0 card art variants
raw/equipment: equipment slot silhouettes and first item icons
raw/icons: P0 HUD/stat icons
raw/fx: first per-frame FX files
```

Result:

```text
runtime raw PNG files: 47 new Batch B files
status: placeholder art, not final polished art
```

Request to Code:

1. Promote Batch B ids into optional atlas registry as grouped categories: ui/cards/equipment/icons/fx.
2. Keep Batch A and Batch B missing/found reports readable; do not hide map status behind future UI ids.
3. Start with registry + pack verification only.
4. Runtime priority after registry: HUD icons, card surfaces/card art, equipment slots/items, then FX hooks.
5. Keep fallback UI/shapes while final art is still iterating.

Request to Art:

1. Replace Batch B placeholders with polished versions using the same filenames, sizes, alpha rules and folders.
2. Highest priority: HUD icons readability at 24x24, card art readability at 64x64, equipment slots in the right hero panel.

## 2026-06-06 06:55 - Batch A placeholder PNG kit generated  [STATUS: open]

-> ref: `gamedesign/docs/32_asset_production_batch_a.md`
-> ref: `gamedesign/tools/generate_batch_a_assets.py`
-> ref: `gamedesign/assets/concept/production_batch_a/batch_a_runtime_preview.png`
-> ref: `gamedesign/assets/concept/production_batch_a/batch_a_runtime_manifest.md`

GDD generated a full runtime placeholder kit for Batch A. This is not final art; it is a pipeline-ready replacement target so Code can validate atlas ids and runtime drawing hooks now.

Result:

```text
runtime raw PNG files: 44
pack builder report: found 44 / missing 0
target build: cmake --build build/_cmake/native-debug --target turkic_jam - passed
```

Important folder contract:

```text
road_* and buffer_* currently both live in games/turkic-jam-2026/raw/road/
Do not expect raw/buffer/ unless the builder contract changes first.
```

Request to Code:

1. Rerun/continue with the new raw PNGs present.
2. Verify active tile sprites, hero sprite, ground/decor, and UI optional ids in runtime.
3. Next priority: replace procedural road/aul drawing with Batch A sprites where practical.
4. Keep fallback procedural drawing for missing art, because final art will be iterated.

Request to Art:

1. Replace placeholder PNGs with polished versions using exactly the same filenames, sizes, alpha rules, and folders.
2. Do not create new names for Batch A unless GDD updates this contract.

## 2026-06-06 06:35 — Asset Production Batch A contract  [STATUS: open]

→ ref: `gamedesign/docs/32_asset_production_batch_a.md`
→ ref: `gamedesign/docs/31_visual_production_master_plan.md`
→ ref: `gamedesign/assets/concept/ui_fake_shot_h_approved_style_reference.png`

GDD уточнил первый production batch для Art/Code. Это уже не fake-shot pass, а список конкретных PNG, размеров, alpha/slice9 правил и acceptance gates.

Главное для Code:

```text
32_asset_production_batch_a.md = конкретный Batch A contract
31_visual_production_master_plan.md = общий visual scope
```

Batch A включает:

```text
UI 9-slice:
ui_panel_felt_dark_96
ui_panel_felt_light_96
ui_card_playable_96x128
ui_card_selected_96x128
ui_slot_equipment_64
ui_chip_resource_64
ui_tooltip_dark_64

Map/world:
ground_sand_base_01
decor_dune_01
decor_stones_01
decor_dry_grass_01
decor_tracks_01
decor_cracks_01
decor_bones_01
road_straight_ew/ns
road_corner_ne/es/sw/wn
road_entry_aul
road_current_highlight
buffer_edge_stones_01
buffer_packed_sand_01
buffer_stakes_01
buffer_cart_marks_01

Аул:
aul_ground_2x2
aul_yurt_small_01
aul_yurt_small_02
aul_fire_01

Active tiles:
tile_saxaul_01
tile_yurt_01
tile_tamga_stone_01
tile_wolf_track_01
tile_oasis_01
tile_mirage_01
tile_storm_01
tile_last_tamga_01

Hero:
hero_wayfarer_idle_s
hero_wayfarer_walk_s/e/n/w
hero_wayfarer_panel
```

Просьба к Code:

1. Использовать `32_asset_production_batch_a.md` как ожидаемый id list для optional atlas registry и missing asset report.
2. Не ждать, пока все PNG будут готовы: fallback remains required.
3. Сначала закрыть runtime hooks для placed active tile sprites и hero sprite, потому что они сильнее всего показывают игроку, что арт реально встроен.
4. UI 9-slice registry добавить сейчас, а runtime UI замену делать Batch B, когда Art отдаст готовые surfaces.

---

## 2026-06-06 06:05 — Full visual production scope and approved fake-shot lock  [STATUS: open]

→ ref: `gamedesign/docs/31_visual_production_master_plan.md`
→ ref: `gamedesign/docs/ui_design_system_v0_1.md`
→ ref: `gamedesign/docs/playable_art_integration_brief.md`
→ ref: `gamedesign/assets/concept/ui_fake_shot_h_approved_style_reference.png`

Расширяю задачу: это уже не только первый `tile_saxaul_01`, а полный visual production pipeline для игры.

Важно:

```text
approved fake-shots are style locks
ui_fake_shot_h_approved_style_reference.png = approved gameplay UI/style reference
new generic fake-shots are not needed unless GDD asks for an unresolved screen
next art work = production asset kits
```

Новый главный документ по визуалу:

```text
gamedesign/docs/31_visual_production_master_plan.md
```

Что там зафиксировано:

```text
UI 9-slice kit
world ground / road / road_buffer
base decor for empty buildable cells
active tile sprites
cards
hero and character sprites
equipment / inventory / hero panel
aul assets and upgrade stages
icons
FX
batch plan
review gates
```

Просьба к Code:

1. Использовать `31_visual_production_master_plan.md` как текущий visual asset scope.
2. Текущий optional atlas + fallback plan остается правильным.
3. Делать runtime так, чтобы он мог постепенно принимать все группы ассетов из `raw/...`, а не только P0 map sprites.
4. Сгенерировать/поддержать отчет missing asset ids, чтобы Art понимал, какие PNG еще нужны.
5. Не импортировать fake-shot целиком. Fake-shot H - только reference для composition/style.

Порядок Code на сейчас:

```text
Batch 0: raw folder contract + optional atlas registration + fallback rendering
Batch 1: map readability sprites
Batch 2: UI kit/cards/hero panel sprites
```

---

## 2026-06-06 05:50 — Add new art to game: layered first playable asset pass  [STATUS: open]

→ ref: `gamedesign/docs/art_bible_v0_1.md`
→ ref: `gamedesign/docs/05_art_direction.md`
→ ref: `gamedesign/docs/ui_fake_shots_review_01.md`
→ ref: `gamedesign/assets/concept/ui_fake_shot_h_approved_style_reference.png`
→ ref: `gamedesign/assets/concept/base_decor_ground_tiles_01.png`
→ ref: `gamedesign/assets/concept/road_buffer_ground_tiles_01.png`
→ ref: `gamedesign/assets/concept/tile_saxaul_01_candidate.png`
→ ref: `gamedesign/assets/concept/active_tiles_transparent_sheet_02.png`

Нужна задача Code: подключить новый арт в игру так, чтобы он совпадал с GDD layer model и не превратил тайлы в baked fake-shot.

## Главный принцип

```text
concept/fake-shot images = reference only
production sprites = отдельные PNG в game raw folders
active gameplay tile object = transparent sprite поверх ground/decor
ground/base decor != active tile object
```

Не импортировать `fake_shot_*` или `ui_fake_shot_*` как игровые тайлы. Это референсы композиции, палитры и UI.

## Предлагаемая структура raw assets

```text
games/turkic-jam-2026/raw/ground/
games/turkic-jam-2026/raw/decor/
games/turkic-jam-2026/raw/road/
games/turkic-jam-2026/raw/tiles/
games/turkic-jam-2026/raw/aul/
games/turkic-jam-2026/raw/hero/
games/turkic-jam-2026/raw/fx/
games/turkic-jam-2026/raw/ui/
```

`raw/ui/` уже существует. Остальные папки можно добавить при первом production sprite.

## Render order / layers

```text
1. ground sand/base
2. quiet base decor for empty buildable cells
3. road_path
4. road_buffer no-build edge
5. active tile object sprites
6. hero / aul focus / current cell marker
7. placement highlight / trigger FX
8. gameplay UI
```

Критично: `road_buffer` должен быть визуально no-build зоной, но не стеной. `field_build` и `roadside_build` должны читаться как разные placement-зоны.

## First import priority

Если можно подключить только маленький батч, порядок такой:

```text
P0. road_path sprites + road_buffer sprites
P0. aul core: ground/yurts/fire
P0. hero_wayfarer walk/idle placeholder sprite
P0. tile_saxaul_01 as transparent active tile sprite
P1. base decor for empty buildable cells
P1. tile_yurt_01, tile_tamga_stone_01, tile_wolf_track_01
P2. oasis, mirage, storm, last_tamga, FX
```

Почему: FTUE сейчас проверяет путь `аул -> герой вышел -> дорога -> Саксаул поставлен -> эффект сработал`. Для этого важнее дорога, буфер, аул, герой и Саксаул, чем полный набор красивых тайлов.

## Asset ids / naming baseline

Использовать стабильные ids из art bible:

```text
ground_sand_base_01
decor_dune_01
decor_stones_01
decor_dry_grass_01
decor_tracks_01
decor_bones_01
decor_cracks_01

road_straight_ns
road_straight_ew
road_corner_ne
road_corner_es
road_corner_sw
road_corner_wn
road_entry_aul
road_current_highlight

buffer_edge_stones_01
buffer_packed_sand_01
buffer_stakes_01
buffer_cart_marks_01

aul_ground_2x2
aul_yurt_small_01
aul_yurt_small_02
aul_fire_01

hero_wayfarer_idle_s
hero_wayfarer_walk_s/e/n/w
hero_wayfarer_panel

tile_saxaul_01
tile_yurt_01
tile_tamga_stone_01
tile_wolf_track_01
tile_mirage_01
tile_storm_01
tile_last_tamga_01
```

Важно по `tile_wolf_track_01`: internal id остается `wolf_track`, player-facing карта называется `Звериная тропа`. Визуально это тропа риска/следы/царапины, не волк-тотем и не тело волка.

## Code implementation request

1. Расширить `build_packs.c`: добавить atlas entries для production PNG из `games/turkic-jam-2026/raw/...`.
2. Не ломать текущие fallback/procedural shapes: если sprite отсутствует или pack не пересобран, игра должна оставаться запускаемой.
3. В `view`/render layer не смешивать ground/decor/active tile. Для GDD важно потом менять `tiles.tsv`, не перерисовывая весь ground.
4. Привязать sprite selection к existing config ids (`tiles.tsv`) там, где это возможно.
5. Для первого прохода можно использовать только 64x64 sprites и без сложной анимации; fire/hero/FX можно временно 1-2 кадра.
6. После подключения прислать список: какие asset ids реально добавлены, какие пока fallback, и скрин/описание первого экрана.

## Visual acceptance

Считать проход успешным, если:

```text
аул читается как центр
дорога читается как путь вокруг аула
road_buffer читается как no-build кромка
пустые buildable cells не выглядят пустой сеткой
Саксаул выглядит как низкий куст/хворост, а не дерево и не оазис
hero виден на дороге
карта/лог/UI не перекрывают игровое поле сильнее, чем в approved UI direction
```

---

## 2026-06-06 05:35 — Narrative content baseline accepted  [STATUS: open]

→ ref: narrative thread `019e995e-943a-7d11-8592-a2a8d1e4ef2e`
→ ref: `gamedesign/docs/30_narrative_content_baseline.md`
→ ref: `gamedesign/docs/13_event_log_and_texts.md`
→ ref: `gamedesign/docs/28_current_source_of_truth.md`

Принимаю `30_narrative_content_baseline.md` как рабочий baseline для:

```text
FTUE text
P0 card/tile copy
first choice copy
event/log library
risk/help/memory events
synergies/transformations
first gifts/things
naming rules
cultural guardrails
```

Для Code это означает:

```text
если нужен текст события, карты, выбора, gift или синергии -> сначала смотреть 30_narrative_content_baseline.md
если текст в старом GDD конфликтует с 30 -> считать старый текст устаревшим
```

Ключевые строки:

```text
Песок стирает следы.
Путь ждет первого путника.
Отправиться в путь
Первый путник выходит из аула.
Путь ведет его сам.
```

Risk naming:

```text
internal id = wolf_track
player-facing card name = Звериная тропа
trigger/log detail = волчий след открыт у дороги
```

First choice copy:

```text
Род советуется у огня: стоянка, память или риск.

Юрта — Малая стоянка. Запасы аула +2.
Камень Тамги — Знак памяти. Мудрость +2.
Звериная тропа — Опасный след. Проверка Body. Слава +2.
```

Просьба к Code:

1. Когда подключаешь `log.tsv`, брать event ids/templates из `13_event_log_and_texts.md` и `30_narrative_content_baseline.md`.
2. Если появится `cards.tsv`, player-facing copy брать из `30`.
3. Если появится `synergies.tsv` или `gifts.tsv`, стартовые строки брать из `30`.

---

## 2026-06-06 05:20 — UI fake shot F accepted, right panel simplified  [STATUS: open]

→ ref: art/UI thread `019e9928-1183-71b0-8014-3e8cce9a7afb`
→ ref: `gamedesign/assets/concept/ui_fake_shot_f_playable_cards_hero_panel.png`
→ ref: `gamedesign/docs/ui_fake_shots_review_01.md`
→ ref: `gamedesign/docs/ui_style_iteration_02_turkic_identity.md`
→ ref: `gamedesign/docs/16_ui_ux_layout.md`

Принимаю F как UI visual direction для следующего прохода.

Что берем:

```text
ярче и теплее
тюркско-кочевой identity layer
без title игры в gameplay HUD
компактная подсказка top-center
карты снизу читаются как playable/spendable cards
правая панель ближе к Loop Hero/RPG hero panel
```

Что правим:

```text
right panel MVP must be simpler
Хранилище / backpack / keepsakes убрать из normal gameplay HUD
Реликвия и Последняя Тамга не должны быть активными с первой секунды
```

## Right panel MVP

Always visible:

```text
hero doll / silhouette
archetype + "Путник рода"
Body / Mind / Spirit
Силы
current cell
current check / current danger summary when active
```

Optional visible if clean:

```text
3-4 empty equipment slots
```

Hidden or locked until event:

```text
Реликвия пути -> appears after first relic
Тамга прошлого -> appears after death / Last Tamga appears
```

Remove from gameplay HUD:

```text
Хранилище
рюкзак
small keepsake row
```

These belong to death/result/inter-run screens, not normal run. If shown during first playable, they read as unexplained inventory and confuse the player.

## Naming

Use:

```text
Реликвия пути
Тамга прошлого
Сумка рода / Хранилище аула only between runs
```

Rule:

```text
do not show a named system slot before the player has seen that system
```

---

## 2026-06-06 05:05 — Direct answers to Code questions  [STATUS: open]

→ ref: FROM_CODE 2026-06-06 — "КОНФЛИКТ РЕШЁН"
→ ref: FROM_CODE 2026-06-06 — questions #2/#3/#4
→ ref: `gamedesign/docs/28_current_source_of_truth.md`

Отвечаю коротко по открытым вопросам.

## 1. Field permanent + road reroute

Принимаю реализацию:

```text
field_build = постоянный слой построек
road loop = пересобирается между кругами и обходит постройки
storm veil = скрывает перепрокладку
```

Это хорошее решение для фишки "путь меняется", если выполнены два условия:

1. поставленные тайлы не исчезают и не теряют позицию;
2. игрок видит причинность `построил в поле -> дорога изменилась/обошла -> тайл продолжает работать`.

Если визуально дорога начинает выглядеть хаотично, GDD-ограничение: перепрокладка должна быть мягкой, не полная новая карта каждый круг.

## 2. Road events on road_path

Да, road-встречи на `road_path` разрешены.

```text
road_path = no-build для игрока
road_path = ok для системных on_enter events
```

Игрок не строит на дороге, но игра может класть туда временные встречи/риски/проверки. Это нормально и близко к Loop Hero.

## 3. Архетип vs Путник

Да, текущая модель ок:

```text
экран выбора / механика: Охотник, Шаман, Сказитель
лог / нарратив: Первый путник, Новый путник, Путник
```

В UI можно показывать:

```text
Охотник
Путник рода
```

или:

```text
Охотник-путник
```

Но в первых логах личные имена не нужны.

## 4. Priority: camera vs log.tsv

После уже сделанного field/road следующим приоритетом ставлю:

```text
road_buffer + zone highlight + invalid placement feedback
```

Почему: без этого игрок не понимает, где дорога, где кромка, где можно ставить карты.

Порядок:

1. `road_buffer` как читаемая no-build кромка.
2. Подсветка valid zones при выбранной карте.
3. Invalid feedback bubble/log:
   - `Здесь проходит дорога.`
   - `Кромка пути: строить нельзя.`
   - `Эта карта ставится дальше от дороги.`
   - `Эта карта должна стоять у дороги.`
4. Потом `log.tsv`.
5. Потом camera pan/center.
6. Потом battle bubble.

Если нужно быстрое улучшение параллельно: можно подключить `log.tsv` как малую задачу, но главный UX-блок сейчас zones/feedback.

## 5. wolf_track naming

Не ломаем internal id:

```text
id = wolf_track
player-facing name = Звериная тропа
trigger/log detail = Волчий след открыт у дороги
```

Причина: игроку понятнее ставить "тропу", а "след" оставить как проявление риска на road cell.

---

## 2026-06-06 04:55 — Card naming + wolf guardrail + road/field accepted  [STATUS: open]

→ ref: FROM_CODE 2026-06-06 — "КОНФЛИКТ РЕШЁН"
→ ref: narrative/content review `29_card_tile_families_review.md`
→ ref: `gamedesign/docs/28_current_source_of_truth.md`
→ ref: `games/turkic-jam-2026/config/tiles.tsv`

Принимаю решение пользователя/Code по карте:

```text
field_build = постоянный слой построек
road loop = может пересобираться между кругами, но обходит постройки
```

Это хорошее решение, если сохраняется причинность:

```text
поставленный тайл -> сохраняет позицию -> дорога его обходит -> эффект остается понятен игроку
```

## Risk card rename

Принимаю рекомендацию narrative/content review:

```text
internal id = wolf_track
player-facing card name = Звериная тропа
trigger/log detail = Волчий след открыт у дороги
placement = roadside
role = Body risk, Слава
```

Важно: не ломаем internal id `wolf_track`, чтобы не усложнять Code. Меняем player-facing copy.

Первый выбор после круга:

```text
Юрта
Камень Тамги
Звериная тропа
```

`Волчий след` больше не название карты игрока. Это visual/log marker события на дороге.

## Wolf guardrail

Волк допустим для сеттинга, но осторожно:

1. Биологически степной волк подходит для степи, полупустыни и Центральной Азии.
2. Культурно волк в тюркских/тюрко-монгольских традициях слишком значимый образ, чтобы использовать его как случайного монстра.
3. Для P0 не делаем волка духом, тотемом, сакральным знаком или "мифическим боссом".
4. P0 формулировка: звериная опасность на дороге; волчий след как предвестник.

## Next priority

После решения road/field следующий приоритет для Code:

```text
road_buffer + valid placement highlight + invalid feedback
```

`log.tsv` важен и быстрее, но без читаемых зон игрок не поймет карту. Если нужно резать: сначала zones/feedback, потом log.tsv, потом camera/battle bubble.

---

## 2026-06-06 04:35 — Review cleanup: active source of truth + answers to Code  [STATUS: open]

→ ref: FROM_CODE 2026-06-06 — "РЕВЬЮ что не сделано + вопросы"
→ ref: `gamedesign/docs/28_current_source_of_truth.md`
→ ref: `gamedesign/docs/26_ftue_step_1_narrative.md`
→ ref: `gamedesign/docs/01_world_layout.md`

Сделал ревью документов и чата с Code. Актуальный короткий контракт теперь:

```text
gamedesign/docs/28_current_source_of_truth.md
```

Если старые GDD-заметки конфликтуют с ним, считать старую заметку устаревшей.

## FTUE Step 1 text

Использовать baseline:

```text
Песок стирает следы.
Путь ждет первого путника.

[Отправиться в путь]

Первый путник выходит из аула.
Путь ведет его сам.
```

Не использовать формулировку, где огонь выступает зовущим субъектом: это звучит как мутная магия. Костер/аул = дом и readiness target. Путь = движение наружу и сказочный зов.

## Ответ на вопрос 1: разный путь каждый круг vs persistent field_build

Решение для первого playable:

1. Внутри одного забега поставленные игроком тайлы должны сохраняться между кругами.
2. Дорога не должна каждый круг ломать связь `поставленный тайл -> будущий эффект`.
3. Вариативность между кругами можно показывать через песчаную вуаль, события, угрозы, декор и небольшие визуальные изменения.
4. Топологию дороги безопаснее менять между наследниками/новыми забегами.
5. Если текущая реализация уже меняет дорогу каждый круг, нужно либо сохранить привязку тайлов к road slots, либо временно ограничить изменение дороги до нового наследника.

Иначе игрок не поймет причинность карт, а `field_build` потеряет смысл.

## Ответ на вопрос 2: road-встречи

`road_path = no-build` означает "игрок не строит на дороге".

Системные road-встречи разрешены:

```text
road_path -> on_enter события/враги/проверки, созданные игрой
roadside_build -> тайлы игрока, которые влияют на ближайший проход
field_build -> дальние ауры/доход/синергии/глобальный риск
```

## Ответ на вопрос 3: нейминг и архетипы

ОК:

```text
механика/выбор: Охотник / Шаман / Сказитель
нарративный лог: Первый путник / Новый путник / Путник
```

В панели героя можно писать `Охотник-путник` или показывать архетип отдельной строкой. В первых логах личные имена не нужны.

## Ответ на вопрос 4: следующий приоритет

Приоритет после текущего ревью:

1. Выравнять реализацию и сайт на FTUE text `Путь ждет первого путника`.
2. Стабилизировать причинность дороги/тайлов внутри забега.
3. Доделать `road_buffer`, `field_build`, подсветку валидных зон и invalid feedback.
4. Подключить `log.tsv`.
5. Потом camera pan/center и battle bubble.
6. Апгрейды аула после того, как core readability не спорит с картой.

---

## 2026-06-06 04:05 — Narrative docs added, Step 1 baseline confirmed  [STATUS: superseded by 04:35]

→ ref: narrative designer thread `019e995e-943a-7d11-8592-a2a8d1e4ef2e`
→ ref: `gamedesign/docs/25_narrative_bible.md`
→ ref: `gamedesign/docs/26_ftue_step_1_narrative.md`
→ ref: `gamedesign/docs/27_cultural_research_backlog.md`
→ ref: `gamedesign/docs/gdd.md`

Нарративный дизайнер добавил базовые документы. Эта запись оставлена как история. Актуальный baseline см. выше в записи 04:35 и в `28_current_source_of_truth.md`.

GDD подтверждал:

```text
26_ftue_step_1_narrative.md = narrative baseline для первых 6 секунд и первого клика FTUE
```

Использовать Step 1 text:

```text
Песок стирает следы.
У костра ждет первый путник.

[Отправиться в путь]

Первый путник выходит из аула.
Путь ведет его сам.
```

`Род хранит имена` зарезервирован для смерти/Тамги/результата, не для первых 6 секунд.

Культурные guardrails и research backlog теперь в:

```text
25_narrative_bible.md
27_cultural_research_backlog.md
```

---

## 2026-06-06 03:55 — Narrative baseline for FTUE step 1  [STATUS: superseded by 04:35]

→ ref: narrative designer thread `019e995e-943a-7d11-8592-a2a8d1e4ef2e`
→ ref: `gamedesign/docs/24_narrative_designer_role.md`
→ ref: `gamedesign/docs/22_ftue_production_script.md`
→ ref: `gamedesign/index.html` вкладка FTUE

Историческая запись. Актуальный baseline см. выше в записи 04:35 и в `28_current_source_of_truth.md`.

Ранее фиксировался production baseline текста первого шага FTUE.

Использовать в первых 6 сек:

```text
Песок стирает следы.
У костра ждет первый путник.
```

Readiness action:

```text
Отправиться в путь
```

После клика:

```text
Первый путник выходит из аула.
```

После 2-3 шагов:

```text
Путь ведет его сам.
```

Важно: `Род хранит имена` не выводим в первых 6 сек. Это сильная, но абстрактная строка. Ее лучше использовать позже в контексте смерти/Последней Тамги/результата забега, где память рода становится механикой.

Культурные guardrails:

1. Используем тюркские мотивы как структурные мотивы: род, путь, огонь, знак/тамга, память, след.
2. Не вводим конкретных духов, богов, ритуалы, реальные родовые знаки или имена без отдельного ресерча.
3. Не делаем Тамгу generic magic rune.
4. Не смешиваем все тюркские народы в декоративный набор.

---

## 2026-06-06 03:40 — FTUE step 1 readiness action: click aul/campfire  [STATUS: open]

→ ref: `gamedesign/docs/22_ftue_production_script.md`
→ ref: `gamedesign/docs/24_narrative_designer_role.md`
→ ref: `gamedesign/index.html` вкладка FTUE / Segment A-B storyboard

Уточнение по первому шагу FTUE.

После reveal аула игрок должен сделать маленькое действие готовности:

```text
click campfire / click aul core / press Space
```

На экране:

```text
анимированный палец над аулом/костром
подпись: "Отправиться в путь"
костер мягко пульсирует
```

Это не управление путником и не выбор направления. Это подтверждение, что игрок готов начать. После клика:

```text
Первый путник выходит из аула. Костер остается за спиной.
```

`ftue_04_aul_exit` считается gameplay-state с locked input, не cinematic. Карта, камера, герой и UI уже живые, но игрок пока не управляет, потому что FTUE учит автодвижение.

Если игрок не нажимает 2-3 секунды, pointer/костер становятся заметнее. Автостарт допустим только как fallback через 8-10 секунд.

---

## 2026-06-06 03:25 — Explicit task: tiles, world zones, camera/scroll  [STATUS: open]

→ ref: `gamedesign/docs/20_tile_placement_system.md`
→ ref: `gamedesign/docs/22_ftue_production_script.md`
→ ref: `gamedesign/index.html` вкладка FTUE / Placement teaching

Отдельно фиксирую задачу по тайлам, большому миру и камере, чтобы это не было размазано по прошлым сообщениям.

## MVP world model

Карта не должна быть только маленьким экраном вокруг аула. Нужен большой мир пустыни:

```text
aul_core       -> центр, no-build
road_path      -> путь героя, no-build
road_buffer    -> кромка дороги, no-build
roadside_build -> ближние buildable клетки, прямой эффект на проход
field_build    -> дальний buildable мир, требует камеры/скролла позже
```

## Camera / scroll

Для первого MVP:

1. В начале FTUE камера locked/soft-follow вокруг аула и дороги.
2. Первый Саксаул ставится **без скролла** в видимый `roadside_build` слот.
3. После первого круга, если игрок выбирает `yurt` или `tamga_stone`, камера чуть отъезжает или панорамирует к `field_build`.
4. Игрок должен понять, что пустыня больше первого 16:9 экрана.
5. Ручной pan/scroll камеры нужен для большого field, но не должен требоваться в первые 3 минуты.

Минимальный вариант управления камерой:

```text
mouse drag / touch drag -> pan
wheel / pinch           -> zoom optional, можно отложить
button "center hero"    -> вернуть к герою
button "center aul"     -> вернуть к аулу
```

Если времени мало, zoom можно отложить. Pan/scroll для `field_build` важнее.

## Tile placement rules

Первый набор:

```text
saxaul      -> roadside_build
oasis       -> roadside_build или rare field landmark, но не FTUE first
yurt        -> field_build
tamga_stone -> field_build
wolf_track  -> roadside_build
mirage      -> field_build
storm       -> field_build
```

Подсветка:

1. Выбрана `saxaul`: подсвечивать только видимые `roadside_build`; `field_build` виден, но muted.
2. Выбрана `yurt` или `tamga_stone`: подсвечивать `field_build`; камера показывает дальние клетки.
3. Выбран `wolf_track`: подсвечивать `roadside_build`.
4. `road_path` и `road_buffer` не подсвечиваются никогда.

Ошибочный клик:

```text
road_path   -> "Это путь героя. Здесь не строят."
road_buffer -> "Кромка пути: строить нельзя."
wrong zone  -> "Эта карта требует другую зону."
occupied    -> "Здесь уже стоит знак."
```

Feedback должен быть bubble/log, не модалка.

## No empty cells

Каждая buildable cell должна иметь:

```text
base_ground_id
base_decor_id
tile_id optional
```

Если `tile_id = none`, клетка все равно рисуется как живая пустыня: дюна, камни, сухая трава, следы, кости, трещины. Active tile object рисуется поверх как отдельный sprite/object.

## Acceptance

1. В первом экране видны аул, дорога, buffer и несколько roadside/field клеток.
2. Первый Саксаул ставится без скролла.
3. `road_buffer` визуально читается и запрещен.
4. После первого круга `field_build` становится понятным через Юрту/Камень Тамги.
5. Камеру можно сдвинуть по большому миру и вернуть к герою/аулу.
6. Нет пустых бежевых клеток без ground/decor.

---

## 2026-06-06 03:10 — FTUE naming: первый герой = Путник, без личного имени  [STATUS: open]

→ ref: `gamedesign/docs/17_first_10_minutes.md`
→ ref: `gamedesign/docs/22_ftue_production_script.md`
→ ref: `gamedesign/docs/23_ftue_analysis_and_improvement_plan.md`
→ ref: `gamedesign/index.html` вкладка FTUE

Правка по фидбеку пользователя: в FTUE не используем личные имена вроде `Арын/Саян`.

Причина:

- в первые минуты игрок должен понять архетип и core loop, а не запоминать имя;
- текущие heirs в config уже архетипы (`Охотник`, `Шаман`, `Сказитель`);
- первый герой должен читаться как средний стартовый архетип.

Использовать:

```text
Первый путник
Путник
Новый путник
```

Примеры логов:

```text
У костра поднимается первый путник.
Первый путник выходит из аула. Костер остается за спиной.
Путник едва стоит. На песке проступает Последняя Тамга.
Новый путник выходит из аула. Костер остается за спиной.
```

Личные имена можно вводить позже, если появится отдельная система named heirs, но не в первом FTUE.

---

## 2026-06-06 02:55 — FTUE analysis: сначала доказать Segment A-B  [STATUS: open]

→ ref: `gamedesign/docs/23_ftue_analysis_and_improvement_plan.md`
→ ref: `gamedesign/docs/22_ftue_production_script.md`
→ ref: `gamedesign/docs/06_claude_brief.md`
→ ref: `gamedesign/index.html` вкладка FTUE

Сделал анализ FTUE и уточнил порядок реализации.

Главный риск: FTUE перегружается, если в первые минуты одновременно объяснять `road_buffer`, `roadside`, `field`, карты, лог, смерть, Тамгу и наследника.

Решение для Code:

```text
Segment A-B = must-pass first
```

До расширения на выбор 1 из 3 и смерть нужно убедиться, что без устных объяснений читается:

1. герой выходит из аула;
2. герой идет сам;
3. игрок получает Саксаул;
4. первый placement виден без скролла;
5. `road_path` и `road_buffer` не подсвечиваются;
6. ошибочный клик по buffer дает bubble/log, не модалку;
7. Саксаул срабатывает позже на связанной road cell;
8. игрок понимает причинность `карта -> слот -> тайл -> будущий эффект`.

Добавлены UI lock rules: в каждом FTUE-state явно указано, что скрыто, что disabled, что clickable.

---

## 2026-06-06 02:30 — Подробный FTUE production script  [STATUS: open]

→ ref: `gamedesign/docs/22_ftue_production_script.md`
→ ref: `gamedesign/docs/21_ftue_best_practices_research.md`
→ ref: `gamedesign/docs/06_claude_brief.md`
→ ref: `gamedesign/index.html` вкладка FTUE

Сделал главный рабочий сценарий FTUE по состояниям. Если старые FTUE-заметки конфликтуют с ним, приоритет у `22_ftue_production_script.md`.

Главная последовательность:

```text
ftue_00_boot
-> ftue_03_aul_reveal
-> ftue_04_aul_exit
-> ftue_05_road_entry
-> ftue_07_first_resource
-> ftue_08_card_saxaul_found
-> ftue_10_saxaul_placed
-> ftue_11_saxaul_triggered
-> ftue_12_lap_1_complete
-> ftue_13_first_choice_opened
-> ftue_17_first_risk_reveal
-> ftue_19_death_or_near_death
-> ftue_21_new_heir_exit
-> ftue_22_last_tamga_pickup
```

Важно для реализации:

1. Герой должен выйти из аула к дороге, а не появиться сразу на road cell.
2. Первый дроп только `saxaul`; Оазис не стартовый.
3. Первая постановка только `roadside_build`; `road_path` и `road_buffer` запрещены.
4. Саксаул срабатывает позже на связанной road cell: `Силы +1`, `Запасы +1`.
5. Первый выбор после круга: `yurt` field, `tamga_stone` field, `wolf_track` roadside.
6. Ошибочный клик по buffer/road должен дать короткий inline/log feedback без модалки.
7. Нужно заложить FTUE event ids из документа для ручной проверки/воронки.

---

## 2026-06-06 01:45 — Единый документ по tile placement  [STATUS: open]

→ ref: `gamedesign/docs/20_tile_placement_system.md`
→ ref: `gamedesign/docs/gdd.md`

Добавил отдельный документ с текущим источником правды по зонам карты, placement и первому набору тайлов.

Code можно читать его как краткий implementation brief:

```text
road_path      -> no-build
road_buffer    -> no-build
roadside_build -> build, прямой эффект на ближайший проход
field_build    -> build, большой мир, радиусы/ауры/доход/синергии
```

Первый FTUE placement:

```text
saxaul -> roadside -> Силы +1, Запасы +1
```

Важные acceptance:

1. `road_buffer` нельзя строить и он визуально читается как кромка дороги.
2. `field`-тайлы не требуют соседства с дорогой.
3. Саксаул первый, Оазис не стартовый.
4. Buildable-клетки имеют `base_decor_id`, пустых клеток нет.

---

## 2026-06-06 01:25 — Правка тайлов: первый тайл Саксаул, Оазис не стартовый, мир большой  [STATUS: open]

→ ref: обсуждение с пользователем — Оазис слишком сильный/редкий для первой карты
→ ref: `games/turkic-jam-2026/config/tiles.tsv`
→ ref: `gamedesign/docs/01_world_layout.md`
→ ref: `gamedesign/docs/12_playable_developer_plan.md`

Меняю GDD-решение:

```text
первый guaranteed drop = saxaul / Саксаул
oasis / Оазис = более сильный тайл, не первая обучающая карта
```

Почему:

- Оазис в пустыне ощущается как крупная редкая находка.
- Первый обучающий тайл должен быть маленьким, частым и правдоподобным.
- Саксаул хорошо объясняет “малую помощь”: тень, хворост, ориентир.

### Обновил `tiles.tsv`

Добавлен:

```text
saxaul | Саксаул | safe | none | 0 | 0 | 1 | 0 | 0 | 0 | 1 | roadside
```

Изменены placement-ожидания:

```text
saxaul      -> roadside
oasis       -> roadside
yurt        -> field
tamga_stone -> field
wolf_track  -> roadside
mirage      -> field
storm       -> field
```

Важно: не все тайлы должны стоять рядом с дорогой.

### Новая модель зон

```text
aul_core       no-build
road_path      no-build, герой идет здесь
road_buffer    no-build, визуальная/техническая кромка дороги
roadside_build build, прямой эффект на ближайшую road cell
field_build    build, дальний скроллящийся мир
```

`road_buffer` нужен обязательно: там нельзя строить, это место под читаемость дороги, анимацию героя, battle bubble и будущую генерацию/изгибы дороги. Визуально это не пустота, а кромка: камни, утоптанный песок, колышки, следы повозок.

### Что нужно Code

1. В FTUE заменить guaranteed `oasis` на `saxaul`.
2. Первый эффект:

```text
Саксаул: путник находит тень и хворост. Силы +1. Запасы +1.
```

3. Оазис оставить в пуле как сильный тайл, но не использовать как первый обучающий.
4. Не считать, что все карты игрока = roadside. `field` нужен для дальних тайлов большого мира.
5. Поддержать no-build `road_buffer` вокруг дороги.

---

## 2026-06-06 01:05 — FTUE production script + правило “нет пустых клеток”  [STATUS: open]

→ ref: запрос пользователя — “по этому описанию можно было просто брать и делать”
→ ref: сайт GDD `/gamedesign/`, вкладка FTUE
→ ref: `gamedesign/docs/01_world_layout.md`
→ ref: `gamedesign/docs/17_first_10_minutes.md`

Обновил FTUE на сайте: теперь это не абстрактные карточки, а production script.

Каждый шаг первого круга содержит:

- финальный текст;
- арт-описание кадра;
- описание анимации;
- действие игрока;
- UI/лог;
- данные/состояния;
- условие готовности.

### Главное новое правило уровня

Пустых клеток на уровне быть не должно.

Свободная buildable-клетка не рисуется как пустой песочный квадрат. До постановки карты она выглядит как декоративная пустынная сцена:

```text
dune
stones
dry_grass
tracks
bones
cracks
```

Предлагаемая модель данных:

```text
cell.base_decor_id = dune / stones / dry_grass / tracks / bones / cracks
cell.tile_id = none / oasis / yurt / tamga_stone / wolf_track / ...
```

Если `tile_id = none`, клетка все равно рисует `base_decor_id` и может подсвечиваться как доступная для карты.
Если игрок ставит карту, активный тайл визуально заменяет или перекрывает базовый декор.

Реф по ощущению: Capybara Go — экран постоянно дает маленькие сигналы, всплывающие числа, следы, находки, лог. Даже спокойная клетка не должна ощущаться мертвой.

### Следующая задача для Code

1. Добавить/поддержать `base_decor_id` для buildable-клеток карты.
2. При генерации карты назначать декор каждой свободной roadside-клетке.
3. Рисовать декор, когда `tile_id = none`.
4. При placement карты заменять/перекрывать декор активным тайлом.
5. В FTUE реализовать первые состояния:

```text
intro_dark
desert_reveal
aul_exit
road_entry
loop_walk
road_reward_small
card_found_pause
place_tile
tile_effect_saxaul
lap_complete_pause
```

---

## 2026-06-06 00:35 — Ответ на открытые вопросы Code + значения placement  [STATUS: open]

→ ref: FROM_CODE 2026-06-05 — Placement + постановка игроком готовы (playable шаги 1-5)
→ ref: FROM_CODE 2026-06-05 — Обсуждение: дорога (реф LH) + чат/лог событий
→ ref: FROM_CODE 2026-06-05 — Старт кора + контракт конфигов

Закрываю открытые вопросы одним сообщением, чтобы не искать ответы по старым блокам.

### 1. placement в `tiles.tsv`

Проставил 12-ю колонку `placement` в:

```text
games/turkic-jam-2026/config/tiles.tsv
```

Этот блок был обновлен 2026-06-06 01:25. Актуально теперь 7 тайлов и смешанный placement:

```text
saxaul      -> roadside
oasis       -> roadside
yurt        -> field
tamga_stone -> field
wolf_track  -> roadside
mirage      -> field
storm       -> field
```

Почему: не все карты игрока должны стоять рядом с дорогой. `roadside` - прямой эффект на проходе героя; `field` - дальний мир, радиусы, доход за круг, ауры и синергии.

### 2. Карты = tiles 1:1 для MVP

Подтверждаю:

```text
cards = tiles 1:1
```

Отдельный `cards.tsv` пока не нужен. Заводим его только когда понадобятся:

- `rarity`;
- `drop_weight`;
- правила руки;
- отдельные названия/описания карт;
- unlock по этапам аула.

### 3. log.tsv

Да, `log.tsv` нужен. Схема `event_id | tone | template` подходит.

Минимальный набор событий:

```text
run_start_from_aul
road_entry
resource_gain
card_found
card_placed
tile_trigger
check_success
check_fail
lap_complete
death
last_tamga_spawn
last_tamga_pickup
new_heir
```

Что логируем без паузы:

- ресурс;
- срабатывание тайла;
- успех/провал проверки;
- потеря/восстановление Сил;
- постановка карты;
- появление/подбор Тамги.

Что паузит игру:

- первая обучающая постановка Саксаула;
- выбор 1 из 3 карт после круга;
- смерть / результат забега;
- запуск нового наследника;
- крупное редкое событие после MVP.

### 4. Таблицы `synergies.tsv`, `trials.tsv`, `aul.tsv`

Подтверждаю как будущие отдельные таблицы, но не текущий блокер.

Порядок:

```text
Последняя Тамга
-> выбор 1 из 3 карт
-> выбор наследника
-> минимальные апгрейды аула
-> log.tsv
-> trials.tsv
-> synergies.tsv
-> aul.tsv как расширенная мета
```

Если удобнее подключить `log.tsv` раньше, можно, но по gameplay-приоритету Тамга и выбор карт важнее.

### 5. Ближайшая задача

Самая важная следующая задача остается:

```text
aul_exit -> road_entry -> loop_walk
```

Герой должен выйти из стойбища/аула к дороге, а не появляться сразу на loop.

После этого:

1. исправить стартовый скачок `dt`;
2. сделать Последнюю Тамгу;
3. сделать выбор 1 из 3 карт после круга.

## 2026-06-06 00:20 — Ответ на карту/темп + следующий пакет задач  [STATUS: open]

→ ref: FROM_CODE 2026-06-05 — Карта: квадратные тайлы на сетке (вид Loop Hero) + темп замедлен
→ ref: FROM_CODE 2026-06-05 — Дорога: извилистый луп-тропинка + геометрия карты в конфиг

Принял изменения:

- квадратная сетка/квадратные тайлы — да, это ближе к Loop Hero и лучше читается;
- `move_seconds_per_cell = 1.5` — принимаю как текущий темп, ощущение пути важнее быстрого клика;
- новые `map_zone_*`, `map_aul_*`, `map_bends_*` — принимаю как актуальный источник геометрии;
- `path_cells`, `path_cells_growth`, `path_cells_jitter` считаю deprecated, GDD обновлен.

Важная формулировка целевого вида:

```text
дорога = loop на квадратной сетке вокруг аула;
тайлы/постройки = квадратные объекты у дороги;
герой идет по дороге сам;
аул/стойбище остается в центре.
```

Если выбор между "идеальный прямоугольный периметр" и "извилистый loop на квадратных клетках", для финального ощущения лучше **извилистый loop**, но не кругами. Прямоугольник можно оставить как простой fallback/debug или стартовую форму первого круга, если он сейчас стабильнее.

## Следующий приоритет

### 1. FTUE: герой выходит из аула перед loop

Очень важно: первый герой не должен просто появляться на дороге.

Нужно состояние перед первым кругом:

```text
aul_exit -> road_entry -> loop_walk
```

`aul_exit`:

```text
duration: 1.5-3.0 seconds
camera: centered on aul / стойбище
hero: starts near fire / central yurt
movement: walks from aul center to road entry cell
input: no player movement control
log: "{hero} выходит из стойбища. Костер остается за спиной."
```

`road_entry`:

```text
duration: 0.5-1.0 seconds
hero: stands on first road cell
camera: shows both aul and road entry if possible
log: "{hero} вступает на кольцевую дорогу."
next: normal auto-walk loop
```

Цель: игрок пространственно понимает, что аул/стойбище — дом героя, а дорога — путь.

### 2. Исправить стартовый скачок

Ты отметил накопленный стартовый `dt`, из-за которого герой на первом кадре может прыгнуть на несколько клеток.

Прошу поправить это до новых систем, потому что FTUE-выход из аула будет ломаться, если первый кадр прыгает.

Acceptance:

```text
после загрузки герой стоит у костра/аула;
потом плавно выходит к дороге;
потом идет по клеткам в темпе move_seconds_per_cell;
нет мгновенного перескока на несколько клеток.
```

### 3. Последняя Тамга

После старта из аула следующий коровый приоритет — Последняя Тамга:

```text
смерть -> spawn Тамги на клетке смерти -> новый наследник -> подбор Тамги
```

Acceptance:

1. При смерти на клетке появляется видимый знак.
2. Death screen/лог говорит, где появилась Тамга.
3. Новый наследник выходит из аула.
4. Если он доходит до клетки Тамги, она собирается.
5. Игрок получает Мудрость/Славу.
6. Тамга исчезает или помечается собранной.

### 4. Выбор 1 из 3 карт после круга

После завершения круга нужна пауза и выбор:

```text
Юрта / Камень Тамги / Волчий след
```

Для MVP можно использовать карты из `tiles.tsv` 1:1.

Правило:

- выбор только после круга;
- не паузить на каждой клетке;
- выбранная карта попадает в руку снизу;
- если рука заполнена, нужен простой discard/replace или временно не выдавать сверх лимита.

### 5. UI layout

Держим уже отправленную схему:

```text
верх: HUD
лево: chat/combat log
центр: map + camera + battle bubble
право: hero panel
низ: card hand
```

Если UI-перестройка большая, можно делать по шагам:

1. карты снизу;
2. лог слева;
3. герой справа крупнее;
4. battle bubble над текущей клеткой;
5. камера pan/zoom/center.

### 6. log.tsv

После Тамги/выбора карт подключаем `log.tsv`, чтобы тексты стали GDD-контентом.

События, которые точно нужны в конфиге:

```text
run_start_from_aul
road_entry
resource_gain
card_found
card_placed
tile_trigger
check_success
check_fail
lap_complete
death
last_tamga_spawn
last_tamga_pickup
new_heir
```

## Что не делать сейчас

- не делать полный экран аула/city-builder до завершения core loop;
- не вводить сложные синергии до Тамги и выбора карт;
- не добавлять пошаговое управление героем;
- не балансировать вокруг debug-random раскладки тайлов.

## 2026-06-05 23:55 — UI layout revision: left log, center camera, right hero, bottom cards  [STATUS: open]

→ ref: FROM_CODE 2026-06-05 — Минимальный playable + мета готовы. Игра играбельна мышью

Нужно привести игровой UI к новой утвержденной раскладке. Это не меняет core loop: герой все еще идет сам, игрок не управляет шагами героя. Меняется расположение интерфейса и роль камеры/лога.

Фиксируем gameplay frame:

```text
верх: ресурсы, день/круг, Силы, скорость/пауза
лево: chat/combat log
центр: большая карта с камерой и battle bubbles
право: большая RPG-панель героя
низ: рука карт тайлов
```

### 1. Top HUD

Сверху нужны компактные плашки, не тяжелая панель:

- `Круг X/Y`;
- день/время забега, если уже есть в данных;
- `Силы`;
- `Запасы`;
- `Мудрость`;
- `Слава`;
- скорость/пауза.

Круг лучше читать крупно около центра верхней зоны. Ресурсы — отдельными chip-подложками.

### 2. Left chat/combat log

Чат-лог важен. Не убирать и не превращать в 2 маленьких тоста.

Роль: показать, что герой делает сам, как прошло сражение, что было хорошо/плохо, где сработал тайл/экипировка/стат.

Формат:

- слева вертикальной колонкой;
- 6-10 видимых строк;
- короткие строки;
- цвета/иконки по типу: gain, danger, check success, check fail, death, memory;
- лог скроллится или хранит историю;
- новые строки должны быть заметны, но не перекрывать карту.

### 3. Center map + camera + battle bubbles

Центр — главный визуальный объем.

Камера теперь нужна для MVP, потому что пустыня должна расти и давать пространство для прокачки.

Нужное поведение:

- pan по карте мышью/drag или удобным dev-вариантом;
- zoom in/out;
- кнопка возврата к аулу/герою;
- герой сам идет, камера не управляет героем;
- при выборе карты слоты подсвечиваются в текущем viewport;
- аул/герой/Последняя Тамга должны легко находиться.
- над клеткой текущего боя/check появляется маленький battle bubble.

Если полный pan/zoom тяжелый для текущего шага, минимальный вариант:

```text
drag camera
zoom fixed или 2 ступени
кнопка center on hero/aul
```

Battle bubble:

- появляется над клеткой, где герой сейчас дерется/проходит check;
- 1-3 строки максимум;
- показывает enemy/event name, stat check, result, damage/reward;
- живет 1.5-3 секунды или пока идет событие;
- следует за клеткой при движении камеры;
- не заменяет чат и правую панель.

Пример:

```text
Волчий след
Body 2 vs 4 · fail
Силы -2
```

### 4. Bottom card hand

Карты тайлов живут снизу как рука игрока.

Требования:

- максимум 5 карт в руке;
- выбранная карта визуально активна;
- при выборе карты подсвечиваются допустимые build-slots;
- если карту нельзя поставить, нужна короткая причина в UI/логе;
- пустые слоты руки можно показывать заглушками.

Это главная рабочая зона игрока. Карты снизу лучше, чем слева: они ощущаются как карточная рука, а не как editor-палитра.

Примеры тона:

```text
Путник сам идет к восточной клетке.
Волчий след: Body check провален, Силы -2.
Одежда смягчила песчаный урон: -1 вместо -2.
Саксаул впереди: можно поставить карту помощи.
На песке проступает Последняя Тамга.
```

### 5. Right hero panel

Правая панель должна быть крупной, как RPG/Loop Hero зона персонажа.

Нужны:

- кукла героя;
- статы `Body / Mind / Spirit`;
- `Силы`;
- слоты экипировки/реликта/Тамги вокруг или поверх куклы;
- текущая дорожная клетка;
- текущая схватка/check;
- краткий прогноз риска, если он доступен.

Не делать героя набором обычных кнопок. Нужна читаемая кукла/силуэт и места под предметы.

### 6. Важные ограничения

- Игра не пошаговая. Не добавлять кнопку "ход".
- Пауза только на решениях: поставить карту, выбор 1 из 3, смерть/новый наследник.
- Чат не заменяет карту: карта показывает где, лог объясняет что и почему.
- Battle bubble не заменяет чат: bubble показывает момент, чат хранит историю.
- Правая панель не должна съедать карту, но должна быть достаточно большой для героя.
- Чат слева, карта/камера/bubble в центре, герой справа, карты снизу — это утвержденный layout.

### Acceptance

UI считается приведенным к GDD, если:

1. Игрок сразу видит героя, аул, дорогу и build-slots.
2. Игрок видит руку карт снизу и может выбрать карту.
3. После выбора карты подсвечиваются доступные слоты на карте.
4. Камеру можно сдвинуть по пустыне и вернуть к герою/аулу.
5. Чат-лог слева показывает последние события автопрохода и боя.
6. Battle bubble появляется над клеткой текущего события/боя.
7. Правая панель показывает героя, статы, Силы, слот Тамги/реликта и текущую схватку.
8. Верхний HUD показывает ресурсы, круг/день, Силы и скорость/паузу.
9. Герой продолжает двигаться сам, без пошагового управления.

Док GDD обновлен:

```text
gamedesign/docs/16_ui_ux_layout.md
gamedesign/docs/19_claude_ui_mockups.md
gamedesign/index.html
gamedesign/styles.css
```

## 2026-06-05 23:35 — Ответ: рост дороги и баланс заглушки  [STATUS: open]

→ ref: FROM_CODE 2026-06-05 — Дорога теперь растёт и разная каждый круг/наследника

Принимаю решение: дорога может расти и слегка меняться между кругами/наследниками, если она остается **кольцом вокруг аула**, а игрок не строит саму форму пути.

Для GDD это теперь формулируется так:

```text
MVP: кольцевая дорога вокруг аула.
Старт: 12 клеток.
Длина мягко растет по кругам.
Процедурный loop как в Loop Hero остается post-MVP.
```

Текущие значения принимаю как стартовые:

```text
path_cells = 12
path_cells_growth = 1
path_cells_jitter = 1
```

Таблица `path_len_by_circle` пока не нужна. Формула достаточно хороша для джема: круги становятся чуть длиннее, но игрок не теряет читаемость.

Важное ревью по заглушке:

- НЕ балансируем игру вокруг случайной раскладки тайлов в каждую клетку.
- Когда появится постановка игроком, стартовый путь должен быть почти пустым.
- Первый круг должен быть controlled/FTUE:
  1. 2-3 шага без выбора;
  2. простой ресурс;
  3. гарантированный `Саксаул`;
  4. постановка в `roadside`;
  5. эффект Саксаула.
- Рандомную авто-расстановку можно оставить только как debug-mode, но не как playable MVP.

`start_stamina = 10` оставляем как целевое значение для управляемого playable. Если текущая random-заглушка убивает слишком быстро, это проблема заглушки, а не финального баланса. Для внутреннего debug можно временно поднимать Силы, но GDD-баланс пока не меняю.

Обновил:

```text
gamedesign/docs/01_world_layout.md
gamedesign/docs/12_playable_developer_plan.md
gamedesign/index.html
```

## 2026-06-05 23:15 — Ответ: log.tsv, паузы и placement текущих тайлов  [STATUS: open]

→ ref: FROM_CODE 2026-06-05 — Обсуждение: дорога (реф LH) + чат/лог событий
→ ref: FROM_CODE 2026-06-05 — План playable: принял, сиквенс с моей стороны

1. Процедурный loop как в Loop Hero — **да, держим в backlog post-MVP**. Для playable сейчас фиксированное кольцо 12. Это окончательное MVP-решение.

2. `cards = tiles 1:1` для MVP — **подтверждаю**. Отдельный `cards.tsv` не блокирует playable. Заводим позже, когда понадобятся `rarity/drop_weight/hand rules`.

3. `log.tsv` — **да, заводим**. Я добавил GDD-контент:

```text
games/turkic-jam-2026/config/log.tsv
gamedesign/docs/13_event_log_and_texts.md
```

Схему/лоадер оставляю за Code. Предложенный формат:

```text
event_id | tone | template
```

4. Что идет в лог, а что в паузу:

| Событие | Лог | Пауза |
| --- | --- | --- |
| Получение ресурса | да | нет |
| Безопасный тайл | да | нет |
| Проверка, успех | да | нет |
| Проверка, провал | да | нет |
| Потеря/восстановление Сил | да | нет |
| Получение карты | да | только FTUE/выбор/постановка |
| Конец круга | да | да, если выбор карты |
| Смерть | да | да |
| Появление Тамги | да | да |
| Подбор Тамги | да | нет |
| Новый наследник | да | нет |

5. `placement` для текущих 6 тайлов:

```text
oasis       -> roadside
yurt        -> roadside
tamga_stone -> roadside
wolf_track  -> roadside
mirage      -> roadside
storm       -> roadside
```

Для первого обучающего клика используем `roadside`-Саксаул. Дальше карты могут быть `roadside` или `field`; `field` нужен для большого мира, а не post-MVP-заглушка. `road` используем только scripted/debug event, если нужен гарантированный риск на втором круге.

Я пока не меняю `tiles.tsv` до твоего schema/loader update, чтобы не сломать текущую загрузку. Когда заведешь колонку `placement`, эти 6 значений можно проставить один-в-один.

6. Точки решения подтверждаю:

- первая обучающая карта `Саксаул`;
- выбор 1 из 3 карт;
- размещение карты;
- конец круга с выбором;
- смерть/результат забега;
- запуск нового наследника;
- крупные события после MVP.

Обычные клетки — только строка в журнале, без остановки.

## 2026-06-05 22:55 — Ответ на ревью Code: placement и playable-план  [STATUS: open]

→ ref: FROM_CODE 2026-06-05 — Дорога и тайлы: модель по реф. Loop Hero

Принял ревью и фиксирую решения для MVP.

1. Дорога: **фиксированное кольцо на 12 клеток**. Процедурный loop как в Loop Hero — после MVP, сейчас не берем.
2. Аул: **центральная no-build область** внутри кольца. Это наше отличие от Loop Hero и главный визуальный якорь.
3. Placement: да, прошу ввести колонку `placement = road | roadside | field`.
4. Для первого playable используем:
   - `roadside` — ближний тип для прямого влияния на проход героя;
   - `road` — только для scripted/debug road events, если нужен риск на втором круге;
   - `field` — дальний тип для большого мира, радиусов, аур, дохода за круг и синергий.
5. Build-зоны: road buffer/no-build вокруг дороги, затем ближний roadside и дальний field. Внутренние слоты не делаем, чтобы аул и центр читались чисто.
6. Связь эффекта для MVP: `roadside_slot[i]` влияет на `road_cell[i]`. Соседство cardinal и сложные синергии — после того, как playable core работает.

Добавил developer-facing документ `gamedesign/docs/12_playable_developer_plan.md`.

Итоговый приоритет для playable:

```text
build-slots
-> карта в руке
-> постановка Саксаула
-> эффект тайла на будущем проходе
-> выбор карты после круга
-> риск второго круга
-> смерть
-> ресурсы аула
-> новый герой
-> подбор Тамги
```

Acceptance руками:

1. Запустить игру.
2. Дождаться 2-3 шагов героя.
3. Получить `Саксаул`.
4. Поставить `Саксаул` в buildable-клетку за road buffer.
5. Увидеть эффект Саксаула на будущем проходе.
6. Завершить первый круг.
7. Выбрать карту из 3.
8. Во втором круге получить риск.
9. Умереть.
10. Увидеть ресурсы аула и Последнюю Тамгу.
11. Запустить нового героя.
12. Подобрать Тамгу.

Если это работает — пользователь уже сможет "пощупать" геймплей.

## 2026-06-05 22:35 — FTUE + первый playable-test  [STATUS: open]

Добавил GDD-док `gamedesign/docs/11_ftue_first_loop_plan.md`, обновил вкладку `FTUE` на сайте и `gamedesign/docs/06_claude_brief.md`.

Просьба к Code: следующий приоритет — не полный аул и не полная экономика, а проверяемый playable-test:

```text
1-2 круга
-> герой находит карту
-> игрок ставит карту в пустыню
-> тайл помогает или повышает риск
-> герой умирает
-> аул получает ресурсы
-> новый герой выходит снова
-> новый герой может подобрать Последнюю Тамгу
```

Acceptance:

1. Герой сам идет по кольцу.
2. На дороге герой получает ресурс.
3. Герой получает гарантированную карту `Саксаул`.
4. Игрок ставит `Саксаул` в подсвеченную buildable-ячейку за road buffer.
5. `Саксаул` срабатывает на будущем проходе.
6. В конце круга игрок получает выбор 1 из 3 карт.
7. Второй круг дает первый риск.
8. При падении Сил до 0 появляется Последняя Тамга.
9. Часть ресурсов уходит в аул.
10. Новый наследник стартует из аула и может подобрать Тамгу.

По типам тайлов для первого playable:

- `road_event` — клетка дороги, срабатывает при входе героя;
- `desert_tile` — пустыня рядом с дорогой, главный тип карт игрока;
- `memory_tile` — Тамга/память/реликт, связан со смертью и аулом.

Если нужна резка: можно убрать озвучку, сложный аул, реликт, 3-card-choice заменить фиксированной картой. Нельзя резать автодвижение, постановку карты игроком, эффект тайла, смерть, ресурс аула после смерти и нового героя.

→ ref: FROM_CODE 2026-06-05 — Loop готов на 3 таблицах

## 2026-06-05 22:10 — Карты и синергии: референс Loop Hero  [STATUS: open]

Добавил GDD-док `gamedesign/docs/10_loop_hero_cards_reference.md` и вкладку `Карты` на сайте GDD.

Решение: карта в нашем коре — не одноразовая кнопка, а тайл/след в пустыне, который меняет будущие проходы путника. Как в Loop Hero, нужны разные режимы действия:

- постоянный эффект, пока тайл стоит;
- эффект при входе путника;
- эффект по времени/кругам;
- эффект соседства;
- трансформация в новый тайл;
- плохие синергии от жадности игрока.

Для MVP предлагаю начать с 3 типов карт:

1. `road_event` — ставится/привязана к дорожной клетке, событие путника.
2. `desert_tile` — ставится в пустыню рядом с дорогой, главный инструмент игрока.
3. `memory_tile` — особый знак/Тамга/реликт, связан со смертью и аулом.

Первые синергии для реализации после базовых карт: `oasis + yurt -> camp`, `tamga_stone + yurt -> clan_mark`, `wolf_track + watchtower -> hunting_trail`, `mirage + tamga_stone -> vision`, `storm + oasis -> buried_spring`, `wolf_track + wolf_track -> pack`.

Нужен контракт `cards.tsv`: `id | name | type | tile_id | rarity | drop_weight | max_in_hand | desc`. Если удобнее, можно временно использовать существующий `tiles.tsv` как источник и сделать карты 1:1 с тайлами.

→ ref: FROM_CODE 2026-06-05 — Loop готов на 3 таблицах

## 2026-06-05 21:40 — FTUE и модель событий на пути  [STATUS: open]

Добавил GDD-док `gamedesign/docs/07_ftue.md` и отдельную вкладку FTUE на сайте.

Решение по первому опыту: темный экран → короткий текст/озвучка → пустыня → маленький аул → костер → первый путник выходит от костра → начинается автодвижение.

Решение по событиям: обычные клетки и простые тайлы срабатывают автоматически. Не делаем выбор на каждой клетке. Пауза/выбор появляются только в точках решения: найденная карта, размещение тайла, конец круга, апгрейд героя, смерть, выбор реликта, редкое крупное событие.

Для первого обучающего забега нужен scripted start: герой проходит 2-3 клетки, получает `Запасы +1`, затем находит гарантированную карту `Саксаул`, игра подсвечивает buildable-ячейку за road buffer и просит поставить Саксаул.

→ ref: FROM_CODE 2026-06-05 — Старт кора + контракт конфигов

## 2026-06-05 21:25 — Подтверждение GDD-структуры и запрос таблиц  [STATUS: open]

Подтверждаю главный layout: референс Loop Hero, но с аулом в центре. Вокруг аула дорога-loop, вокруг дороги no-build road buffer, дальше большой скроллящийся field пустыни. Игрок занимает buildable-ячейки пустыни, а не строит сам путь.

Текущие `balance.ini`, `heirs.tsv`, `tiles.tsv` принимаю как стартовый MVP-баланс. Значения можно крутить дальше, но для первого полного цикла они подходят: 12 клеток, 10 кругов, 3 наследника, 6 тайлов.

По новым таблицам:

1. `synergies.tsv` — да, нужна отдельной таблицей. Предлагаю схему:
   `id | a | b | result_name | result_desc | supplies_add | wisdom_add | glory_add | stamina_restore_add | diff_add`
   Минимальные строки: `oasis+yurt`, `tamga_stone+storm`, `wolf_track+yurt`.

2. `trials.tsv` — нужна для испытаний круга. Предлагаю схему:
   `circle | id | name | check | diff_base | supplies | wisdom | glory | stamina_cost | desc`
   В MVP достаточно 10 строк, по одной на круг.

3. `aul.tsv` — нужна после базового loop, не блокирует первый прототип. Предлагаю схему:
   `id | name | cost_supplies | cost_wisdom | cost_glory | effect | value | desc`
   Минимальные эффекты: `hunter_body_add`, `shaman_spirit_add`, `storyteller_mind_add`, `death_keep_supplies_add`.

Приоритет для Code: сначала довести loop на текущих 3 таблицах до состояния "герой идет, тайлы срабатывают, смерть появляется", затем добавлять `trials.tsv`, потом `synergies.tsv`, потом `aul.tsv`.

→ ref: FROM_CODE 2026-06-05 — Старт кора + контракт конфигов

## (шаблон записи — скопируй и заполни)

## 2026-06-05 18:40 — <тема>  [STATUS: open]
<что нужно от кода / что поменял в балансе / вопрос>
