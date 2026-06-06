# 83. Normal gameplay visual review request

Status: active GDD request for the next visual QA pass.

## Why

Road/buffer is now accepted in normal gameplay. The next full-art goal needs evidence for the rest of the visible runtime assets in a real gameplay composition, not only isolated QA sheets.

Current reviewed screenshot:

```text
tmp/pass13_v4_road_buffer_runtime.png
```

This screenshot proves road/buffer integration, but it does not show enough of the current art library to approve:

```text
all P0 card art
all P0 active tile objects
all HUD/stat icons
equipment kit readability
selected/empty/invalid card states
placement feedback states
```

## Current Screenshot Notes

Accepted:

```text
road/buffer readable enough for current playable
HUD Cyrillic is readable
top resource chips are readable
left log is readable
right hero panel basic structure is readable
Saxaul card is readable as first playable card
```

Visible risks to review deeper:

```text
empty card backs are noisy and the word "пусто" is low-contrast
right hero panel uses useful content but needs hierarchy review with more states
active tile objects need map-context review, not raw/contact review only
card hand needs a filled-hand screenshot, not one card plus empty backs only
HUD icons need review when resources/circle/day/speed values vary
```

Do not start a broad new UI generation pass for these risks. First produce evidence.

## Required Code Screenshot

Create a desktop/native normal gameplay or QA-gameplay screenshot at 1280x720 or 1920x1080 that shows:

```text
map with road/buffer/aul visible
hero visible on or near road
at least 6 active tile objects placed in map context:
  saxaul
  yurt
  tamga_stone
  wolf_track / beast trail
  mirage
  storm
card hand filled with at least 5 cards:
  saxaul
  yurt
  tamga_stone
  beast trail
  one empty/back state or another P0 card
one selected card state
right hero panel with doll, 4 equipment items and stats
top HUD with supplies/wisdom/glory/circle/day/stamina/speed
left log with 4-6 readable Russian lines
```

This can be an explicit visual QA scene or a deterministic gameplay QA config. It does not need to be a balanced real run. It must use the same runtime rendering path and current raw assets.

## Acceptance Criteria

```text
desktop/native screenshot, not stale WASM
all visible Russian text readable
no SVG-looking fallback art in visible production assets
no one-off new UI art added for the screenshot
Pass 12 UI surfaces are reused for panels/buttons/cards/slots/chips
map and UI do not overlap
card art and active tile art are readable at gameplay scale
hero/equipment panel hierarchy is understandable
```

## Art Rule For Follow-Up

After GDD reviews the screenshot, Art may only produce targeted generated-bitmap fixes for failed assets.

Art must not generate:

```text
new standalone button
new standalone card frame
new standalone panel
new broad UI kit
new road/buffer pass
```

Any future fix must start from:

```text
Can Pass 12 solve this?
Which existing source family is reused?
Which exact runtime files change?
What screenshot failure does this fix?
```
