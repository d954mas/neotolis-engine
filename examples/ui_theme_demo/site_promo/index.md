---
title: UI Theme Demo
description: Model D theme hot-swap — game-side palette pattern flips per-widget styles with a single pointer write. Press T to toggle dark/light.
---

Demonstrates the Phase 53 v1.8 UI theme system: each widget owns a `nt_ui_<widget>_style_t` struct, and "themes" are a game-managed palette of pointers. No engine API is involved in the swap — pressing `T` flips a single `g_current` palette pointer between `g_dark` and `g_light`, and the next frame's labels render with the new style.

Six labels per frame exercise every `nt_ui_label_style_t` field: font size, color, text alignment (center + right), wrap mode + line height, and letter tracking.
