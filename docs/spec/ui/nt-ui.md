# nt_ui Module

The minimal immediate-mode UI module built on vendored Clay: what it deliberately
is not, the Clay dependency contract, the render-time transform pipeline, the
interaction and arbitration model, the anim cache and state pool, stateful
widgets, scroll physics, virtualization, text input, popups and menus, and the
two known rendering limitations.

Related: [Scope](../core/scope.md), [Core Principles](../core/principles.md), [Rich Text](rich-text.md), [Radial Widgets](radial-widgets.md), [Render Items, Sorting, Batching](../render/items-sorting-batching.md)

## Not a full UI framework

`nt_ui` is a minimal immediate-mode layout + render bridge module. It is **not**
a full UI framework — no widget authoring tools, no styling pipeline beyond
inline calls, no asset hot-reload of UI definitions, no scene-graph
integration. Game owns the loop and render order; `nt_ui` provides
building blocks per the engine's "set of modules" principle.

## Clay as a public dependency

Clay v0.14 is vendored as a **public** dependency of `nt_ui`: game
code declares layout and widgets via `CLAY_*` macros directly,
while `nt_ui` owns lifecycle (contexts, the walker that turns Clay's
render commands into sprite/text renderer calls). Styling is
game-owned: `nt_ui_label_style_t` is a plain data struct passed by
const-pointer; engine ships no theme manager or style registry. This
is a deliberate compromise — wrapping Clay's full surface in an
engine-owned adapter API would be ~100+ shim functions for no
benefit beyond hiding the dependency. The cost we accept is that
Clay's pinned API (`CLAY_PINNED_MAJOR/MINOR` enforced via
`_Static_assert` in `nt_ui.c`) becomes part of the engine's
publicly-promised surface; bumping Clay can require coordinated
game-side changes.

## Floating zIndex is relative

Upstream Clay sorts every floating element globally by its own
`zIndex`. The vendored copy diverges (NT patch 4 in `deps/clay/clay.h`):
a floating declared inside another floating gets `parent_z + own_z`, so
`zIndex` reads as a stacking context, like CSS. Without it a widget's
own floating part (the slider thumb, the input caret and text) sank to
the global band 0 and drew UNDER the modal panel it was declared in;
the scrollbar escaped that only by hand-computing the popup band from
`active_modal_depth`, which is the workaround the patch deletes. A child
root sorted ahead of its parent was also positioned from the parent's
PREVIOUS-frame bbox — a frame of lag, and an empty clip rect on the
first frame, which the walker's SCISSOR assert catches. A delta of 0
now ties with the parent and, since the root sort is stable, paints
after it with a same-frame bbox; a NEGATIVE delta still sorts ahead of
its parent and still reads the parent's previous-frame bbox (two such
cases: the tooltip drop-shadow, which guards for it, and the inspector
highlight, which attaches to a base-band element and so reads a
same-frame bbox).

Consequences: a widget composes anywhere without knowing its global
stacking position; the engine's overlay bands (`modal_zband_stride`) are
declared as deltas and accumulate with declaration nesting, NOT with
attachment (`attachTo = ROOT` still stacks where it was declared), so a
popup opened from inside another popup reaches `stride*depth` as long as
every enclosing floating is an engine overlay; a widget's own floating
parts (scrollbar, thumb, caret) declare delta 0 and rely on being
declared last to paint above their container's content; a game floating
that wants to escape its enclosing panel must be declared outside it;
the value Clay stores and reports for a floating is the accumulated
band, not the declared delta — but among RENDER COMMANDS only
RECTANGLE, TEXT and the clip SCISSOR carry it, since Clay leaves
`zIndex` at 0 on IMAGE, BORDER and CUSTOM; and an accumulated band that
would saturate `int16` raises a Clay error (which `nt_ui` asserts on)
rather than silently merging two bands, which puts a ceiling on what a
game floating may declare: its own `zIndex` plus `modal_zband_stride`
per overlay level nested inside it must still fit `int16`. That ceiling
has no test — the engine's own bands cannot reach it (overlays are
bounded by `modal_zband_stride * NT_UI_MODAL_MAX_DEPTH`, and the
inspector root nests nothing with a positive delta).

