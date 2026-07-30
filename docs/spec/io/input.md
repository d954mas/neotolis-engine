# Input System

Polling-based input: the game queries state each frame, no event callbacks.
Mouse and touch unify under one pointer model with position, deltas, and edge
flags; per-pointer capture ownership is stored centrally in the input system
with auto-release on pointer release.

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

## Pointer state

```c
typedef struct InputPointer {
    bool active;
    bool down;
    bool pressed;
    bool released;

    float x;
    float y;
    float prev_x;
    float prev_y;
    float dx;
    float dy;

    uint32_t capture_owner;
} InputPointer;
```

    Mouse and touch unify under pointer model
        .

## Input capture

Capture is stored centrally in input system.

```c
bool input_try_capture(int pointer, uint32_t owner);
void input_release_capture(int pointer, uint32_t owner);
bool input_is_owner(int pointer, uint32_t owner);
bool input_pointer_captured(int pointer);
```

Raw input always exists; capture only affects processing ownership.

Capture owner: not necessarily entity id, generic `uint32_t owner_id` chosen by game/systems. Auto-release on pointer release.
