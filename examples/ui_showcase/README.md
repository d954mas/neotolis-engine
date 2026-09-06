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

## Tabs (18 entries)

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
5. **Toggles & Radios** - checkbox + exclusive radio group + sliding toggle +
   a **tristate "select all"** (`nt_ui_checkbox_tri`) whose parent reflects the
   children (all on / all off / indeterminate MIXED dash); see the **New-widget
   controls** + **Visual-QA protocol** below.
6. **Sliders & Progress** - float + int sliders + a **vertical** volume/mixer
   slider (`NT_UI_SLIDER_VERTICAL`, BOTTOM_UP fill) + a progress bar with a live
   value / auto-animate properties panel.
7. **Scroll** - four independent (non-nested) scroll containers in a 2x2 grid:
   vertical AUTO_HIDE bar / vertical ALWAYS bar / horizontal-only / both axes (XY).
8. **Virtual List** - two `nt_ui_vlist` clippers over a **10,000-row** dataset
   (a vertical column + a horizontal strip); each owns ONE scroll / ONE Clay clip
   and renders only the visible window, so cost ~ the visible count, not 10k. A
   header readout shows the live window vs the total; see the **Visual-QA
   protocol** below.
9. **Modals** - confirm modal + nested depth-2 modal + a live transition panel.
10. **Input** - plain / numeric-filtered / password-masked / Cyrillic text fields
    (`nt_ui_input_text`); see the **Input controls** table below.
11. **Events** - a hold-to-confirm button (`nt_ui_events` gesture cfg) whose
    `hold_progress` drives a fill bar and confirms on `long_pressed`, plus a
    double-click target with a readout; see the **Interaction-events controls** below.
12. **Radial** - SDF radial feedback (`nt_ui_radial` + `nt_ui_radial_image`): a
    looping **cooldown** wedge, a **hold-to-confirm** wedge driven by the events
    `hold_progress`, ring + oval shape variants, the **four reveal modes**
    (desaturate / dim / hide / tint) on a textured radial-image, and a **dense
    batched grid** that proves N radials sharing one material stay one draw call;
    see the **Radial controls** + **Radial visual-QA protocol** below.
13. **Rich Text** - styled, wrapped, inline-illustrated text under one measured
    block (`nt_ui_rich_text` + `nt_ui_rich_text_markup`), authored **two ways**:
    the code-first push/pop builder AND the runtime `<markup>` parser. Demos
    **real** bold / italic / bold-italic faces (DejaVu R/B/I/BI baked into the
    pack), per-run **scale** (a big title + a smaller reward word), **all five**
    stock effects in a gallery (`wave` `shake` `rainbow` `pulse` `fade_in`),
    inline icons (lossless per-image tint), a **typewriter** (`fade_in` stagger
    off the game clock), and an **interactive link** that brightens + grows on
    hover and flips to a green "Accepted" latch on click; see the **Rich Text
    controls** + **Rich Text visual-QA protocol** below.
14. **Dropdown** - the **immediate** combo (`nt_ui_combo_begin`/`selectable`/`end`):
    a short list (icon gutter), a long scrolling list (more than `max_visible_rows`)
    that flips up near the window bottom, and a custom swatch-trigger combo
    (`nt_ui_combo_preview_begin`/`end`).
15. **Tooltip** - timed hover-reveal tooltips on popup-core (no catcher, so they
    never block clicks on the targets underneath).
16. **Menu** - the **immediate** context menu (`nt_ui_menu_begin`/`item`/`item_ex`/
    `submenu_begin`/`separator`/`item_begin`/`end`) on a right-click / long-press: a
    rich row (icon + `Ctrl+N` shortcut), a checkmark-toggle row, a disabled item, a
    nested **submenu**, and a custom `activatable=false` row whose inner button owns
    the click. Mouse-aim hover-intent, per-level edge-flip, nested dismiss, keyboard nav.
