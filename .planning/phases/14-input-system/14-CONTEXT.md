# Phase 14: Input System - Context

**Gathered:** 2026-03-12
**Status:** Ready for planning

<domain>
## Phase Boundary

Game code can poll keyboard and mouse/touch state each frame with correct coordinate mapping. Covers PLAT-04 (keyboard polling), PLAT-05 (mouse polling), PLAT-06 (CSS→framebuffer coordinate mapping), and INPUT-01 (touch input with unified pointer model — pulled forward from future requirements). Single nt_input module with swappable backends.

</domain>

<decisions>
## Implementation Decisions

### Key representation
- Named enum with ~60 common game keys: A-Z, 0-9, arrows, Space, Enter, Escape, Shift, Ctrl, Alt, Tab, F1-F12
- Maps from physical KeyboardEvent.code (layout-independent — WASD stays WASD on AZERTY/QWERTZ)
- Fixed-size array with compile-time limit (NT_KEY_COUNT), no heap
- Left and right modifiers as separate enum values (NT_KEY_LSHIFT, NT_KEY_RSHIFT, etc.)

### Key state API
- `nt_input_key_is_down(key)` — held this frame (sustained)
- `nt_input_key_is_pressed(key)` — just pressed this frame (edge, one frame)
- `nt_input_key_is_released(key)` — just released this frame (edge, one frame)
- All three with "is" prefix for consistency and boolean clarity
- `nt_input_any_key_pressed()` — true if any tracked key was pressed this frame
- Keyboard state internal (not exposed in g_nt_input struct) — accessed only through functions
- Internally: two arrays (current + previous frame) for edge detection

### Key capture
- Block browser default behavior on common game keys (Space, Tab, arrows, F-keys) via preventDefault
- Keyboard events on canvas element with tabindex (not document-level)
- Only captures when canvas has focus — doesn't interfere with dev console or other page UI

### Unified pointer model
- Pointer array: `g_nt_input.pointers[NT_INPUT_MAX_POINTERS]` where NT_INPUT_MAX_POINTERS = 8
- Type-agnostic, ordered by arrival — pointer 0 = first event received (mouse on desktop, first touch on phone)
- No fixed "mouse slot" — mouse is just another pointer with type = NT_POINTER_MOUSE
- Sparse slots with active check — gaps possible, game iterates all checking `active`
- Slot lifecycle: pointerdown → find free slot, store browser pointerId, mark active; pointermove → find by id, update; pointerup/pointercancel → find by id, mark inactive, set released
- Last position kept when pointer deactivates (useful for "where did the touch end")
- Slot reused by next new pointer event

### Pointer struct fields
- `uint32_t id` — browser PointerEvent.pointerId (stable per touch lifecycle, for cross-frame tracking)
- `float x, y` — position in framebuffer pixels, relative to canvas top-left
- `float dx, dy` — movement delta in framebuffer pixels (computed engine-side: current - previous)
- `float wheel_dx, wheel_dy` — mouse wheel delta (from separate wheel event, mouse pointer only)
- `float pressure` — 0.0-1.0 (mouse: 0.5 when pressed, touch/pen: actual)
- `uint8_t type` — NT_POINTER_MOUSE, NT_POINTER_TOUCH, NT_POINTER_PEN
- `bool active` — pointer currently exists (mouse hover or touch contact)
- `nt_button_state_t buttons[NT_BUTTON_MAX]` — per-button state

### Button state
- `nt_button_state_t` struct per button: `is_down`, `is_pressed`, `is_released`
- NT_BUTTON_LEFT, NT_BUTTON_RIGHT, NT_BUTTON_MIDDLE
- For touch: buttons[NT_BUTTON_LEFT] = touch contact (primary action)
- No top-level is_down/is_pressed/is_released on pointer — all state lives in buttons[]
- Mouse convenience helper: `nt_input_mouse_is_down(NT_BUTTON_RIGHT)` finds mouse pointer automatically

