# Frame Lifecycle

The engine owns top-level frame execution; the game provides
init/fixed_update/update/render/shutdown callbacks. Defines the engine frame
order (input, resource step, scratch reset, fixed loop, transform update,
render), the fixed-update accumulator loop, the optional interpolation factor,
and the explicit no-system-registry policy.

Related: [Memory Policy](memory.md), [Platform Architecture](platform.md), [Transform](../data/transform.md)

The engine owns the top-level frame execution. The game provides callbacks.

## Engine callbacks exposed to game

```c
void game_init(void);
void game_fixed_update(float dt);
void game_update(float dt);
void game_render(void);
void game_shutdown(void);
```

## Engine frame order

```text
platform_step
input_begin_frame
    → if pointer pressed && audio suspended → audio_try_resume()
input_event_apply
resource_step         ← async loading processing
game-defined resource sync helpers
    → e.g. sprite_comp_sync_resources() after resource publication changes
audio_update          ← voice state management
nt_mem_scratch_reset  ← frame scratch arena cleared (see memory.md — memory categories)
fixed_update loop
game_update           ← CLAY layout, NT_UI_DATA_* allocations
transform_update
game_render           ← nt_ui_walk reads scratch pointers
```

`nt_mem_scratch_reset()` MUST run before any scratch allocation in the
current frame — typically right after `audio_update`. Allocating then
resetting in the same frame invalidates pointers already handed to
systems (e.g. `nt_ui` retains them through `nt_ui_walk`).

## Fixed update loop

```c
accumulator += frame_dt;
int fixed_steps = 0;

while (accumulator >= fixed_dt && fixed_steps < max_fixed_steps)
{
    game_fixed_update(fixed_dt);
    accumulator -= fixed_dt;
    fixed_steps++;
}
```

Recommended defaults:

```c
fixed_dt = 1.0f / 60.0f
max_fixed_steps = 4
```

## Update responsibilities

### `game_fixed_update(dt)`

Stable simulation: movement, AI, combat, timers, deterministic logic, future physics.

### `game_update(dt)`

Frame-based logic: camera, UI logic, effect fades, interpolation inputs, render-state preparation.

### `transform_update()`

Happens after gameplay movement, before render.

## Optional interpolation factor

```c
float alpha = accumulator / fixed_dt;
```

Can be used for render interpolation between previous/current transform state.

## Systems registry not used

The engine does not have a system registry. The game calls its systems explicitly in code. System order is defined explicitly. No phases, dependency graph, or scheduler.

```c
void game_fixed_update(float dt)
{
    movement_system_fixed(dt);
    ai_system_fixed(dt);
    combat_system_fixed(dt);
}

void game_update(float dt) {
    camera_system_update(dt);
    ui_system_update(dt);
}
```
