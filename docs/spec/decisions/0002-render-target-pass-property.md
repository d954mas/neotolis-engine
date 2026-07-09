# ADR: Render Target Selection As A Pass Property

**Status:** Accepted
**Date:** 2026-07-09
**Context:** Phase 74 - RTT / Offscreen-Framebuffer Subsystem

## Decision

Render target selection is part of the pass descriptor. Public code selects the
pass destination through `nt_pass_desc_t.target`; zero selects the default
framebuffer, and a valid `nt_render_target_t` selects an offscreen render
target.

The primary API is not public render-target bind/unbind state. `nt_gfx` maps the
descriptor field to backend framebuffer binding inside `nt_gfx_begin_pass`, and
`nt_gfx_end_pass` closes the pass state machine.

## Rationale

The game owns pass order, so the target belongs next to the rest of the pass
setup. Keeping destination selection in `nt_pass_desc_t` makes pass intent
visible at the callsite and avoids sticky framebuffer state that can leak between
passes.

The shape also keeps backend details private. GL FBOs and renderbuffers are
implementation objects; public users pass engine handles and sample attachment
textures through the existing texture API.

## Consequences

- Default-framebuffer rendering stays explicit: use a zero `target`.
- Offscreen rendering is explicit per pass: set `target` to an
  `nt_render_target_t`.
- There is no public bind/unbind stack to restore or accidentally leave dirty.
- Backend framebuffer binding remains internal to the selected gfx
  implementation.
- Future post-fx, portals, minimaps, depth-aware passes, and text soft-shadow
  consumers compose by declaring pass destinations, not by mutating global
  render-target state.