## Clay private symbols

Even with `NT_UI_DEBUG_TOOLS=OFF`, nt_ui touches ~10 Clay **private**
symbols (`Clay__OpenElement` / `Clay__ConfigureOpenElement` /
`Clay__CloseElement`, the `Clay__MeasureText` callback hookup, a few
`Clay__default*` size constants) — all routed through thin wrappers in
`engine/ui/nt_ui_clay_impl.c`, the exclusive `CLAY_IMPLEMENTATION`
TU. The widget composition pattern (button / panel / group `_begin`)
splits Clay's `CLAY({...}) { body }` into separate `_begin`/`_end`
calls so widget code can run between them — Clay's public macro
bundles `Open` + `Configure` + scoped body into a single statement
and can't be split across function boundaries. The wrappers are the
smallest possible escape hatch.

One of those wrappers reads state that exists only because of NT patch 4:
`nt_ui_clay_priv_enclosing_floating_z` returns the top of Clay's
`openFloatingZStack`, and `nt_ui_popup` uses it to rank overlays. Re-applying
patch 4 is therefore a precondition for overlay arbitration, not only for
painting.

With `NT_UI_DEBUG_TOOLS=ON` this expands by ~30 more Clay private
symbols for the verbatim Clay debug-view port (the inspector body
lives in the same TU). Either way, bumping Clay can require
coordinated nt_ui-side changes.

## Render-time pipeline

`nt_ui` composes a per-element column-major
`mat4` + opacity in a post-`Clay_EndLayout` build pass
(`nt_ui_internal_build_tree`). The transform is `nt_ui_transform_t`
with 9 floats (offset_xyz, rotation_xyz Euler, scale_xyz). Composition
follows standard scene-graph order: `world = accum_parent · L_child`,
where `L = T(O+C) · R(rot_xyz) · S · T(-C)` is built around the element's
layout-space bbox center via cglm. The walker reads the composed mat4
from `tree_baked[layout_idx]` (hot path, no hashmap lookup) and ships
`world_mat4[16]` to every emit_*.

Two coord-space modes are gated by `nt_ui_create_desc_t.use_raycast_input`:

- **2D ctx** (default): screen Y-flip (Clay layout Y-down → GL Y-up) is
  folded into `world_mat4` once per dispatch (negate m[1]/m[5]/m[9],
  add `vy+vh` to m[13]). Hit-test reverses through the top-left 2×2 +
  col3 of the same mat4 (closed-form inverse-affine).
- **3D ctx** (`use_raycast_input=true`): walker leaves `world_mat4`
  unflipped; the game owns the screen mapping via `nt_ui_set_view_proj`.
  Hit-test raycasts pointer → NDC (Clay Y-down to NDC Y-up) → world ray
  via `inv_view_proj` → ray-plane at widget Z=0 → `mat4_inv_trs(baked.m)`
  back to widget-local → bbox check. Optional `element_depth_bias_ndc`
  is render-only: positive values move deeper hierarchy levels slightly
  closer in projected depth to avoid z-fighting. Hit-test stays on the
  unbiased widget plane. Debug inspector render commands are excluded
  from the regular 3D walk and drawn by `nt_ui_debug_inspector_walk` as
  a separate final screen-space pass. The game binds a 2D UI projection
  first; `nt_ui_make_screen_view_proj(w, h, ...)` provides the standard
  Y-up orthographic matrix used by 2D demos. The post-walk highlight
  overlay (`nt_ui_inspector_overlay_draw`) differs: in 3D ctx it emits the
  hovered element's bbox under that element's `hit_baked` world matrix, so
  the game binds the perspective VP for it while the sidebar tree stays
  orthographic.

Both modes use the same `tree_baked[layout_idx]` + per-id mirror
`hit_baked[slot]` (Clay's hashmap is persistent across frames;
`hit_generation[slot]` rejects stale ids). Opacity is a separate
`float` accumulator on the same struct.

## Interaction model

