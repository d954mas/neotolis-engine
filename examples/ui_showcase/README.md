# ui_showcase

A Druid-style tabbed vitrine that supersedes the older per-phase UI demos
(the buttons, stateful-widget, and theme demos). It is the single source
of truth for every `nt_ui` widget, the modal helper, and the theme hot-swap
pattern. Builds and runs identically on native (GLFW) and WASM (WebGL2).

## Layout

A static-const registry (`g_tabs[]`) of `{name, info, code_url, render fn,
nullable props_fn}` entries drives a **left tab list -> right content stage**.
The stage is wrapped in a scroll container (exercises the scissor stack). A tab
that sets a `props_fn` also renders a focused live properties panel beside its
content; all other tabs render no panel.

The left tab list itself **dogfoods the reusable `nt_ui_tabbar`** widget (the game
owns the active-tab index; the widget draws the accent bar + selected fill + hover
lighten and writes the index on click).

## Tabs (15 entries)

1. **Labels** - h1 / body / caption variants, themed via the palette.
2. **Buttons** - six cells: standard (idle/hover/pressed/disabled) / exaggerated
   scale / per-state art swap (blue idle / green hover / red press) / no-pad
   touch-target / icon button / disabled.
3. **Buttons: Transform** - a single button wrapped in a live rotation / scale /
   offset transform driven by a properties panel; the click counter proves the
   transform-aware (inverse-affine) hit-test still clicks while it is rotated,
   scaled, and offset.
4. **Images & Slice9** - slice9 panels at several sizes (corners stay crisp) +
   a live insets/size properties panel.
5. **Toggles & Radios** - checkbox + exclusive radio group + sliding toggle.
6. **Sliders & Progress** - float + int sliders + a progress bar with a live
   value / auto-animate properties panel.
7. **Scroll** - four independent (non-nested) scroll containers in a 2x2 grid:
   vertical AUTO_HIDE bar / vertical ALWAYS bar / horizontal-only / both axes (XY).
8. **Modals** - confirm modal + nested depth-2 modal + a live transition panel.
9. **Input** - plain / numeric-filtered / password-masked / Cyrillic text fields
   (`nt_ui_input_text`); see the **Input controls** table below.
10. **Events** - a hold-to-confirm button (`nt_ui_events` gesture cfg) whose
    `hold_progress` drives a fill bar and confirms on `long_pressed`, plus a
    double-click target with a readout; see the **Interaction-events controls** below.
11. **Dropdown** - the **immediate** combo (`nt_ui_combo_begin`/`selectable`/`end`):
    a short list (icon gutter), a long scrolling list (more than `max_visible_rows`)
    that flips up near the window bottom, and a custom swatch-trigger combo
    (`nt_ui_combo_preview_begin`/`end`).
12. **Tooltip** - timed hover-reveal tooltips on popup-core (no catcher, so they
    never block clicks on the targets underneath).
13. **Menu** - the **immediate** context menu (`nt_ui_menu_begin`/`item`/`item_ex`/
    `submenu_begin`/`separator`/`item_begin`/`end`) on a right-click / long-press: a
    rich row (icon + `Ctrl+N` shortcut), a checkmark-toggle row, a disabled item, a
    nested **submenu**, and a custom `activatable=false` row whose inner button owns
    the click. Mouse-aim hover-intent, per-level edge-flip, nested dismiss, keyboard nav.
14. **Tabs** - the reusable `nt_ui_tabbar` begin/end **core** dogfooded: icon+text
    tabs with a distinct selected-tab icon + a BOTTOM accent (contrast the LEFT nav
    list, which uses the one-call `labels[]` wrapper with a LEFT accent).
15. **Stress** - N labels @14pt + the frame `gpu_ms` / draw-call readout.

## Controls

| Input | Action |
|-------|--------|
| Left tab list | select the active tab (state per tab is retained) |
| **T** key / header **Theme** button | dark <-> light hot-swap |
| **D** key | toggle the inspector overlay |
| **Esc** (native) | unfocus the focused field; else quit |
| **Esc** (modal up) | close the TOP modal only |
| backdrop click (modal up) | close-on-backdrop (the backdrop blocks click-through) |

## Input controls (Input tab)

Each field edits a **game-owned** `char[]` buffer in place (`nt_ui_input_text`, ImGui-style); the
engine state pool holds only the caret / selection / scroll / blink, never the string.

| Input | Action |
|-------|--------|
| click a field | focus it (the bg/border brighten); the caret blinks |
| type | inserts at the caret — Latin **and** Cyrillic (the demo font bakes both) |
| Left / Right | move the caret one **codepoint** (never splits a multi-byte char) |
| Home / End | caret to start / end |
| Backspace / Delete | delete the codepoint before / after the caret |
| Shift+arrows / Shift+Home/End | extend the selection |
| mouse drag | select a range; **double-click** selects a word; **Ctrl+A** selects all |
| Ctrl+C / Ctrl+X / Ctrl+V | copy / cut / paste via the real `nt_clipboard` (paste is filtered + clamped) |
| **Tab** | advance focus to the next field (wraps to the first) |
| **Esc** | unfocus the field (a modal-less field; otherwise Esc closes the top modal first) |

Per-field behavior:

- **Plain text** — any printable codepoint.
- **Numeric only** — an `nt_ui_filter_numeric` allow-predicate rejects everything but `[0-9.+-]`
  (typed letters and pasted letters are dropped); web gets the numeric soft-keyboard hint.
- **Password (masked)** — renders one mask glyph per codepoint instead of the text (render-only;
  the buffer is untouched); web gets `type=password`.
