# Rich text (`nt_ui_rich_text`)

Styled, wrapped, inline-illustrated text under one measured Clay block, with two
authoring fronts — a code-first push/pop builder and a runtime markup parser —
producing the same run-list. Covers the wrap/baseline solver, text decoration
axes, inline images and custom objects, visual-only effects, links, per-atom
z-layers, and the documented divergences from the original #184 proposal.

Related: [Scope](../core/scope.md), [Radial Widgets](radial-widgets.md), [Resource System](../assets/resource.md)

Styled, wrapped, inline-illustrated text under **one** measured Clay block. Two
authoring fronts produce the same run-list: a **code-first** push/pop builder and
a **runtime markup parser**. The design rationale lives here; the headers
(`nt_ui_rich_text.h`, `nt_ui_rich_tagset.h`, `nt_ui_rich_fx.h`) carry the
caller-facing contract.

## The runtime parser is a CONTENT parser, not an asset parser

The working principles forbid **asset-format parsers at runtime** — the builder
packs source formats (PNG, TTF, glTF) into binaries offline so the runtime loads
prebuilt data with no parsing. `nt_ui_rich_text_markup` does **not** violate this.

The markup string (`"<b>HP</b> <color=gold><img=heart/></color>"`) is a
**content** string, not an asset. It is the exact peer of a `printf` format
string or a localized UI string the game already passes at runtime — short, game-
authored display text, parsed into the same run-list the code-first builder
emits. There is no file format, no versioned binary, no offline-bakeable source
being re-parsed; the parser only routes display content the game produces this
frame. It DRIVES the shared builder (`begin` / `push_*` / `text_n` / `image` /
`pop` / `end`), so a markup string and the equivalent builder calls produce a
byte-identical run-list — one composition path, two front ends.

