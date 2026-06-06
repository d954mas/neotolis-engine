# 56. Generated bitmap art audit

Status: active audit.

Owner: GDD / Art lead.

Audience: GDD / Art / Code.

## Why

The user clarified that final visual production should use generated art, not script-shaped SVG-like placeholders. Document `54_art_source_policy_generated_bitmap.md` makes generated bitmap / painted source the baseline for future candidate-final production art.

This audit separates three states that were previously mixed:

```text
pipeline-ready runtime PNG
candidate technical art
generated bitmap / painted art candidate
```

## Current decision

The current art set remains valuable and should stay registered/integrated while Code proves L5 capture. However, final art acceptance is not allowed until both are true:

1. L5 desktop pixel/readability proof exists.
2. The asset either comes from generated bitmap / painted source, or GDD explicitly accepts it as a technical UI/icon exception.

## Pass source audit

| Pass | Scope | Current source type | Current status | GDD decision |
| --- | --- | --- | --- | --- |
| Production Batch A/B/C placeholder kits | initial raw folders and ids | deterministic script placeholders | pipeline-ready only | not final art |
| Pass 1 | first UI/card/saxaul/stamina/supplies/hero idle + continuation | deterministic/script repaint | candidate technical art, L5 pending | needs L5; likely targeted bitmap repaint for card art/hero/tile if readability or beauty fails |
| Pass 2 | hero walk/panel, equipment, HUD/card utility icons | deterministic/script repaint | candidate technical art, L5 pending | needs L5; hero/equipment may need bitmap repaint, utility icons may remain technical if readable |
| Pass 3 | world map, road, buffer, aul, active tiles | deterministic/script repaint | candidate technical art, L5 pending | needs L5; active tile objects and aul should be reviewed for bitmap repaint need |
| Pass 4 | FX frames | deterministic/script repaint | candidate technical art, L5 pending | acceptable if motion/readability works; repaint only if L5 shows weak FX |
| Pass 5 | aul upgrade stages | deterministic/script repaint | candidate technical art, QA-only L4, L5 pending | likely needs generated bitmap pass if upgrades become player-facing |
| Pass 6 | future tiles/cards/icons | deterministic/script repaint | candidate technical art, QA-only L4, L5 pending | card art and landmark tiles likely need generated bitmap repaint before production use |
| Pass 7 original | hero archetype panels | deterministic/script repaint | rejected by user as SVG-like | superseded |
| Pass 7 corrected | hero archetype panels | generated bitmap source -> cleanup/slice | candidate generated bitmap art, L5 pending | current preferred production direction |
| Pass 8 cards | first/future card art source and 10 runtime card PNGs | generated bitmap source -> cleanup/slice | candidate generated bitmap card art, card-layout L5 pending | correct source direction; review mirage/storm/beast trail after card UI screenshot |

## Technical exceptions

The following may remain deterministic/scripted if L5 proves readability:

- 9-slice UI panels, chips, buttons and slots;
- tiny utility icons where clean silhouette matters more than painterly detail;
- simple FX frames where motion and clarity matter more than illustration;
- validation/debug assets.

The following should generally use generated bitmap / painted source before final acceptance:

- hero portraits/panel dolls;
- card art;
- active tile objects with identity;
- aul progression art;
- narrative/memory/death visuals;
- future heir/archetype UI.

## Next Art direction

Do not start a broad blind repaint while Code is solving L5 capture. The next art pass should be targeted from actual runtime pixels.

Expected next pass after L5:

```text
Pass 8 - targeted generated bitmap repaint from L5 defects
```

Pass 8 card-art source and runtime contact sheet already exist:

```text
gamedesign/assets/concept/pass_8_generated_bitmap_repaint/pass_8_card_tile_source_sheet.png
gamedesign/assets/concept/pass_8_generated_bitmap_repaint/pass_8_generated_card_art_runtime_contact_sheet.png
```

Current Pass 8 card-art watchlist after GDD review:

```text
raw/cards/card_art_mirage_64.png
raw/cards/card_art_storm_64.png
raw/cards/card_art_wolf_track_64.png
```

Do not repaint these blindly. Wait for `tmp/visual_qa_l5_pass8_cards_layout.png` or equivalent L5 card-layout evidence, because the current card UI may make the art too small.

Likely candidates:

- card art that reads like icons instead of painted cards;
- hero/equipment panel if too flat;
- active tile objects that look procedural;
- aul stages if they become player-facing;
- any 24px icon that fails silhouette readability.

## Acceptance rule

An asset can become final accepted only after:

```text
generated/source-policy accepted
-> exact runtime PNG
-> builder found
-> runtime bind/drawn
-> desktop L5 screenshot/readability accepted
```

Until then, use the precise status:

```text
candidate technical art
candidate generated bitmap art
runtime integrated
L5 accepted
```
