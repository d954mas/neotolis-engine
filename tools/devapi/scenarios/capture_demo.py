#!/usr/bin/env python3
"""DevAPI capture demo — the live-host UAT for the frame-capture group.

Exercises capture.frame / capture.region end-to-end against a real running examples/devapi_host, native
or web. Capture is deferred to the rendered frame, which only a live host + real GL context drives.
The host clears a known two-tone pattern; this UAT checks a capture in RUN, then with the sim frozen in
MANUAL the PNG decodes, the pattern's orientation and channel order, region == the same crop of the full
frame, scale dims, bad_params over the wire, and that capturing never advances the frame.

With --port it attaches to a running examples/devapi_host (its pattern is probed); the host returns to
RUN when the client leaves. The decoded PNGs go to the --out directory.

Usage: python -m tools.devapi.scenarios.capture_demo [--target native|web] (options: tools/devapi/launch.py)
Exit 0 pass, 1 failure, 2 usage / launch (tools/devapi/launch.py).

Pillow + numpy are imported lazily inside the capture branch via tools/devapi/pixel_health.py — the
harness core stays stdlib-only.
"""
import os
import sys

from .. import launch
from ..client import DevApiClient, DevApiResultError

def run(client: DevApiClient, out_dir: str) -> None:
    """Drive every capture surface; raise AssertionError on any contract violation.

    pixel_health (Pillow + numpy) is imported HERE, lazily — the harness core never pulls it.
    """
    from .. import pixel_health  # capture-scoped import: lazy, inside the capture branch.

    # Read the live framebuffer dims so expected sizes are derived, never pinned.
    view = client.result("view")
    fb_w = int(view["fb_width"])
    fb_h = int(view["fb_height"])
    assert fb_w > 0 and fb_h > 0, f"view returned a zero-size framebuffer ({fb_w}x{fb_h})"

    # RUN first: a capture resolves while the sim runs too.
    pixel_health.check_payload(client.capture_frame(), fb_w, fb_h)

    # MANUAL with no step queued: the sim stays frozen, so every capture reads the same frame.
    client.set_mode("manual")
    frame_before = client.frame_current()["frame"]

    # 1. capture.frame resolves at the next rendered frame; probe the pattern (orange box at top-left x 1/8..5/8,
    #    y 1/8..5/8 on a blue background) so a Y flip, X mirror or R/B swap fails.
    img = pixel_health.check_payload(client.capture_frame(), fb_w, fb_h).convert("RGB")
    fg = img.getpixel((fb_w // 4, fb_h // 4))
    below = img.getpixel((fb_w // 4, fb_h * 3 // 4))
    right = img.getpixel((fb_w * 3 // 4, fb_h // 4))
    assert fg[0] > fg[2], f"capture.frame: box pixel {fg} is not red-dominant orange (R/B swap or Y flip?)"
    assert below[2] > below[0], f"capture.frame: pixel below the box {below} is not the blue background (Y flip?)"
    assert right[2] > right[0], f"capture.frame: pixel right of the box {right} is not the blue background (X mirror?)"
    img.save(os.path.join(out_dir, "capture_frame.png"))
    print(f"PASS[1/5] capture.frame: {fb_w}x{fb_h} PNG, pattern orientation and channel order hold.")

    # 2. capture.region equals the same crop of the full frame. A non-zero y catches a wrong gl_y conversion.
    rx, ry, rw, rh = fb_w // 4, fb_h // 16, fb_w // 2, fb_h // 2
    region = pixel_health.check_payload(client.capture_region(rx, ry, rw, rh), rw, rh).convert("RGB")
    assert region.tobytes() == img.crop((rx, ry, rx + rw, ry + rh)).tobytes(), (
        f"capture.region {{x:{rx},y:{ry},w:{rw},h:{rh}}} differs from the same crop of capture.frame"
    )
    region.save(os.path.join(out_dir, "capture_region.png"))
    print(f"PASS[2/5] capture.region {{x:{rx},y:{ry},w:{rw},h:{rh}}} equals the full-frame crop.")

    # 3. scale 2 and 4 on a rect both divide (any framebuffer size): post-scale dims.
    sw, sh = fb_w - fb_w % 4, fb_h - fb_h % 4
    for factor in (2, 4):
        pixel_health.check_payload(client.capture_region(0, 0, sw, sh, scale=factor), sw // factor, sh // factor)
    print(f"PASS[3/5] capture.region {sw}x{sh} scale 2 -> {sw // 2}x{sh // 2}, scale 4 -> {sw // 4}x{sh // 4}.")

    # 4. Bad params -> bad_params over the wire (never an assert / crash on untrusted bot input). Both
    #    axes are proven OOB over the real framebuffer: w>fb_w (X) and h>fb_h (Y).
    for method, params, label in (
        ("capture.frame", {"scale": 3}, "capture.frame scale=3"),
        ("capture.region", {"x": 0, "y": 0, "w": fb_w + 1, "h": fb_h}, "capture.region w>fb_w"),
        ("capture.region", {"x": 0, "y": 0, "w": fb_w, "h": fb_h + 1}, "capture.region h>fb_h"),
        ("capture.region", {"x": 0, "y": 0, "w": 0, "h": fb_h}, "capture.region w=0"),
    ):
        try:
            client.result(method, params)
            raise AssertionError(f"{label} unexpectedly succeeded; expected bad_params")
        except DevApiResultError as exc:
            assert "bad_params" in str(exc), f"{label} raised {exc!r}, expected bad_params"
    print("PASS[4/5] bad params (scale=3, region w>fb_w, region h>fb_h, region w=0) -> bad_params over the wire.")

    # 5. MANUAL: capturing never advanced the frozen sim.
    frame_after = client.frame_current()["frame"]
    assert frame_after == frame_before, f"capture advanced the MANUAL sim: frame {frame_before} -> {frame_after}"
    print(f"PASS[5/5] MANUAL: frame.current stayed {frame_before} across every capture.")

    print("PASS: capture.* end-to-end over the transport — deferred capture + region + scale + bad_params + pixel-health all machine-asserted.")


if __name__ == "__main__":
    sys.exit(launch.main(run))