17. **Tabs** - the reusable `nt_ui_tabbar` begin/end **core** dogfooded: icon+text
    tabs with a distinct selected-tab icon + a BOTTOM accent (contrast the LEFT nav
    list, which uses the one-call `labels[]` wrapper with a LEFT accent).
18. **Stress** - N labels @14pt + the frame `gpu_ms` / draw-call readout.

## Controls

| Input | Action |
|-------|--------|
| Left tab list | select the active tab (state per tab is retained) |
| **T** key / header **Theme** button | dark <-> light hot-swap |
| **D** key | toggle the inspector overlay (`*-debug` presets; release builds log a warning and ignore it) |
| **Esc** (native) | unfocus the focused field; else quit |
| **Esc** (modal up) | close the TOP modal only |
| backdrop click (modal up) | close-on-backdrop (the backdrop blocks click-through) |

## New-widget controls (Toggles / Sliders / Virtual List tabs)

The tristate checkbox, vertical slider, and virtual list are **Model D**: the game owns
the value(s); the engine stores only the eased visual + scroll physics. All three are wired
into the **existing** vitrine tabs (no new example dir).

| Input | Action |
|-------|--------|
| **Toggles** — toggle some (not all) child checkboxes | the "Select all" parent shows the indeterminate **MIXED** dash (the game aggregates the children each frame; MIXED is display-only) |
| **Toggles** — toggle ALL / NONE of the children | the parent resolves to the **checkmark** (all on) / **empty** box (all off) |
| **Toggles** — click the "Select all" parent | `nt_ui_checkbox_tri` resolves any non-ON to ON then toggles ON↔OFF (a click **never** produces MIXED); the game then writes every child to match |
| **Sliders** — drag the vertical "Mixer" thumb up/down | the value rises dragging **up**; the fill anchors at the **bottom** (`NT_UI_SLIDER_VERTICAL` + `NT_UI_FILL_BOTTOM_UP`); the thumb overhang is clickable (cross-axis hit-pad) |
| **Virtual List** — wheel / drag-fling the vertical column | the 10k-row list scrolls; only the visible window (+ overscan) is rendered; the header readout shows `rows X..Y visible (N of 10000 rendered)` |
| **Virtual List** — wheel / drag-fling the horizontal strip | the same windowing on the X axis (`NT_UI_AXIS_X`); the scrollbar thumb size tracks the giant list |

## Visual-QA protocol (Toggles / Sliders / Virtual List)

The GL surface is **not reliably headless-capturable** here, so these widgets are verified
by the **user's eyes** — there is no automated screenshot regression. If any showcase atlas
asset changed, **force-delete the stale `.ntpack`** before this run (the pack depends only on the
builder exe, not the asset source) so QA sees fresh art. Build + run the native showcase (see
**Build & run** below), then confirm:

1. **MIXED dash** (Toggles tab) — with some children on and some off, the parent renders a
   **centered**, full-opacity dash that is clearly **DISTINCT** from the checkmark (different
   shape AND tint); all-on shows the check, all-off shows the empty box; clicking the parent
   visibly sets every child.
2. **Vertical slider** (Sliders tab) — the thumb travels the **full** track top↔bottom, the fill
   is **bottom-anchored** (BOTTOM_UP), dragging **up raises** the value, and the thumb overhang
   is clickable (the cross-axis hit-pad catches the round thumb past the narrow track).
3. **10k-row scroll feel** (Virtual List tab) — fling **both** the vertical column and the
   horizontal strip: windowing is **smooth** with NO pop-in / blank rows / overlapping rows on a
   fast fling, momentum / rubber-band is intact, and the scrollbar thumb **size + position track
   a giant list correctly** in both orientations; the header readout shows cost ~ the visible
   count, **not** 10k.
4. **Dark/Light parity** — press **T**; both palettes restyle all three new demos correctly.
5. *(Optional)* load the wasm-debug build in a browser and confirm WebGL2 parity.