**Intrinsic vs tagset-resolved tag values.** The **intrinsic** tags resolve
directly off the markup, no tagset needed: `<b>`, `<i>`, `<scale=N>`,
`<link=id>` (the id is hashed in place), `<color=#RRGGBB>` (hex literal read
directly), and the default-atlas `<img=region/>` (region resolved by name
against the base style's atlas). The `<img>` tag takes an optional
space-separated attribute tail after the region spec —
`<img=region scale=S oy=Y valign=V/>` — where `scale` is the size multiplier
(float, default `1`), `oy` is the vertical pixel offset (float, default `0`),
and `valign` is one of `baseline|middle|top|bottom` (default `middle`); these
mirror the builder `nt_ui_rich_image(ref, valign, oy, scale)` args, so a tagged
markup `<img>` and the builder call produce a byte-identical run. A malformed
attr (bad float, unknown key, unknown valign) is **logged once (`nt_log_warn_unique`) and skipped** —
markup is untrusted localization DATA, so a bad value degrades gracefully (the rest renders) and never
asserts; the code-first builder, being trusted game code, still asserts. Only the **NAMED** resolves go through the
**tagset**: `<color=name>`, `<font=name>`, `<fx=name>` (optionally tuned:
`<fx=name amp=8 speed=3>` — `key=value` float pairs after the name, stock effects
only), an `<img=alias:region/>` atlas alias, and the self-closing
`<obj=name/>` (a game-drawn WidgetSpan resolved like `<img=alias:region/>`) —
passing one of these with a `NULL` tagset (or an unresolved name) is **logged once and skipped** — the tag
drops and the surrounding text still renders. A tagset is therefore required only when the markup uses those named
forms; pure-intrinsic markup parses with a `NULL` tagset.

## Design: flat run-list → solver → one FIXED block

- **Run-list (SoA, frame scratch).** The builder appends runs into a flat
  per-call run-list in frame scratch (no heap; `#define` caps assert on overflow).
  A new run starts only when the **composed style** (color / scale / font variant
  / effect / image-material) changes — adjacent same-style text coalesces.
- **Solver.** A word-wrap + baseline solver resolves every atom (glyph, image,
  object) to a box at a container width, stacking lines on a per-line max
  baseline. Horizontal alignment (L/C/R) offsets each line.
- **One Clay FIXED block.** The solved total size feeds a single
  `CLAY_SIZING_FIXED` Clay element; text emits during that element's
  custom-walk via `NT_UI_CUSTOM_TYPE_RICH_TEXT`. Keeping text under one measured
  block is what lets the whole paragraph wrap and align as a unit.
- **Base font size is a STYLE FIELD.** `nt_ui_rich_style_t.font_size` (px, > 0)
  mirrors `nt_ui_label_style_t.font_size` — the base size lives in the style, not a
  per-call param. `nt_ui_rich_style_defaults()` seeds it from
  `NT_UI_RICH_DEFAULT_FONT_SIZE` (16). Per-run `<scale>` *multiplies* it (the relative
  model is unchanged): `size = style.font_size × composed scale`. Per-run **absolute**
  `<size=N>` stays deferred.
- **Synthetic italic (faux-italic).** An italic-requested run whose resolved family has no italic
  face (the `BI→B→R` variant fallback drops the italic member) raises
  `NT_UI_RICH_RUN_SYNTH_ITALIC`; the emit pass leans it via
  `nt_text_renderer_set_oblique(NT_UI_RICH_SYNTH_ITALIC_SHEAR)` (0.2 — text-local `x += k·y` about
  the baseline) instead of a real italic glyph. The shear is **renderer-level state** (sibling of
  `glyph_depth_bias`; survives `restore_gpu`, cleared on cold init) folded into the model on the CPU
  per draw — no flush, mixes within one batch — and the pass resets it to 0 so the lean never leaks
  onto later text. Bold has no free analog (weight is contour *coverage*, not an affine transform);
  synthetic bold instead emits an emboldened glyph variant — see the decoration contract in [Text decoration](#text-decoration-weight--outline--shadow--underline--strike).

## Text decoration (weight / outline / shadow / underline / strike)

Five decoration axes are **renderer-level sticky state** on `nt_text_renderer`, set via
`set_weight` / `set_outline` / `set_shadow` / `set_underline` / `set_strikethrough`. Both authoring
fronts feed the SAME setters at emit: `nt_ui_label` from `nt_ui_label_style_t` fields, and rich text
from composed run state (variant bits + `push_outline/shadow/underline/strikethrough`). No new
subsystem — decoration reuses the text pipeline and the `slug_text` shader.

- **Units.** Text `font_size` is **px**. Everything decorative is **em** (a fraction of the text
  height, so it scales with size): `weight`, `outline` width, and `shadow` (dx,dy) offset are all em,
  converted `px = value × font_size` at emit (the shadow multiplies by `size`; weight/outline multiply
  by `units_per_em` into the glyph-cache key). Underline/strike position + thickness come from the v5
  font-header metrics (font units) scaled by `size`. One consistent rule: *size in px, decoration in em*.
- **Glyph variants.** Synthetic weight and outline offset the glyph contour (Minkowski-style point-ring
  offset + self-intersection resolution) and cache the result under a `(codepoint, quantized weight)`
  key — a separate entry from the natural glyph, sharing the same curve/band textures so an emboldened
  or outlined run still batches into ONE draw. This geometry runs only on the glyph-cache **miss path**
  (not per frame); the outline pass grows the fill weight by `outline_w`, the shadow pass reuses the
  outermost visible variant (no new key). This CPU offset/self-intersection resolution is a **deliberate,
  bounded exception** to "builder does the heavy work; runtime stays simple": it is amortized (once per
  `(codepoint, weight)` variant, then cached), uses only static scratch (no heap), degrades gracefully at
  fixed caps, and is guarded by a worst-case decode-miss budget test. Prebaking a fixed set of weight
  variants in the builder remains an option if a workload ever thrashes the variant cache.
- **Painter order & batching.** Per run: **shadow → outline → fill → underline/strike**. Underline and
  strike are one continuous solid quad per line, emitted as a **sentinel** vertex (`band_count == 0`,
  which the shader reads as full coverage) in the same vertex buffer / material — no separate draw
  call, no flush, so decoration never breaks the batch. This per-run order + continuous-per-line underline
  is the **label path**: `nt_ui_label` draws the whole label in one `draw_n`, so the passes group over it
  and the underline is one quad. **Rich text is different**: it atomizes each run into **word atoms** (and
  an `<fx>` run further into **per-glyph** draws), and each atom is its own `draw_n`, so rich decoration is
  **per-atom** — outline/shadow ride each atom (necessary for `<fx>`, where a per-run pass would detach
  from the moving glyphs) and underline/strike are per-atom (per word, or per glyph under `<fx>`), NOT one
  continuous per-line quad (so a multi-word underline is segmented at word gaps, and cross-atom painter
  order is only approximate where atoms overlap). Known limitation; a continuous per-line rich underline
  would need a per-line decoration emit (renderer span API), tracked separately.
- **Reset & leak-safety.** Decoration state is sticky (survives `restore_gpu`, cleared on cold
  init/shutdown). Because it persists, the UI calls `nt_text_renderer_reset_decoration()` after each
  decorated run so nothing leaks onto the next. Every float setter is hard-guarded with a real
  `if (!isfinite)` (NOT an assert — `NT_ASSERT` is a no-op in shipping, and a NaN would poison the
  offset/quantize math).
- **Parent opacity** folds into the fill AND the outline/shadow alpha (the walker pre-multiplies only
  the fill's `textColor.a`, so `nt_ui_label_deco_apply` / the rich emit multiply the decoration colors
  by the accumulated opacity too — a faded panel fades its outline/shadow consistently).
- **Fallback (explicit).** Bold with no bold family member → synthetic weight; italic with no italic
  member → faux-italic oblique ([Design — synthetic italic](#design-flat-run-list--solver--one-fixed-block)); underline/strike are decoration toggles needing no family
  member. A label has a single `font_id`, so its bold always degenerates to the synthetic weight.

## Inline images ride the standard u8 sprite path

An `<img>` atom is **NOT** a Clay child. It emits **immediately** in the rich
block's CUSTOM self-emit (`rich_emit_images`) via
`nt_sprite_renderer_emit_region`, positioned at the solver's solved `(x, y)`.
The composed tint (the run's `<color>` × any per-atom effect tint) is packed to
the standard **u8** sprite tint, the block's **image material** (the plain u8
sprite path, `attr_map_count == 0`) textures the region, and the self-emit folds
the parent opacity into the tint alpha exactly like rich TEXT — there is **no**
bespoke material, float4 `a_tint`, or custom-attr block. The single composed
tint is invisible at u8 on an 8-bit display, so the earlier lossless-float4 path
gave no benefit and was dropped. `set_material` is bound **once per band** (the
`bound` guard), so **all** of a band's inline images **coalesce into one sprite
batch** — no per-image flush. Because the sprite renderer emits while the active
**scroll scissor is GL-live** during the walk, the images are clipped to the
panel/scroll automatically — by the live scissor, **not** a Clay
`.floating.clipTo`. Caveat: an `fx.scale > 1` image loses its per-image
self-clip-to-bbox and **over-draws** past its solved box (same as OBJECT atoms;
consistent and accepted). Images resolve **by atlas + region name** — the atlas
IS the registry (see [Spec ↔ #184-proposal divergences](#spec--184-proposal-divergences-per-agentsmd)). The per-band z-ordering / drain model that
sequences these emits is **[Per-atom z-layers](#per-atom-z-layers-explicit-draw-order)**.

## Effects (visual-only) and links

- **Per-atom effects** are a pure deterministic curve `fn(atom_idx, kind,
  base_xy, base_wh, base_color, time, hovered, user_data) → {offset, color, scale,
  visible}` evaluated at emit and folded into the existing position / tint / scale
  (no 5th custom attr). They are **visual-only**: the solver layout never re-flows.
  The animation clock is **passed in by the game** (`time`) — there is no engine
  global frame clock (RESEARCH Pitfall 4). The `user_data` is the pointer the game
  registered with the fn (see below): the same fn can be **parameterized per
  registration** (e.g. amplitude/offset read from `user_data`); stock catalogue fns
  receive `NULL` and ignore it.
- **Tunable stock effects (revises the earlier compile-time-constants stance).**
  The stock catalogue constants are now **defaults**, not the final word: a stock
  effect is tunable at runtime via `nt_ui_rich_fx_params_t { float amp; float
  speed; }` passed as the stock fn's `user_data`. Two fronts deliver it: the builder
  `nt_ui_rich_push_effect_ex(ctx, stock_id, params)` (params are **copied by value**
  into per-block storage — they are read at emit, so they must outlive the transient
  caller struct) and the markup `<fx=name amp=8 speed=3>` (the tag value is the
  effect name followed by space-separated `key=value` pairs; keys `amp`/`speed`,
  float values; a malformed pair logs once and is skipped — markup is untrusted
localization content, never an assert). **Convention: a field
  `<= 0` means "use the effect's compile-time default"**, so a partly-specified
  struct tunes only the field it sets. `params == NULL` (the plain
  `nt_ui_rich_push_effect` / bare `<fx=name>` path) is **byte-identical** to the
  original compile-time-default behaviour. Per-effect mapping: wave `amp` = vertical
  px / `speed` = rad/s; shake `amp` = jitter px / `speed` = steps/s; rainbow `speed`
  = hue turns/s (`amp` unused); pulse `amp` = scale delta / `speed` = rad/s; fade_in
  `speed` = per-atom reveal rate (`amp` unused). The params struct is **in-memory
  only** (never serialized), and tuned stock effects route through the SAME per-block
  custom-fx table as game-supplied fns (a tuned effect carries an `effect_id >=
  NT_UI_RICH_FX_CUSTOM_BASE`). Markup `k=v` params apply to **stock effects only** —
  a `<fx=name>` resolving to a custom fn carries that fn's own `user_data`, so
  passing `k=v` on a custom name is **logged once (`nt_log_warn_unique`) and the params ignored**.
  This **revises** the original
  proposal's stance that per-effect tuning was compile-time constants and NOT tag
  params; the catalogue constants are now the defaults.
- **Stock + custom effect catalog (extensible).** A stock catalogue
  (wave / shake / rainbow / pulse / fade_in / bounce / glow / sway) ships as a
  starting set, registered piecemeal by name. The visual-only additions:
  **bounce** (`offset_y = -amp*|sin(time*speed + idx*PHASE)|`, an always-upward
  sharp-bottom hop — distinct from wave's smooth swing), **glow**
  (`color.rgb = base + (1-base)*amp*(0.5+0.5*sin(time*speed))`, a brightness pulse
  toward white, alpha kept), **sway** (`offset_x = amp*sin(time*speed + idx*PHASE)`,
  the horizontal counterpart to wave). All three honour the `amp`/`speed` params
  (`<=0` keeps the default). A game also supplies its **own** `nt_ui_rich_fx_fn` and uses
  it from **both** authoring fronts: the builder's
  `nt_ui_rich_push_effect_fn(ctx, fn, user_data)` and, for markup `<fx=name>`,
  `nt_ui_rich_tagset_register_effect_fn(ts, name, fn, user_data)`. A custom name
  resolves **before** the stock catalogue. Because effects evaluate at emit — when
  only the solved run-list (parked in the custom command) is available and the
  tagset is **not** guaranteed present — the resolved effect is captured at
  build/solve into the solved state: the composed style carries a `uint8_t`
  `effect_id`, and an id `>= NT_UI_RICH_FX_CUSTOM_BASE` indexes a per-block
  fixed-cap `(fn, user_data)` table (no heap; the table lives outside the style); a smaller
  id is a stock catalogue index resolved via `nt_ui_rich_fx_stock`. An unknown
  stock id falls back to identity. The 72 B `nt_ui_rich_style_t` and the
  per-block custom table are in-memory-only, never serialized.
- **Links** (`<link=id>`): the widget hit-tests its **own** solver rects against
  the pointer (offset by the block's prev-frame bbox origin) and reports
  `{hovered_link, clicked_link}` — there is **no extra Clay element per link**.
  Link hover gates effects (an effect sees `hovered == true` only for the hovered
  link's atoms). The Model-D game reacts to the reported click. The builder call
  is **set/clear, NOT push/pop**: `nt_ui_rich_link(ctx, id)` starts a pending
  link, `nt_ui_rich_link(ctx, 0)` ends it (`</link>` does the same); links never
  nest (HTML no-nested-anchor rule). Style push/pop is separate — popping past
  the base style asserts in debug and hard-no-ops in shipping.
- **Custom objects** (`<obj>`): a Flutter-style WidgetSpan — the solver reserves
  a box via `measure_fn` (text wraps around it); the widget calls the game's
  `draw_fn(user_data, x, y, w, h, color, world_mat4)` at the solved box. The engine
  never draws the object (renderer-agnostic). `x,y,w,h` are LAYOUT (logical,
  Clay Y-down) px; `world_mat4` is the frame's column-major LAYOUT→world matrix — the
  **same** matrix every other engine emit uses, with the screen Y-flip baked in for the
  default 2D ctx — so the game multiplies its positions by it (or composes it on the
  LEFT of its model) and the object lands correctly under the UI transform incl. the
  Y-flip. The complement: the frame UBO ortho itself is **Y-flip-free**
  (LAYOUT→clip, logical px), so a `draw_fn` composing its own projection instead of
  using `world_mat4` must apply the flip itself. Box-exact drawing uses
  `nt_sprite_renderer_emit_geometry` (explicit corners); `emit_region` draws at
  native source size. A 3D object renders inside the box by remapping its
  clip-space output into the box's NDC sub-rect
  (`clip'.xy = half·clip.xy + center·clip.w`, aspect from box pixels) — it must
  **not** touch `glViewport`/scissor: the walk's live scroll scissor stays intact.
  `color` is the **absolute resolved RGBA**
  the engine resolved for the atom — the run's `<color>` with parent opacity folded
  into alpha plus any per-atom effect tint, the SAME color the TEXT and IMAGE paths
  render with — so a custom object honours opacity / `<color>` / effects consistently
  (AGENTS.md "if sprites have it, UI images need it too").

## Per-atom z-layers (explicit draw order)

UI is **painter-order** (depth test off). Cross-renderer z is therefore **flush
order**, and every walk barrier flushes **sprite then text** — so within a single
batch text always lands *on top of* images, and the two are **not reorderable** by
emit order. To give the game explicit control of overlap z, each atom carries a
**layer** (z-order band):

- **Default by kind** (no `<layer>`): `TEXT = 0`, `IMAGE = 1`, `OBJECT = 2`
  (ascending = further back → further front). So by default text draws *behind*
  images, which draw *behind* objects. The `nt_ui_rich_style_t.layer` field
  (offset 42) holds the sentinel **`255` (AUTO)** until an explicit layer is pushed; the per-kind
  default is resolved at atom build.
- **`<layer=N>` / `nt_ui_rich_push_layer(N)`** (N = 0..254): every enclosed atom of
  **any** kind takes layer N, overriding the per-kind default. `</layer>` /
  `nt_ui_rich_pop` restores. The parser drives the same builder, so `<layer=N>`
  produces a **byte-identical** run-list to `push_layer(N)`. Malformed / out-of-range
  (`255`, `>254`, empty, non-numeric) is a builder-validate assert in DEBUG and a
  **hard skip to AUTO** that survives `NT_ASSERT` OFF (untrusted-markup hard-guard
  rule).
- **Layer-ordered self-emit.** The self-emit gathers the **distinct** layers present
  (insertion-sorted ascending, capped at `NT_UI_RICH_MAX_LAYERS = 16` with a hard
  drop guard — the over-cap distinct layers are dropped **by encounter order**, not by
  value, and the drop asserts in DEBUG), then for each band ascending emits `{font-grouped text → coalesced
  images → objects}` and **DRAINs** (sprite flush + text flush) before the next band,
  so band N fully lands before band N+1. The drain runs after **every** band incl. the
  last, making the block a self-contained z island regardless of the walker's global
  flush order.
- **Within-band z (text < image < object).** Inside ONE band the self-emit drains
  **text first** (`nt_text_renderer_flush`) so it lands *behind*, then emits the band's
  images and objects and drains the sprite renderer — within-band draw order is
  therefore **text behind images behind objects**, matching the per-kind default. To
  control text-vs-image z explicitly, put them on **separate** layers (one band is one
  cross-renderer flush boundary, not per-emit). **Caveat:** a shape-renderer object
  (e.g. a 3D cube) drawn by an `<obj>` `draw_fn` **self-flushes** its own renderer, so
  an object sharing a non-top band is best-effort z (the rich block sequences sprite +
  text drains, not third-party renderer flushes).
- **Cost.** A layer is an explicit **flush boundary** — it buys z-control, **not** a
  draw-call saving (each band adds one sprite+text flush). The font-group and
  image-coalesce DC wins are **within** a band and unchanged: the font gather is
  per-band (a shared face rebinds once per band), image coalescing is per-band. Use
  distinct layers only where explicit overlap z is needed; non-overlapping content on
  one default layer pays nothing extra. The per-band `set_material` calls stay direct
  (the walker bind cache is untouched).

## Spec ↔ #184-proposal divergences (per AGENTS.md)

The shipped behaviour is the sections above; this table only records where it
deliberately differs from the original #184 proposal, so a reader of that
proposal is not misled.

| # | Proposal said | Shipped instead |
|---|---|---|
| D-67-13 | per-image-tag `register_image_tag` call | `<img=region/>` resolves by atlas + region NAME — the atlas IS the registry; the tagset carries only an atlas alias, font family, named color, effect id, object tag |
| D-67-17 | effects per-glyph, TEXT only | effects per-ATOM via `effect_id`, applying to TEXT, IMAGE and OBJECT (per-glyph explode kept for TEXT as a quality path) |
| D-67-21 | per-run alignment | one per-block `nt_rich_align_t` (L/C/R) offsetting each solved line |
| D-67-22 | free pixel offset for image vertical placement | a `valign` enum (`baseline/middle/top/bottom`) on the image atom |
| D-67-23 | a new block-origin getter | the FIXED block reuses `nt_ui_get_bbox` for its prev-frame origin, so the block carries `decl.id` |
| D-67-26 | game effect callback looked up in an extensible tagset catalog | custom `nt_ui_rich_fx_fn` interned into a per-block table at build/solve and addressed by `effect_id >= NT_UI_RICH_FX_CUSTOM_BASE` — the tagset is game-owned and may be absent during the walk |
| D-67-27 | per-effect tuning is compile-time constants, never tag params | catalogue constants are defaults; stock effects take `nt_ui_rich_fx_params_t` via `push_effect_ex` or `<fx=name amp=.. speed=..>` |
| D-67-28 | `draw_fn(user_data, x, y, w, h)` | `draw_fn(..., color, world_mat4)` so a game-drawn object lands under the same transform as TEXT/IMAGE |
| D-67-29 | per-atom z-layers as a draw-call saving | layers are an explicit flush boundary for overlap order (one flush per band); DC wins stay within a band |
| D-67-30 | `font_size` as a call parameter | `font_size` is a `nt_ui_rich_style_t` field, mirroring `nt_ui_label_style_t` |
