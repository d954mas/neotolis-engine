# Pass 11 Generated HUD Icons Delivery Review

Status: delivered as candidate generated bitmap runtime art.

## Source

Generated bitmap source:

```text
gamedesign/assets/concept/pass_11_generated_hud_icons/pass_11_hud_icons_chroma_source.png
```

Runtime slicing tool:

```text
gamedesign/tools/pass_11_generated_hud_icons_runtime.py
```

Review contact sheet:

```text
gamedesign/assets/concept/pass_11_generated_hud_icons/pass_11_generated_hud_icons_contact_sheet.png
```

## Runtime Files Replaced

All files are exact `32x32 RGBA` with non-empty alpha:

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

## Technical Validation

```text
all files exact 32x32
all files RGBA
all files non-empty alpha
contact sheet includes native 32x32 and gameplay 24x24 preview
```

Pack builder:

```text
Optional Batch A sprites: found 44 / missing 0
Optional Batch B sprites: found 47 / missing 0
Optional Batch C aul progression sprites: found 6 / missing 0
Optional Batch C hero archetype panels sprites: found 3 / missing 0
Optional Pass 6 future library sprites: found 21 / missing 0
Optional production sprites: found 121 / missing 0
Atlas packed: 125 sprites
Generated merged header: 132 assets
CRC32: 0x7A1CE979
```

Desktop L5:

```text
Command: .\turkic_jam.exe --visual-qa --dump-frame C:\projects\neotolis-engine-turkic-jam-2026\tmp\visual_qa_l5_pass11_hud_icons.png --exit-after-frame
Output PNG: tmp/visual_qa_l5_pass11_hud_icons.png
Resolution: 1280x720
Checksum: 0x6371E58C
Blank/white/black: no
Runtime status: Batch A 44/44, Batch B 47/47, Batch C aul 6/6, Batch C hero panels 3/3, Pass 6 future 21/21
```

## GDD Review

Decision:

```text
Pass 11 is accepted as candidate generated bitmap runtime art.
Pass 11 is not final accepted production art until user/GDD reviews actual gameplay readability.
```

What works:

- Top HUD icons now share the same generated/painterly direction as cards, active tiles, hero and equipment.
- Icons are readable in the QA harness at 24px/32px scale.
- Future strip icons are visible and no longer pure technical placeholders.
- No new ids, filenames, balance rows, layout contracts, or pack schema changes were introduced.

Risks:

- `icon_body_32` reads more like a vest/armor than a pure Body stat; acceptable for now, but review in the final right-panel/stat UI.
- `icon_glory_32` reads as a banner/standard rather than abstract glory; acceptable for the nomadic visual language.
- `icon_last_tamga_32` and `icon_warning_32` contain fictional mark-like shapes. They must remain fictional and must not be treated as researched real tamgas.
- Some icons have more detail than ideal for 24px. If HUD becomes noisy, simplify silhouettes in a targeted repaint.

## Next Gate

No new Code task is opened by this pass because the Code thread is paused during map migration.

After the map migration finishes, request one normal gameplay screenshot with:

```text
top HUD resources/day/circle/speed
right hero panel stats/equipment
bottom card hand
future/debug strip only if still relevant
```

Final acceptance depends on user/GDD readability review in that normal gameplay context.
