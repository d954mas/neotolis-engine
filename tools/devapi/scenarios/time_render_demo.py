#!/usr/bin/env python3
"""DevAPI time/render demo — the example bot-driven workflow.

Connects to a running examples/devapi_host over its devapi transport (native or web) and drives the
time/render/frame command group through three workflows, in order:

  1. pause -> step -> observe — pause the sim, switch to manual (lockstep) mode,
     step exactly 5 fixed-dt frames, and assert frame advanced by exactly 5.
  2. lockstep crunch — manual mode + render OFF + fps 0 (uncapped) + step(count=N):
     advance a few hundred deterministic frames as fast as the host can run them,
     assert frame advanced by exactly N (reproducible fast runs use lockstep crunch,
     not dt-scale which is observation-only).
  3. render-off fast test — assert render.info reports {enabled:false, draw_calls:0},
     then restore: render ON + resume.

Usage: python -m tools.devapi.scenarios.time_render_demo [--target native|web] (options: tools/devapi/launch.py)
Exit 0 pass, 1 failure, 2 usage / launch (tools/devapi/launch.py).
"""
import sys

from .. import launch
from ..client import DevApiClient

# Lockstep-crunch frame budget — large enough to prove deterministic fast-forward.
CRUNCH_FRAMES = 300


def run(client: DevApiClient) -> None:
    """Drive the three workflows; raise AssertionError on any contract violation."""
    # 1. pause -> step -> observe.
    client.pause()
    start = client.frame_current().get("frame")
    assert isinstance(start, int), f"frame.current.frame is {start!r}, expected int"
    client.set_mode("manual")
    client.step(count=5)
    after = client.frame_current().get("frame")
    assert after == start + 5, f"manual step(5): frame {start} -> {after}, expected {start + 5}"

    # 2. lockstep crunch — manual + render off + fps 0 + a big step burst.
    client.set_mode("manual")
    client.render_set_enabled(False)
    client.set_fps(0)
    crunch_start = client.frame_current().get("frame")
    client.step(count=CRUNCH_FRAMES)
    crunch_after = client.frame_current().get("frame")
    assert crunch_after == crunch_start + CRUNCH_FRAMES, (
        f"crunch step({CRUNCH_FRAMES}): frame {crunch_start} -> {crunch_after}, "
        f"expected {crunch_start + CRUNCH_FRAMES}"
    )

    # 3. render-off fast test — draw_calls must be 0 while render is disabled.
    info = client.render_info()
    assert info.get("enabled") is False, f"render.info.enabled is {info.get('enabled')!r}, expected False"
    assert info.get("draw_calls") == 0, f"render.info.draw_calls is {info.get('draw_calls')!r}, expected 0"

    # Restore a normal live state for the next session: render on, back to RUN, unpaused.
    # (set_mode("run") matters — resume() only clears the pause flag; without it the host stays
    # in MANUAL and looks frozen, dt=0, to any follow-on session.)
    client.render_set_enabled(True)
    client.set_mode("run")
    client.resume()

    print(
        "PASS: pause+manual step(5) advanced 5 frames; lockstep crunch advanced "
        f"{CRUNCH_FRAMES} frames; render-off render.info draw_calls=0; restored"
    )


if __name__ == "__main__":
    sys.exit(launch.main(run))