> **Segmented control:** `nt_ui_tabbar` (horizontal dir + `NT_UI_TABBAR_ACCENT_NONE` + filled
> `selected` state + one-call `nt_ui_tabbar(labels, icons, count, int *active)`) doubles as the
> segmented control — the **Tabs** tab is the reference.

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

## Radial controls (Radial tab)

The radial widgets are **Model D**: the game owns the `fill` (a looping cooldown timer or the
events `hold_progress`); the engine draws an SDF arc/sector/ring/oval per pixel (crisp AA, no
vertex-pie facets) and bakes the angles into a per-vertex custom attribute so many radials batch.

| Element | Behavior |
|---------|----------|
| **Cooldown** disc + ring | a looping timer ramps `fill` 0→1 over ~3 s; `nt_ui_radial_fill` sweeps a full turn from the top |
| **Oval** sector | a static 270° sector on a non-square (140×80) bbox — the `aspect` (w/h) keeps 0° at +X with no distortion |
| **Hold** disc | press and HOLD the button; the events `hold_progress` fills the ring and confirms at the long-press threshold |
| **Reveal** row (desaturate / dim / hide / tint) | `nt_ui_radial_image` on a full-bleed (UV [0,1]) textured swatch; the **swept** sector is full color, the **un-swept** sector gets the per-mode composite |
| **Dense grid** (12×8) | every cell sweeps to a different phase but shares ONE `s_radial_material` — the header `draw calls` count stays flat as the grid count grows (batched) |

## Radial visual-QA protocol

The GL surface is **not reliably headless-capturable** here, so the radial widgets are verified
by the **user's eyes** at the BLOCKING visual-QA gate — there is no automated screenshot
regression. Build + run the native showcase, open the **Radial** tab, and confirm:

1. **Arc/sector crispness + AA** — at the small grid cells AND the large discs the arc edge is
   smooth, NOT a Defold-style vertex-pie of flat facets; the AA width reads consistent along the radius.
2. **Oval shape** — the 140×80 oval sector has the correct aspect, no distortion at the angular edges.
3. **Two-angle animation + seam** — drive the cooldown + hold radials; the 0°/360° boundary crosses
   each quadrant with NO hairline seam or flicker as `fill` sweeps.
4. **Four reveal modes** — desaturate / dim / hide / tint each apply ONLY to the un-swept sector;
   the swept sector stays full color; no premultiply halos at the swept boundary.
5. **Cooldown + hold feel** — the cooldown wedge sweeps smoothly and loops; the hold radial fills
   with `hold_progress` and confirms (the confirm counter ticks) at the long-press threshold.
6. **Batched scale** — the header `draw calls` readout does NOT scale with the 96-cell grid (it
   stays at roughly the same count as without the grid); FPS stays stable.
7. **Dark/Light parity** — press **T**; both palettes render the radial tab correctly.
8. *(Optional)* load the wasm-debug build in a browser and confirm the radials render (WebGL2 parity).

## Rich Text controls (Rich Text tab)

Rich text is **Model D**: the game owns the content (the builder calls or the markup
string), the effect **clock** (`time`, accumulated from frame `dt` — there is no engine
global clock), and the link reaction. The widget lays out + draws one measured block and
reports the link hover/click back. The tab renders the **same content twice** — once via
the code-first builder, once via the runtime markup parser — to prove byte-identical output.

