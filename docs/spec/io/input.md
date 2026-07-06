# Input System

Polling-based input: the game queries state each frame, no event callbacks.
Mouse and touch unify under one pointer model with position, deltas, and edge
flags; per-pointer capture ownership is stored centrally in the input system
with auto-release on pointer release.

Related: [Platform Architecture](../runtime/platform.md), [Frame Lifecycle](../runtime/frame-lifecycle.md), [Logging, Errors, Debugging](../debug/logging-errors-debugging.md)

## Model

Input system is polling-based. Game queries state each frame, does not subscribe to callbacks.

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
