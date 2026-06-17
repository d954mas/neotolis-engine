#!/usr/bin/env python3
"""DevAPI time/render demo — the example bot-driven workflow.

Connects to a running examples/devapi_host over loopback TCP and drives the
Phase-65 time/render/frame command group through three workflows, in order:

  1. pause -> step -> observe — pause the sim, switch to manual (lockstep) mode,
     step exactly 5 fixed-dt frames, and assert frame advanced by exactly 5.
  2. lockstep crunch — manual mode + render OFF + fps 0 (uncapped) + step(count=N):
     advance a few hundred deterministic frames as fast as the host can run them,
     assert frame advanced by exactly N (D-10: reproducible fast runs use lockstep
     crunch, NOT dt-scale which is observation-only D-13).
  3. render-off fast test — assert render.info reports {enabled:false, draw_calls:0}
     (TIME-04), then restore: render ON + resume.

Usage: python tools/devapi/scenarios/time_render_demo.py [--port N]
  Port resolution: --port N  >  env NT_DEVAPI_PORT  >  default 17890.
Returns exit 0 if every assertion passes, exit 1 on any failure (connect /
timeout / assertion). Stdlib only (socket + json) — no pip deps.
"""
import os
import sys

# Allow running as a plain script by making the repo root importable, so the
# absolute `tools.devapi.*` imports resolve (mirrors engine_probe.py).
if __package__ in (None, ""):
    sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..")))
    from tools.devapi.client import DevApiClient, DevApiResultError
    from tools.devapi.transport import DEFAULT_PORT, DEFAULT_READ_TIMEOUT, SocketTransport
else:
    from ..client import DevApiClient, DevApiResultError
    from ..transport import DEFAULT_PORT, DEFAULT_READ_TIMEOUT, SocketTransport

# Lockstep-crunch frame budget — large enough to prove deterministic fast-forward,
# small enough to stay well under the host's per-wait safety cap.
CRUNCH_FRAMES = 300


def _parse_port(raw: str) -> int:
    """int() + range-check; clean exit(2) on garbage instead of a raw traceback."""
    try:
        port = int(raw)
    except ValueError:
        print("error: --port must be an integer 1..65535", file=sys.stderr)
        sys.exit(2)
    if not (1 <= port <= 65535):
        print("error: --port must be an integer 1..65535", file=sys.stderr)
        sys.exit(2)
    return port


def resolve_port(argv) -> int:
    """--port N (or positional) > env NT_DEVAPI_PORT > DEFAULT_PORT."""
    for i, a in enumerate(argv):
        if a == "--port" and i + 1 < len(argv):
            return _parse_port(argv[i + 1])
        if a.startswith("--port="):
            return _parse_port(a.split("=", 1)[1])
    env = os.environ.get("NT_DEVAPI_PORT")
    if env:
        return _parse_port(env)
    for a in argv:
        if a.isdigit():
            return _parse_port(a)
    return DEFAULT_PORT


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


def main(port: int) -> int:
    transport = None
    try:
        transport = SocketTransport("127.0.0.1", port, read_timeout=DEFAULT_READ_TIMEOUT)
        client = DevApiClient(transport)
        run(client)
        return 0
    except AssertionError as exc:
        print(f"FAIL: assertion: {exc}")
        return 1
    except (DevApiResultError, ValueError) as exc:
        print(f"FAIL: protocol error: {exc}")
        return 1
    except (ConnectionError, TimeoutError, OSError) as exc:
        print(f"FAIL: transport error talking to 127.0.0.1:{port}: {exc}")
        return 1
    finally:
        if transport is not None:
            transport.close()


if __name__ == "__main__":
    args = sys.argv[1:]
    if "--help" in args or "-h" in args:
        print(__doc__)
        sys.exit(0)
    sys.exit(main(resolve_port(args)))