- **Cyrillic** — pre-filled with a Cyrillic string to exercise multi-byte UTF-8 edit + measure + render.

## Interaction-events controls (Events / Dropdown / Tooltip / Menu tabs)

These tabs wire the interaction events + app-widgets. All widget state is
**game-owned** (Model D); the engine owns only the gesture / hover-delay / popup cells.

| Input | Action |
|-------|--------|
| **Events** — press and HOLD the button | the fill bar ramps 0→1 over ~1.5s (`hold_progress`) and confirms at the top (`long_pressed`) |
| **Events** — drag off the button mid-hold | resets the fill (no confirm) — release-outside / move past the drag radius cancels |
| **Events** — double-click the second button | increments the double-click readout |
| **Dropdown** — click a trigger | toggles the list open; click a row to select (the game writes its own index) |
| **Dropdown** — the swatch trigger | a custom-content trigger (swatch + label) via `combo_preview_begin`/`end` |
| **Dropdown** — scroll the long "city" list | wheel / drag the scroll wrapper; opening near the window bottom flips the list ABOVE the trigger |
| **Tooltip** — hover a target ~0.5s | the tooltip reveals; it hides the instant the cursor leaves and never blocks the click underneath |
| **Menu** — right-click / long-press the panel | opens the **context menu** at the cursor |
| **Menu** — hover "More", travel diagonally into the submenu | the **submenu stays open** while the cursor aims at it, even crossing a sibling (mouse-aim triangle) |
| **Menu** — open near the right / bottom border | per-level **edge-flip** keeps each level on screen |
| **Menu** — Esc / click outside / arrows + Enter | Esc closes the deepest level; outside-click dismisses the whole chain; arrows navigate, Enter activates a leaf / opens a parent |

## Visual-QA protocol

The GL surface is **not reliably headless-capturable** here, so the interaction +
visual behavior of these tabs is verified by the **user's eyes** at the BLOCKING
visual-QA gate — there is no automated screenshot regression for them. Build + run
the native showcase (see **Build & run** below), then walk the Events / Dropdown /
Tooltip / Menu tabs and the dogfooded tab list per the table above, and confirm the
Dark/Light (**T**) toggle restyles every new tab. Confirm no regressions on the
pre-existing tabs (Buttons, Modals, etc.).

## Theme hot-swap

The whole hot-swap is a single `g_current` pointer flip over a game-owned
`ui_palette_t` of per-widget style **pointers** (label / button / checkbox /
slider / progress / scroll / **modal**), with `dark` and `light` variants. There
is **no engine API in the swap** (Model D). Pressing **T** or the header button
flips the pointer; every widget — including the modal — restyles on the next
frame.

## Focused properties panels

- **Modal** (the headline panel): a segmented transition selector
  (scale-pop / fade / slide), plus sliders for ease speed, scale-start
  (~0.85..1.0), and backdrop alpha (0..1). The panel writes into a **runtime**
  `nt_ui_modal_style_t` that the Modals tab passes to `nt_ui_modal` each frame,
  so the open/close animation visibly tracks the panel as you drag.
- **Slice9**: per-side insets L/R/T/B + target W/H drive a live
  `NT_UI_IMAGE_SLICE9_OVERRIDE` emit.
- **Progress**: a value slider + an auto-animate toggle.
- **Stress**: a segmented label count (500 / 1500 / 3000 / 6000) driving the loop.

## Slug measurement protocol (Stress tab)

Open the **Stress** tab and read the **frame `gpu_ms`** overlay. Pick a label
count with the panel (500 / 1500 / 3000 / 6000) and watch the frame `gpu_ms` and the
live `ui_draw_calls` readout respond.

**GL-timer latency:** `gpu_ms` comes from an asynchronous `GL_TIME_ELAPSED`
query, so the displayed value lags the current frame by a frame or two — after
you change the label count the reading settles one or two frames later. It is an
honest informational proxy, not an exact same-frame measurement.

**GPU-timing limitation (no-nest):** `GL_TIME_ELAPSED` query segments **cannot
nest** (`engine/graphics/gl/nt_gfx_gl.c:483` asserts no-nest). The host frame
loop already wraps the whole frame in one `nt_gfx_begin_segment("frame")`
segment, so an *isolated* `ui_text` segment is impossible. The Stress tab is
therefore a **text-only** tab and surfaces the **frame** `gpu_ms` as the honest
Slug-cost proxy — not a nested per-widget timing.

**WebGL2 note:** `nt_stats_get_gpu_ms()` returns `-1.0` when the
`EXT_disjoint_timer_query` extension is absent (common on WebGL2). The overlay
guards `< 0` and shows `gpu: n/a` instead of garbage.

**Threshold is INFORMATIONAL only.** The historical target was `< 4 ms` of text
GPU cost on a mid-tier mobile device. Exceeding it does **not** block this phase
— it merely files a v1.9 bitmap-font-fallback issue. The frame `gpu_ms`
proxy is reported for that decision, not as a gate.

## Batching evidence

The header shows a live `ui_draw_calls` readout. Batching is driven by
**explicit `nt_ui_layer_t`** (0..255, lower draws first) — there is **no
sort-by-material toggle** (`NT_UI_WALK_SORT_BY_MATERIAL` does not exist). The
draw-call count is the batching evidence.

## Build & run

```sh
# native-debug
cmake --build build/_cmake/native-debug --target ui_showcase
build/examples/ui_showcase/native-debug/ui_showcase.exe

# wasm-debug (paired) then serve and open in a browser
```
