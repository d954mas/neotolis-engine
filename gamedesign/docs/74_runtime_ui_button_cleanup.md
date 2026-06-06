# 74. Runtime UI button cleanup

Status: delivered cleanup for visual design system consistency.

Purpose: remove old one-off demo button assets from the game runtime pack and keep game UI on the Pass 12 generated material system.

## Decision

Old Kenney/demo button regions are removed from `turkic_jam`:

```text
button_blue
button_green
button_red
```

The game now uses the generated Pass 12 button surface:

```text
ui_button_dark_64
```

for `tj_button()` across primary, secondary and danger variants. Variant difference is now a small tint/state adjustment on the same material family, not separate unrelated button art.

## Removed Runtime Files

```text
games/turkic-jam-2026/raw/ui/button_blue_depth.png
games/turkic-jam-2026/raw/ui/button_green_depth.png
games/turkic-jam-2026/raw/ui/button_red_depth.png
```

## Code Changes

```text
games/turkic-jam-2026/build_packs.c
games/turkic-jam-2026/main.c
games/turkic-jam-2026/src/game.h
games/turkic-jam-2026/src/ui_kit.c
games/turkic-jam-2026/src/ui_kit.h
games/turkic-jam-2026/generated/turkic_jam.h
games/turkic-jam-2026/generated/turkic_jam_assets.h
```

## Verification

```text
cmake --build build/_cmake/native-debug --target build_turkic_jam_packs
build/games/turkic-jam-2026/native-debug/build_turkic_jam_packs.exe build/games/turkic-jam-2026
clang-format --dry-run --Werror games/turkic-jam-2026/build_packs.c games/turkic-jam-2026/main.c games/turkic-jam-2026/src/game.h games/turkic-jam-2026/src/ui_kit.c games/turkic-jam-2026/src/ui_kit.h
cmake --build build/_cmake/native-debug --target turkic_jam
runtime dump: tmp/ui_button_cleanup_check.png
```

Search verification:

```text
rg "BUTTON_BLUE|BUTTON_GREEN|BUTTON_RED|btn_blue|btn_green|btn_red|button_blue_depth|button_green_depth|button_red_depth" games/turkic-jam-2026
```

returns no matches.

## Remaining Visual Issue Found

The cleanup screenshot still shows mojibake in some Russian UI text. This is not caused by button cleanup, but it is a visible UI quality issue and should be handled as a separate text/font/encoding pass.
