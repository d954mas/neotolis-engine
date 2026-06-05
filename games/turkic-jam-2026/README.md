# Turkic Jam 2026 — game base

A ready-to-extend game template on the Neotolis Engine (C17, WebGL2/WASM).
Goal: arrive at the jam with scenes, menu, localization, save, and juice already
working — then only build the actual game.

## Layout

```
games/turkic-jam-2026/
  main.c                 engine bootstrap + render services + scene loop
  build_packs.c          offline pack builder (shaders + atlas + font)
  generated/             codegen headers from the pack builder (committed)
  raw/ui/                Kenney CC0 button art (slice9)
  src/
    game.h               game_ctx_t (shared state) + layer/ref constants
    scene.h              scene vtable (on_enter / on_update / on_exit)
    i18n.{h,c}           EN/RU/TR localization tables
    save.{h,c}           key/value persistence (web localStorage + native file)
    rng.{h,c}            seeded xorshift RNG
    juice.{h,c}          easing curves + trauma screen-shake
    ui_kit.{h,c}         tj_button() + label styles ("big & bright")
    scenes/
      scene_menu.c       title + START + Settings + language
      scene_game.c       tappable score + best, shake, P=pause, Lose
      scene_settings.c   language picker + reset progress
      scene_pause.c      pause overlay (scene swap)
      scene_gameover.c   final score + retry
```

## Architecture

- **main.c owns the engine.** Scenes never touch engine init; they receive
  `game_ctx_t *g` and only build UI + game logic.
- **Scene loop:** `main` builds the shared shell (dark backdrop + centered card)
  and calls `g->scene->on_update`. Transitions are requested with
  `game_goto(g, &SCENE_X)` and applied at the next frame boundary.
- **Render is gated** on resources (atlas + font + materials) being ready, so the
  first frames during async pack load are safe.

## Add a scene

1. Create `src/scenes/scene_foo.c` with an `on_update` and
   `const scene_t SCENE_FOO = {...};`.
2. `extern const scene_t SCENE_FOO;` in `src/game.h`.
3. Add the file to `add_executable(turkic_jam ...)` in `CMakeLists.txt`.
4. `game_goto(g, &SCENE_FOO)` from anywhere.

## Add a localized string

1. New key in `i18n_key_t` (before `T_COUNT`) in `src/i18n.h`.
2. Fill EN/RU/TR rows in `src/i18n.c`. New glyphs → extend `TJ_CHARSET` in
   `build_packs.c` and rebuild packs.

## Save data

`save_set_int/str` + `save_flush`. Keys used: `lang`, `best`. Web persists to
`localStorage["turkic_jam_save"]`; native to `turkic_jam_save.txt` (cwd).

## Build & run (VS Code)

- **Build & run turkic_jam packs (debug)** — generate `.ntpack` + headers.
- **Debug Desktop turkic_jam** — native window.
- **Debug WASM turkic_jam** — build wasm, serve on `:8100`, open Chrome.
- **Package turkic_jam web (zip)** — zip the release web build for itch.io.

Controls: mouse = buttons, `L` = language, `P` = pause, `Esc` = quit (native).
