# Pass 8 Generated Card Art Delivery Review

## Decision

User-facing production art must be generated bitmap or painted-source bitmap. SVG-like/script/procedural art is rejected as final game art.

Script/vector/procedural assets may still be used for:

- technical placeholders;
- masks and slicing helpers;
- UI debug scaffolding;
- QA-only layout proof;
- runtime composition tests.

They are not accepted as final card/tile/hero/gameplay art unless explicitly approved after L5 screenshot review.

## Delivered Generated Source

Generated bitmap source sheet:

```text
gamedesign/assets/concept/pass_8_generated_bitmap_repaint/pass_8_card_tile_source_sheet.png
```

Runtime/contact review sheet:

```text
gamedesign/assets/concept/pass_8_generated_bitmap_repaint/pass_8_generated_card_art_runtime_contact_sheet.png
```

## Runtime Files

All delivered card art files are `64x64 RGBA`:

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

## Pack Evidence

After rebuilding the game pack:

```text
Optional production sprites: found 121 / missing 0
Atlas: 125 sprites
Generated merged header: 132 assets
CRC32: 0x0907AC0C
```

The generated card art ids are present in the atlas/header and bind at runtime.

## L5 Runtime Evidence

Fresh desktop/native dump:

```text
tmp/visual_qa_l5_pass8_cards.png
1280x720
all_black=0
all_white=0
all_same=0
rgb_range=0..255
```

Runtime bind status:

```text
Batch A 44/44
Batch B 47/47
Batch C aul 6/6
Batch C hero panels 3/3
Pass 6 future 21/21
```

## GDD Visual Review

Accepted for direction:

- the source is now generated bitmap, not SVG/procedural-looking art;
- `saxaul`, `yurt`, `tamga_stone`, `oasis`, `well`, `watchtower` are good candidates for first playable review;
- runtime pack integration works.

Not accepted as final:

- current game-scale hand cards still read mostly as card surface plus text;
- generated art is too small in `scene_visual_qa.c::card_preview` and `view.c::hand_card`;
- card art must become the main visual block of each card;
- `mirage` is too decorative/abstract at small size;
- `storm` is too soft/pink for danger until checked in improved card layout;
- `wolf_track` / beast trail needs readability and meaning review at real card scale.

## Code Request

Priority for Code is card presentation, not more registry work:

```text
card surface = background
generated art = central 64-72 px image
placement icon = small corner
count/selected badge = small opposite corner
card name = readable bottom text
empty slots = card backs
card dimensions = stable
```

Required next proof:

```text
tmp/visual_qa_l5_pass8_cards_layout.png
```

Do not call Pass 8 final accepted until the improved card layout screenshot is reviewed.

