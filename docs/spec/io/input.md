# Input System

Polling-based input: the game queries state each frame, no event callbacks.
The mouse and touch/pen contacts share one pointer struct with position,
deltas and button edges, but live by different rules.

Related: [Platform Architecture](../runtime/platform.md), [Frame Lifecycle](../runtime/frame-lifecycle.md), [Logging, Errors, Debugging](../debug/logging-errors-debugging.md)

## Model

The window module applies each device event to the next frame's input state when the platform
delivers it, in arrival order (native: GLFW callbacks during `glfwPollEvents` in `nt_window_poll`;
web: the DOM listeners, as each event fires). Coordinates map with the DPR of the last poll, so a DPR
change (another monitor, browser zoom) maps input from the next frame. A GLFW callback outside
`glfwPollEvents` (a window operation such as fullscreen) also writes only the next frame.
`nt_input_poll` publishes that state for the frame and starts the next one, so the state the game
reads changes only at the poll. Edges accumulate: a press and a release between two polls are both
readable. `g_nt_input` is the whole published frame (pointers, primary, key state, the frame's
typed characters); it and the query functions are read-only for their consumers.

A focus loss is applied in that order: it releases all held input (while the bot drives, the gate
drops it and the bot's input stays), and events after it apply after it. A release and a new press
of one button in one frame therefore read pressed and released together, like any fast press and
release inside one frame. On web a focus loss is a blur of the canvas or of the window: a browser
may keep the canvas focused across an app or tab switch.

## Keys

`nt_key_t` names physical keys, independent of the layout: letters, top-row digits, arrows,
Space/Enter/Escape/Tab/Backspace, left and right Shift/Ctrl/Alt/Meta (Meta is Cmd on macOS),
F1–F12, Delete/Insert/Home/End/PageUp/PageDown, the punctuation keys and the numpad. Each platform
maps its own key codes to it by name (web: `KeyboardEvent.code`; native: GLFW keys); a key outside
the set is not reported. macOS sends no keyup for a key released while Cmd is held, so on web
Cmd's release also releases the keys pressed with it.

## Typed characters

Text input is separate from key state: characters arrive as UTF-32 codepoints, up to
`NT_INPUT_CHAR_MAX` (32) per frame, read in order by the focused widget via:

```c
bool nt_input_pop_char(uint32_t *out_codepoint);  // false when empty
void nt_window_set_text_input_mode(nt_text_input_mode_t mode);  // soft-keyboard hint (window/nt_window.h); no-op on native/stub
```

- Chars carry *text* (layout-resolved, includes key repeat); physical keys carry
  navigation/editing. Web has no GLFW char callback, so the web keydown listener
  synthesizes chars from `e.key` incl. `e.repeat` to match native behavior.
- Frame-local like key edges: `nt_input_poll` publishes the characters typed since
  the previous poll and drops the ones the game left unread. Each publish has its own
  capacity, so unread text never takes room from the next frame's typing.
- Overflow is drop-newest: earlier characters of the frame are never clobbered.

## Pointers

`g_nt_input.pointers[NT_INPUT_MAX_POINTERS]` holds `nt_pointer_t` (`engine/input/nt_input.h`):
position and deltas in framebuffer pixels (fractional, never rounded), wheel notches, pressure, type, and per-button
`is_down`/`is_pressed`/`is_released`.

- **`pointers[0]` is the mouse.** It is active while the cursor is over the window, or while a
  button is held (a drag outside the window keeps it). A button release keeps it; after a leave
  or a focus loss it is published once more (that frame still reads the release) and then
  deactivates, unless a move brings it back first. The wheel
  lands only on an active mouse. On a device without a mouse it stays inactive. Its pressure is 1
  while a button is down, else 0. A press counts only when it starts on the window (on web: from a
  press on the canvas until all buttons are up or the canvas loses focus), so a drag from outside
  never presses.
- **Touch and pen contacts take slots 1..7** (7 contacts), each from its down to its up, with the
  platform contact id. A move for an unknown id is ignored, so a hovering pen has no slot (pen
  hover is not supported).
- **The primary pointer**, for code that follows one pointer: `g_nt_input.pointers[g_nt_input.primary]`,
  chosen by `nt_input_poll` before it publishes. A pointer becomes primary by a press; the mouse also takes
  it by hovering while no pointer is held or releasing. It stays primary from its press through its
  release frame. A contact already held when the primary ends never takes over, and a press that
  lands while the primary is held or releasing is not primary: a single-pointer consumer never sees
  a press or a release it did not get, and a widget capture never moves to another device. The cost
  is that a single-pointer consumer misses such a press, for example a second finger landing in the
  frame the first lifts; a UI that needs it passes the whole array. With no candidate the index
  stays on the last primary, now inactive. `nt_ui_begin(..., pointers, count, primary)` takes the
  primary explicitly: a single-pointer UI passes `&pointers[g_nt_input.primary], 1, 0`; a
  multi-pointer UI passes the whole array with `g_nt_input.primary`, so a widget capture stays with
  its slot. `nt_ui` anchors menus, rich-text links, the Clay feed and the inspector on that pointer.

Every real mouse event is a move with the held buttons (a button change too), plus a leave; each
real wheel event first moves the mouse to the cursor. Contacts arrive as down, move and up.
Devapi input automation drives the same mouse and its own contacts (see "Input automation" in
[Logging, errors and debugging](../debug/logging-errors-debugging.md)).

Pointer capture lives in `nt_ui` (per pointer index), not in the input system.