### Browser event API
- PointerEvent exclusively (handles mouse, touch, pen) — no separate MouseEvent/TouchEvent
- Wheel event listener for scroll data (attached to mouse pointer)
- Canvas-relative coordinates via getBoundingClientRect (reliable cross-browser, handles CSS transforms and scrolled pages)

### Coordinate space
- All positions in framebuffer pixels: CSS offsetX/Y * effective DPR
- Relative to canvas top-left (canvas offset on page handled transparently)
- dx/dy also in framebuffer pixels (consistent with position)
- Feeds directly into camera unproject (Phase 17) without conversion

### Module organization
- Single module: `engine/input/`
- `nt_input.h` — public API, g_nt_input struct (pointers array), key query functions, types/enums
- `nt_input.c` — shared logic (edge detection, delta computation, coordinate mapping via g_nt_window.dpr)
- `web/nt_input_web.c` — PointerEvent/KeyboardEvent/WheelEvent listeners via EM_JS
- `native/nt_input_native.c` — desktop stubs with fake defaults for testing (TODO future GLFW/platform)
- `stub/nt_input_stub.c` — no-op implementation for headless builds (builder, tests, CI)
- CMake: INTERFACE API + STATIC implementations (swappable backend pattern)

### Lifecycle
- `nt_input_init()` — allocates state, registers event listeners (web), sets up arrays
- `nt_input_poll()` — called by game each frame: processes accumulated events, updates edge detection, computes deltas
- `nt_input_shutdown()` — cleanup, remove listeners
- Game controls call order: typically nt_input_poll() at start of frame function

### Claude's Discretion
- Exact EM_JS function signatures for event registration and data passing
- Internal event queue/buffer design (how browser events accumulate between polls)
- Key enum exact values and internal mapping table (code string → enum)
- CMakeLists.txt specifics (link flags, platform conditionals, target names)
- How getBoundingClientRect is cached (per-frame vs per-event)
- Native stub fake input values for testing
- Whether to use emscripten HTML5 API or raw EM_JS for event registration

</decisions>

<specifics>
## Specific Ideas

- "Mouse is a touch too" — unified pointer model from the start, not bolted on later
- On phones there's no mouse — pointer 0 is the first finger, not a phantom mouse
- "Game developer does everything explicitly" — nt_input_poll() called by game, not auto-polled
- Think like GLFW/raylib: function-per-state for keys, button enum for pointer buttons
- Physical keyboard codes (KeyboardEvent.code) for layout-independent game controls

</specifics>

<code_context>
## Existing Code Insights

### Reusable Assets
- `nt_web.h`: EM_JS + inline no-op fallback pattern — reuse for event listener registration
- `nt_platform.h`: NT_PLATFORM_WEB / NT_PLATFORM_NATIVE defines for ifdef-based platform split
- `nt_window.h` / `g_nt_window`: DPR and framebuffer dimensions for coordinate mapping
- `nt_types.h`: nt_result_t for error returns, standard type includes
- Shell HTML: canvas already has tabindex-ready structure, "no border/padding" comment for mouse coords

### Established Patterns
- Swappable backend: shared .c + per-platform implementations (nt_app, nt_window, nt_log)
- Global extern struct: `g_nt_` prefix (g_nt_app, g_nt_window → g_nt_input)
- File-scope statics (`s_`) for internal module state (previous frame arrays, event buffers)
- EM_JS over EM_ASM for JS bridge (avoids C17 variadic macro warning)
- `#ifdef NT_PLATFORM_WEB` with typedef fallback for native clang-tidy
- nt_add_module() CMake helper for new modules

### Integration Points
- nt_input reads g_nt_window.dpr for CSS→framebuffer coordinate mapping
- nt_input reads g_nt_window.fb_width/fb_height for bounds awareness
- Game's frame function calls nt_input_poll() before update logic
- Demo (Phase 17) uses nt_input for cube rotation/camera control
- nt_input links to nt_core (types, platform defines)

</code_context>

<deferred>
## Deferred Ideas

Migrated to GitHub issues — see label `deferred`.

</deferred>

---

*Phase: 14-input-system*
*Context gathered: 2026-03-12*
