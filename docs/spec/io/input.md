# Input System

Polling-based input: the game queries state each frame, no event callbacks.
The mouse and touch/pen contacts share one pointer struct with position,
deltas and button edges, but live by different rules.

Related: [Platform Architecture](../runtime/platform.md), [Frame Lifecycle](../runtime/frame-lifecycle.md), [Logging, Errors, Debugging](../debug/logging-errors-debugging.md)

## Model

Input system is polling-based. Game queries state each frame, does not subscribe to callbacks.

## Typed characters

Text input is separate from key state: characters arrive as UTF-32 codepoints in a
small FIFO ring (`NT_INPUT_CHAR_RING`, 32, power-of-2), drained by the focused
widget via:

```c
bool nt_input_pop_char(uint32_t *out_codepoint);  // false when empty
void nt_input_set_text_input_mode(nt_text_input_mode_t mode);  // soft-keyboard hint; no-op on native/stub
```

- Chars carry *text* (layout-resolved, includes key repeat); physical keys carry
  navigation/editing. Web has no GLFW char callback, so the web backend
  synthesizes chars from `e.key` incl. `e.repeat` to match native behavior.
- Frame-local like key edges: `nt_input_poll` drops unconsumed chars, then the
  platform poll refills this frame's typing — a focused widget always sees the
  current frame's characters.
- Overflow is drop-newest: a full ring never clobbers unread characters.

## Pointers

`g_nt_input.pointers[NT_INPUT_MAX_POINTERS]` holds `nt_pointer_t` (`engine/input/nt_input.h`):
position and deltas in framebuffer pixels, wheel notches, pressure, type, and per-button
`is_down`/`is_pressed`/`is_released`.

- **`pointers[0]` is the mouse.** It is active while the cursor is over the window, or while a
  button is held (a drag outside the window keeps it). A button release keeps it; a leave or a
  focus loss deactivates it on the next poll, so that frame still reads the release. The wheel
  lands only on an active mouse. On a device without a mouse it stays inactive. Its pressure is 1
  while a button is down, else 0. A press counts only when it starts on the window (on web: while
  the canvas holds the pointer capture), so a drag from outside never presses.
- **Touch and pen contacts take slots 1..7** (7 contacts), each from its down to its up, with the
  platform contact id. A move for an unknown id is ignored, so a hovering pen has no slot (pen
  hover is not supported).
- **The primary pointer**, for code that follows one pointer: `nt_pointer_primary(pointers, count)`
  returns the first active pointer with a button down or released this frame, else the first active
  one, else `pointers[0]`, in slot order: a pointer with a button down comes first, then one released
  this frame. So a touch in use beats a hovering mouse, a lifted touch stays primary through its
  release frame, and of two held contacts the lower slot wins. `nt_ui` picks its primary (menu
  anchor, rich-text links, inspector) with the same rule over the array the game passes. A
  single-pointer UI passes the primary (`nt_pointer_primary(...)`, count 1); a multi-pointer UI
  passes the whole array, so a widget capture stays with its slot. With one pointer a capture
  follows the primary, so a press with the other device while one is held (mouse and touch at
  once) moves it.

Backends feed the mouse through `nt_input_mouse_move(x, y, buttons)` (every real mouse event,
including a button change) and `nt_input_mouse_leave()`; contacts through
`nt_input_pointer_down/move/up`. Each real wheel event first moves the mouse to the cursor.
Devapi input automation drives the same mouse and its own contacts (see "Input automation" in
[Logging, errors and debugging](../debug/logging-errors-debugging.md)).

Pointer capture lives in `nt_ui` (per pointer index), not in the input system.
