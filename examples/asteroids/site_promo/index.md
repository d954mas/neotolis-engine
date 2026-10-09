---
title: Methane Asteroids — Neotolis port
description: Neotolis port of Evgeny Gorodetskiy's Methane Asteroids, with 1,000–50,000 objects and original mesh, texture and LOD variation.
---

A Neotolis C17/WebGL 2 port of [Methane Asteroids](https://github.com/MethanePowered/MethaneAsteroids)
by Evgeny Gorodetskiy, Apache 2.0. The original camera, Mars, Galaxy sky and
scene workload are retained; asteroid shapes, noise textures and the scene are
generated at startup. Draws are grouped by texture, LOD and shape into indexed
instanced runs.

Keys 0–9 select complexity; Ctrl+P pauses; L shows LOD colors. Mouse drag or one
finger orbits; the wheel or pinch zooms. Settings provides controls, statistics
and author credits. The default is 2,000 objects, with an explicit 50,000 preset.
Browser and target-device performance still require validation.

Mars: [Solar System Scope](https://www.solarsystemscope.com/textures/), CC BY 4.0.
Galaxy panorama: ESO/S. Brunier, CC BY 4.0; adapted as a cubemap in Methane Asteroids.
[Panorama source](https://www.eso.org/public/images/eso0932a/).
[Full credits and license links](/wasm/asteroids/CREDITS.md).
