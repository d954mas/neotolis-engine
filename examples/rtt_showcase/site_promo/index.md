---
title: RTT Showcase
description: Dedicated render-to-texture demo with offscreen rendering, gaussian blur, depth sampling, and target recreation on size change.
---

Canonical Phase 74 proof: render a generic 3D scene into an offscreen target, sample the color and depth attachments, blur through `nt_postfx_blur`, and recreate the targets on a size change, re-fetching the attachment handles.