Game ids interact via `nt_ui_query_interaction`
(pure, multiple calls per frame OK) and `nt_ui_step_interaction`
(mutating, exactly one call per id per frame). Capture is per-pointer:
a press records `active_id`; release clears. Other widgets are
`exclusive_gated` while one holds capture. **Orphan cleanup** at
`nt_ui_begin` drops `active_id` if the widget didn't call step last
frame — covers scene switches, conditional disable, and widget hide.
Result: 1-frame IM-lag is intrinsic (current frame reads previous
frame's bbox); no stuck input on widget disappearance.

## Front-most arbitration

When interactive widgets overlap a pointer, only
the front-most may react; the rest are gated off. The hot widget per pointer
is resolved once per frame — lazily, on the first `step_interaction` /
`query_interaction` / `nt_ui_pointer_hot` — from the PREVIOUS frame's
interactive registry; only widgets that called `nt_ui_step_interaction` last
frame are candidates. 3D ctx (`use_raycast_input`): nearest world ray-distance
within a game-fed occlusion cutoff (`nt_ui_set_pointer_occlusion`, reset each
begin) so UI can't be clicked through world geometry — the game owns the
raycast, the engine only takes the cutoff distance; `nt_ui_interaction_t.distance`
reports the hit distance. 2D ctx: highest effective Clay zIndex (tie →
last-registered, i.e. the later `step_interaction` call; widget code should step
in declaration order so this matches paint order). A free pointer drives a
widget only if it is the resolved hot
widget or already holds capture; `nt_ui_pointer_hot` exposes the resolved id.
Consequence: a freshly-shown widget registers on its first step and only
becomes eligible the NEXT frame; on the first frame (empty registry) nothing
reacts — reliability over instant first-frame response, no raw-hit fallback
(matches Dear ImGui). This trades immediacy for unambiguous overlap/occlusion.
Symmetrically, a widget that interacted last frame but is gone or disabled this
frame stays a hot candidate for that ONE transition frame (it can gate widgets
beneath it but itself reacts to nothing), then orphan cleanup drops it next frame.
A statically-disabled widget never registers, so never gates — unless it opts in via
`nt_ui_block_pointer(ctx, id, pad_lrtb)`, which writes an INERT hit-registry entry (the
same record `step_interaction` makes, minus any interaction). It blocks input to widgets
behind it but never captures, clicks, or reports hover — the stock disabled widgets
(button/checkbox/slider) call it so a disabled overlay or modal can't leak clicks through.

## Anim cache

`nt_ui_anim_*` provides per-id eased state for widget
visuals. Open-addressing direct-mapped table (`NT_UI_ANIM_SLOTS`,
default 512, power-of-2); 4-probe chain; full-chain collision evicts the STALEST
slot in the probe window — the one with the oldest `last_touch`
generation tick (snap-reseed, easing lost for one id). Evicting by
staleness rather than blindly the tail avoids bleeding a slot that
was already touched this frame between widgets sharing the window.
The `anim_collision_count` monotonic counter surfaces this
degradation; game polls the delta to size `NT_UI_ANIM_SLOTS`.

## State pool

`nt_ui_state(ctx, id, size, tag)` is a generic per-widget-id
retained-state pool — the durable counterpart of the anim cache. It returns a
get-or-create cell (zeroed on create; zero is a valid initial state). The 4-char
`tag` (built with `NT_UI_STATE_TAG`) identifies the owning widget so two widgets
that hash to the same id+size trap on re-acquire instead of aliasing silently,
`nt_ui_state_find` returns NULL if absent, and `nt_ui_state_clear` /
`nt_ui_state_clear_all` drop one or all cells (e.g. a screen transition). The
pool is carved from the caller-provided context arena; the engine allocates no
heap. Its size comes from `nt_ui_create_desc_t.state_slots` (default
`NT_UI_STATE_SLOTS` = 256, about 19 KB) and uses the configured probe window.
It is direct-mapped + linear-probe like the anim cache, but with **no LRU
eviction**: a cell dies only via clear or context destroy. This no-eviction property is the contract that
makes the game-owned-pointer escape hatch leak-safe — for an oversize payload the
game allocates, stores the pointer in the cell, and frees it before clear, knowing
the engine will never silently reclaim it. Overflow, a size mismatch, or a tag
mismatch on re-acquire is `NT_ASSERT` (fail early, not a soft fallback). The returned pointer
is a frame-scoped cache, not held across frames — widgets re-acquire by id each
frame (the same const-cast read pattern as `nt_ui_get_bbox`). Occupancy
(slots/bytes) is surfaced on the inspector "UI memory" line. Distinct from the
anim cache: the anim cache evicts (transient easing), the state pool never evicts
(durable retained state such as a slider's drag offset or a scroll container's
momentum). Slider drag state and scroll physics state both ride it, keyed by
(optionally salted) widget id.

## Stateful widgets

`nt_ui_checkbox` / `nt_ui_radio` / `nt_ui_toggle`
are leaf widgets over one shared core. Per code-first /
explicit-over-implicit, the engine stores NO logical value — the game owns
it: `bool*` for checkbox/toggle, a shared `int*` for radio (exclusivity is
free — every radio compares `*selected == my_value`, no engine-side group
state). Each returns `changed` only on the release-over-widget frame. One
shared `nt_ui_checkbox_style_t` (strict superset for all three) holds, per
interaction state (idle/hover/pressed/disabled) × value row
(unchecked/checked), the art refs + tint + transform + opacity; only
transient visual easing lives in the engine (the anim cache above). The
indicator (box + overlay) draws on `data->layer`; the label on a separate
`label_layer` arg, so games may batch sprite-then-text. `scale_label`
chooses whether press/hover scales the whole row or only the indicator
(opacity/dim is always whole-widget). Toggle adds a render-only sliding
thumb with a symmetric `thumb_pad` end-margin. On the click-release frame the
widget renders the PRE-flip value and returns `changed` after drawing, so the
pop/slide animation begins the next frame — the same intrinsic 1-frame IM lag as
hit-test and arbitration. `nt_ui_checkbox_tri` adds a tristate (OFF/ON/MIXED) over the
same core, additive to the bool checkbox: MIXED renders a dedicated `mixed` dash row but is a
DISPLAY-only state the game sets (e.g. aggregated from children) — a click NEVER produces MIXED
(it resolves any non-ON value to ON, else toggles ON↔OFF), so Model D holds.
`nt_ui_slider` / `nt_ui_progress` extend the same
game-owns-the-value model: the float/int value lives in the game, the engine eases
only the visual fraction (`value_t`); the slider exposes its thumb screen position
(`nt_ui_slider_thumb_pos`) so the game can draw drag-bubbles via a Clay floating
element. A press ON the thumb grabs at its press offset (relative drag, value unchanged
that frame); a press on the track jumps the value to the click point. `step` quantizes
the visual snap with the value, the fill edge meets the thumb CENTER (not `fraction ×
track_w`, which would under/overshoot the thumb's travel), and an out-of-range incoming
`*value` is clamped AND written back so game memory matches what is drawn. A live drag
returns `changed` every frame; act-once callers poll the release edge (commit-on-release).
Hit padding inflates the touch target and auto-grows vertically to cover the thumb's
overhang past the track even at zero style pad. An `orientation` style field selects the drag
AXIS (horizontal default / vertical): the SAME `nt_ui_slider_float` / `_int` branch on it, with
`fill_direction` the anchor WITHIN the axis (vertical defaults to BOTTOM_UP — value 0 at the
bottom; horizontal to LTR). A `fill_direction` whose axis disagrees with `orientation` is a
developer assert and falls back to the axis default locally — the caller's style is never mutated.
Slider and progress share one fill-emit
helper (STRETCH slice9 stretch vs CROP scissor-reveal × four directions).

## Custom scroll physics

`nt_ui` scroll containers bypass Clay's built-in scroll
integration for physics, but `Clay_UpdateScrollContainers` is STILL called each frame
in neutral mode (drag disabled, zero delta) purely for its slot-GC side effect — every
CLIP element reclaims its scroll-pool slot through it, so dropping the call leaks slots
until the pool overflows (type=7 crash). The engine integrates the scroll offset itself
and feeds Clay a ready `clip.childOffset` each frame — it never reads
`Clay_GetScrollOffset`, only `Clay_GetScrollContainerData` for the content/container
clamp dims. Everything is in
Clay's NEGATIVE-down sign convention (childOffset negative going down/right, clamped
to `[-(content-container), 0]`); only the input edge (wheel) and the scrollbar-thumb
mapping flip sign. The four-behavior feel model lives in `nt_ui_scroll_style_t`
tunables: momentum/fling (exponential velocity decay by `friction`), overscroll
rubber-band + critically-damped bounce-back (`rubber_band_c` / `bounce_speed`),
smooth wheel (ease toward target by `wheel_ease_speed`, 0 = instant, no teleport),
and animated `nt_ui_scroll_to`. Release-fling velocity is tracked over a TIME WINDOW
(`TAU_GAIN`/`TAU_DECAY`), not a per-frame delta: a moving frame blends toward the
instantaneous sample, a zero-delta frame decays toward 0, so a fling survives pointer
events arriving slower than the render loop instead of depending on release-frame parity.
A press landing on a flinging container stops the fling and is swallowed (tap-to-stop).
Per-container scroll state (pos/vel/target/flags)
rides the state pool keyed by the scroll id; no heap. Wheel input is normalized to a
NOTCH unit at the platform edge — `1.0 == one physical detent` (GLFW pass-through; web
`deltaMode` divided to notches), so wheel strength is platform-independent and the
consumer scales by `wheel_step_px`. Wheel routing is exclusive
(innermost-wins): each frame every scroll container registers as a candidate, and
ownership is resolved at frame END — for each pointer the engine picks the candidate
whose just-solved bbox holds it (ranked by max scroll-nesting depth, then smallest
area, then latest declaration) and writes a single owner per pointer. That owner is
consumed the NEXT frame, so wheel routing carries a 1-frame lag: a newly shown
container sees its first wheel notch one frame late (the same intrinsic IM lag as
hit-test, which reads the previous frame's bbox). All scroll sizes and pixel
thresholds (`NT_UI_SCROLL_STEAL_THRESHOLD_PX`, `wheel_step_px`, bar/hit dimensions)
are in the coordinate space passed to `nt_ui_begin` — NOT framebuffer pixels; games
typically feed `nt_ui_scale`'s logical space, so these are resolution-independent.
Capture-steal-by-threshold
(`NT_UI_SCROLL_STEAL_THRESHOLD_PX`, ~8 px) arbitrates an inner widget's click vs a
scroll drag: a drag past the threshold cancels the inner capture and scrolls; a tap
below it leaves the inner widget to click. On the latch frame the steal routes zero
positional delta (the inner widget already applied it), tracking 1:1 from the next
frame; the cancelled inner widget keeps any value change it made before the steal
(Model D — the value lives in the game, so the engine cannot revert it). The
scrollbar (2-piece slice9 track + thumb, both axes) emits as floating children of
the container with
ALWAYS / AUTO / AUTO_HIDE visibility (AUTO_HIDE shows on scroll activity and fades
out `bar_hide_delay` seconds after the last motion, via one `nt_ui_anim` value_t).
A thumb press grabs at the press offset inside the thumb (no jump-to-center); a click
on the track off the thumb smooth-jumps to the clicked spot (`nt_ui_scroll_to`). A
thin bar still gets a ≥ 24 px touch target via a cross-axis hit pad. The
"Scissor limitation" note below still holds (AABB clip of a rotated scroll
container is unchanged).

## Virtual list

`nt_ui_vlist_begin` / `nt_ui_vlist_end` is a code-first virtualized-list
clipper (the immediate-mode analogue of Dear ImGui's `ImGuiListClipper`) over ONE engine-owned
scroll/clip. From the scroll position + viewport + a fixed `item_extent` it derives an inclusive
`{first,last}` visible window (returned to the game), emits a LEADING spacer, lets the game loop
`for (i = first; i <= last; ++i)` emitting only the visible rows, then emits a TRAILING spacer so
the content still measures `count × extent` and the existing scrollbar geometry stays correct — a
10k-row list costs ~the visible count, not the row count. Per-row ids RECYCLE over a frame-stable
ring (`id_ring`, slot = `index % id_ring`), so the distinct ids per list are bounded by the ring,
never the row count, and a long list never saturates Clay's persistent element hashmap; the visible
window is hard-clamped below the ring so two simultaneously-visible rows can never alias a slot.
Because ids follow the screen SLOT, the game dispatches per-row ACTIONS by the ABSOLUTE index and
keeps per-row PERSISTENT state game-owned (keyed by absolute index), never hung off the recycled
id — transient UI state (hover/press) follows the slot, standard IM virtualization. Both axes (Y
default, X). The general `nt_ui_child_id(parent_id, "label")` helper derives a per-widget id from a
parent scope + a string label (fmix-folded, never 0), so game code derives child ids without
inventing numeric salts.

## Text input

`nt_ui_input_text` is a single-line field over a game-owned
`char*` buffer (Model D — the engine never reallocs or owns the string; it stores
only caret/selection/scroll/blink/focus in the state pool). Editing is UTF-8
codepoint-aware: insert/backspace/delete/arrows never split a multi-byte sequence
and clamp to `buffer_size-1` / `max_length`. Typed text arrives via the
`nt_input_pop_char` UTF-32 ring (frame-local, drained each frame while focused);
physical keys drive navigation/edit. T2 adds selection (Shift+arrows, mouse drag,
double-click word-select, Ctrl+A), copy/cut/paste via the swappable `nt_clipboard`
façade (paste decodes untrusted bytes per-codepoint, runs the allow-predicate,
clamps, and drops a non-fitting multibyte codepoint whole), a render-only password
mask, stock allow-predicates (numeric/email/url), a per-field `placeholder` string
shown dimmed while empty+unfocused, and a minimal focus arbiter (Esc unfocus, Tab
advance/wrap). Caret, selection highlight, and the text all float as clipped
children carrying a `HAS_TRANSFORM` offset of `pad_x - scroll_x` (set as the Clay
element `userData`, which the walker reads for layer+transform), so the line
scrolls to keep the caret visible while clipping to the field box. Cut is a no-op
when `nt_clipboard_available()` is false (no silent delete-without-copy).

## Consolidated interaction events

`nt_ui_events(ctx, id, cfg)` is the single
canonical MUTATING interaction step (one per widget per frame), superseding the
raw `nt_ui_step_interaction` as the widget-facing call: it returns the base
capture edges (`hovered/pressed/released/held/clicked`) PLUS cfg-gated gestures
(`double_clicked`, `long_pressed`, `hold_progress` — a linear 0..1 ramp toward
`long_press_secs` while `held && hovered`). The base path delegates to
`step_interaction` verbatim (byte-for-byte capture parity); the gesture cell is
allocated ONLY when `cfg` opts in (`cfg==NULL` grows the state pool by zero), so
zero-overhead is the default — explicit-over-implicit. The dbl-click window and
drag move-radius are engine-global (`nt_ui_set_gesture_constants`); dragging off
the bbox or past the radius resets `hold_progress` and suppresses `long_pressed`.
`nt_ui_query_events` is the idempotent read-side mirror (advances nothing).
`hold_progress` is the hold-to-confirm feedback signal and feeds the radial phase.

## Popup-core

A single floating-overlay primitive (`nt_ui_popup_begin/end` +
a one-bool `nt_ui_popup_visible` wrapper) underpins every transient overlay —
modal, dropdown, tooltip, and context-menu. It owns: a Clay floating panel
attached to the ROOT (anchor-derived offset, no trigger-id dependency, so a
missing trigger element can't trip Clay's parent-not-found); trigger-anchored
placement with per-side edge-flip (BELOW/ABOVE/RIGHT/LEFT, CENTER for modal)
read from the panel's previous-frame bbox; a `value_t` open/close tween; a shared
modal-depth z-band declared as ONE `modal_zband_stride` above the enclosing
floating (Clay accumulates the nesting, see "Floating zIndex is relative" above;
NT_ASSERT before the push so a runaway nesting fails early); and a present-only, transparent light-dismiss
catcher at `panel_z-1` (outside-click raises a close signal). Esc and the
outside-click scan run on ONE CATCHER-BEARING popup per frame: the one with the
highest effective band, ties to the last declared at the higher draw layer —
the walker's own paint key, so the popup that consumes the event is the one
painted on top even when a game floating shifts the band of a popup declared
inside it. A catcher-less overlay (menu, tooltip) never claims that slot and
runs its own dismiss; the text field's Esc-unfocus is independent of both. A fully-closed
popup declares NO catcher, so the base UI stays clickable; a hover-driven
overlay (tooltip) can clear the catcher flag entirely. Dismiss is always a
SIGNAL the game acts on (Model D) — popup-core never owns the open bool. The
context-menu stacks one popup-core fly-out per submenu level; the only novel
algorithm is the mouse-aim triangle that keeps a submenu open while the cursor
travels diagonally toward it (never collapse on raw hover-loss).

## Immediate menu + combo

The context-menu and dropdown are immediate-mode:
the game declares the tree (or list) in code EVERY frame; there is no items[]
data array and no `chosen_id` sink. Menu rows are declared with
`nt_ui_menu_begin` / `item` / `item_ex` / `item_begin..item_end` /
`submenu_begin` / `submenu_begin_ex..submenu_end` / `separator` /
`separator_text` / `menu_end`; the combo with `nt_ui_combo_begin` /
`selectable` / `selectable_icon` / `selectable_begin..selectable_end` /
`preview_begin..preview_end` / `combo_end`. Activation is reported INLINE by
the row call (`if (nt_ui_menu_item(...)) act();` / a combo selectable returns
clicked) — there is no per-frame result struct to scan. The game owns the open
state (`nt_ui_menu_state_t` / `bool *open`) and, for the combo, the `int`
selection; the widget only signals and clears `open` on a pick (Model D, same
as popup-core). Both build on the kept popup-core (+ the scroll wrapper for a
long combo list). There is no data-array form — immediate-only.

Per-frame menu scratch is GAME-owned (`nt_ui_menu_ctx_t`, allocated by the
game — static / app field / arena slice — and init via `nt_ui_menu_init`), so
the core `nt_ui_context` pays zero bytes for a game that uses no menu and the
menu TU dead-strips. The SAME instance MUST be reused every frame: it holds the
per-level keyboard-nav record, so a fresh `{0}` each frame would silently break
nav. The combo carries no such scratch (its state lives in the core ctx's pool,
bounded by caps). Row identity is KEY-STABLE via a scope stack:
`mix(scope_id, key)` (fmix, never additive — additive sibling ids collide in
Clay's anonymous child-id space). A submenu pushes its own row id as the child
scope (ImGui `PushID` model), so keys need only be unique among SIBLINGS; a
duplicate sibling key aliases the same row state and is a DEBUG fail-early
(complete for the menu's bounded per-level cap, best-effort window-scanned for
the unbounded combo list — a complete scan there would need heap or O(N²)).
Keyboard nav runs in `menu_end` against THIS frame's per-level record (built as
rows declare, complete by `menu_end`); a focus or open-chain change re-declares
the tree and becomes navigable NEXT frame — a 1-frame latency in the EFFECT,
not in the record lookup.

## Atlas region identity

`nt_atlas_region_ref_t { uint64_t name_hash; nt_resource_t atlas;
uint32_t region; }` is the canonical "sprite-in-atlas" handle (atlas.id==0
is the unset handle; consumers assign their own meaning). The widget APIs
that take atlas art — `nt_ui_image`, `nt_ui_panel_begin`, and the
button/checkbox style structs — take this single ref rather than separate
`(atlas, region_index)` arguments.

## Scissor limitation

GL scissor is axis-aligned in framebuffer
space. A rotated scroll container is clipped by the AABB of its
rotated corners, so content can poke past the visual corners.
Stencil-mask fix is out of scope for v1.

## Text under non-uniform scale

Font atlas em-size is picked from
the X-column magnitude of the composed affine (`sqrt(a²+c²)`). Under
uniform scale or pure rotation, glyph rasterisation stays crisp;
under `sx ≠ sy` the quad stretches but the rasteriser samples one
axis, blurring the other.