| Element | Behavior |
|---------|----------|
| **Code-first builder** block | `nt_ui_rich_begin` / `push_scale` / `push_color` / `push_bold` / `push_italic` / `push_effect` / `text_n` / `image` / `link` / `end` — explicit real values (font / abgr color / atlas ref) |
| **Runtime markup** block | `nt_ui_rich_text_markup` parses the SAME content via `<scale=N>` / `<b>` / `<i>` / `<color=name>` / `<fx=name>` / `<img=name/>` / `<link=quest>…</link>`; a tagset registers the named colors, all five stock effects, the `rich` font family, and the icons atlas alias |
| **Real faces** | DejaVu **R / B / I / BI** are baked and bound to the four variant slots, so `<b>` = real bold, `<i>` = real oblique, `<b><i>` = real bold-italic — no synth-shear, no faux-bold |
| **Scale** | a big `<scale=1.6>` **DRAKE** title word + a smaller `<scale=0.85>` "100 gold" show visible per-run size variation |
| **Effects gallery** | all five stock effects appear, one per labelled word — `wave` `shake` `rainbow` `pulse` `fade_in` — each animating off the same game `time` clock |
| **Inline icons** | the gold + heart icons sit baseline-aligned (`valign=middle`) beside the text; the run's `<color>` rides the **lossless** per-image `a_tint` |
| **Typewriter** | the third line uses `fade_in` to stagger each glyph's reveal off the clock; it loops every ~4 s so the reveal replays |
| **Link** | hovering `[Accept quest]` **brightens (cyan) and scales up**; clicking flips it to a green **`[OK Accepted]`** for ~1.2 s, ticks the click counter, and latches the id (Model-D reaction). Both fronts react identically (the markup string is rebuilt each frame with the same link look) |

## Rich Text visual-QA protocol

The GL surface is **not reliably headless-capturable** here, so rich text is verified by the
**user's eyes** at the BLOCKING visual-QA gate — there is no automated screenshot regression.
Build + run the native showcase, open the **Rich Text** tab, and confirm:

1. **Real bold / italic / bold-italic** — on the "faces:" line, `regular` / **`bold`** / *`italic`* /
   ***`bold-italic`*** are four visibly DIFFERENT faces (real DejaVu weights + slant), not a
   synth-shear or a faux-bold smear. The crimson **DRAKE** title is the bold face, scaled up.
2. **Scale variation** — the **DRAKE** title word is clearly LARGER (`<scale=1.6>`) than the body,
   and the "100 gold" run is slightly SMALLER (`<scale=0.85>`); the layout still wraps cleanly.
3. **All five effects animate** — in the "Effects:" gallery each labelled word moves on its own:
   `wave` (vertical sine), `shake` (jitter), `rainbow` (hue cycle), `pulse` (breathing scale),
   `fade_in` (staggered reveal). The layout does NOT re-flow (effects are visual-only).
4. **Inline-icon baseline alignment** — the gold + heart icons sit on the text baseline
   (`valign=middle`), neither floating above the cap height nor clipping below the descenders;
   the gold icon carries its lossless tint.
5. **Typewriter reveal** — the third block reveals glyph-by-glyph and replays on the ~4 s loop;
   no glyph pops in at full alpha out of order.
6. **Link hover + click (the headline interaction)** — hovering `[Accept quest]` makes it
   **brighten to cyan AND grow**; clicking flips it to a green **`[OK Accepted]`** for ~1.2 s,
   ticks the `clicks` readout, and latches the `last clicked` id (Model D). The reaction is
   unmistakable, not a no-op.
7. **Both fronts identical** — the builder block (1) and the markup block (2) render the SAME
   styled paragraph INCLUDING the live link reaction (the markup is rebuilt each frame with the
   same link look); any divergence is a parser bug.
8. **Wrap at the container width** — narrow the window; the paragraph re-flows at the ~560 px
   block width with NO atom escaping the box, and lines stack on a correct max baseline.
9. **Dark/Light parity** — press **T**; both palettes restyle the rich-text tab (the body color
   tracks the palette).
10. *(Optional)* load the wasm-debug build in a browser and confirm the rich text renders (WebGL2 parity).

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

**WebGL2 note:** the app pushes `gpu_ms < 0` into `nt_metrics` (read back via
`nt_metrics_last().gpu_ms`) when the `EXT_disjoint_timer_query` extension is
absent (common on WebGL2). The tab guards `< 0` and shows `gpu: n/a` instead of
garbage.

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
