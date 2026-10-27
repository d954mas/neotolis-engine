#!/usr/bin/env python3
"""DevAPI capture demo — the live-socket UAT for the frame-capture group.

Exercises capture.frame / capture.region end-to-end against a real running examples/devapi_host over
loopback TCP. Capture is deferred to the pre-swap seam, which only a live socket + real GL context drives.
The host clears a known two-tone pattern; with the sim frozen in MANUAL this UAT checks the PNG decodes,
the pattern's orientation and channel order, region == the same crop of the full frame, scale dims,
bad_params over the wire, and that capturing never advances the frame.

By default it launches its own host subprocess and tears it down; attach to a running host with --no-launch.

Usage: python tools/devapi/scenarios/capture_demo.py [--port N] [--no-launch] [--host-bin PATH] [--save DIR]
  Port resolution: --port N  >  env NT_DEVAPI_PORT  >  default 17890.
Exit 0 if every assertion passes, 1 on any failure (connect / assertion / protocol),
2 on a usage / launch error.

Pillow + numpy are imported lazily inside the capture branch via tools/devapi/pixel_health.py — the
harness core stays stdlib-only.
"""
import os
import subprocess
import sys
import time

# Allow running as a plain script by making the repo root importable, so the
# absolute `tools.devapi.*` imports resolve (mirrors obs_demo.py).
if __package__ in (None, ""):
    sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..")))
    from tools.devapi.client import DevApiClient, DevApiResultError
    from tools.devapi.scenarios.devapi_demo import _find_host_bin
    from tools.devapi.transport import DEFAULT_PORT, SocketTransport
else:
    from ..client import DevApiClient, DevApiResultError
    from ..transport import DEFAULT_PORT, SocketTransport
    from .devapi_demo import _find_host_bin

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


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


def _save_dir(argv):
    """--save DIR -> save decoded PNGs there for human viewing (optional, tooling-side write only)."""
    for i, a in enumerate(argv):
        if a == "--save" and i + 1 < len(argv):
            return argv[i + 1]
        if a.startswith("--save="):
            return a.split("=", 1)[1]
    return None


def _connect_with_retry(port: int, deadline_s: float = 10.0) -> SocketTransport:
    """Poll connect() until the freshly-launched host's listen socket is up (or time out)."""
    end = time.time() + deadline_s
    last_exc = None
    while time.time() < end:
        try:
            return SocketTransport("127.0.0.1", port)
        except (ConnectionError, TimeoutError, OSError) as exc:
            last_exc = exc
            time.sleep(0.1)
    raise ConnectionError(f"could not connect to 127.0.0.1:{port} within {deadline_s}s: {last_exc}")


def run(client: DevApiClient, save_dir=None) -> None:
    """Drive every capture surface; raise AssertionError on any contract violation.

    pixel_health (Pillow + numpy) is imported HERE, lazily — the harness core never pulls it.
    """
    from tools.devapi import pixel_health  # capture-scoped import: lazy, inside the capture branch.

    # Read the live framebuffer dims so expected sizes are derived, never pinned.
    view = client.result("view")
    fb_w = int(view["fb_width"])
    fb_h = int(view["fb_height"])
    assert fb_w > 0 and fb_h > 0, f"view returned a zero-size framebuffer ({fb_w}x{fb_h})"

    # MANUAL with no step queued: the sim stays frozen, so every capture reads the same frame.
    client.set_mode("manual")
    frame_before = client.frame_current()["frame"]

    # 1. capture.frame resolves at the next present; probe the pattern (orange box at top-left x 1/8..5/8,
    #    y 1/8..5/8 on a blue background) so a Y flip, X mirror or R/B swap fails.
    img = pixel_health.check_payload(client.capture_frame(), fb_w, fb_h).convert("RGB")
    fg = img.getpixel((fb_w // 4, fb_h // 4))
    below = img.getpixel((fb_w // 4, fb_h * 3 // 4))
    right = img.getpixel((fb_w * 3 // 4, fb_h // 4))
    assert fg[0] > fg[2], f"capture.frame: box pixel {fg} is not red-dominant orange (R/B swap or Y flip?)"
    assert below[2] > below[0], f"capture.frame: pixel below the box {below} is not the blue background (Y flip?)"
    assert right[2] > right[0], f"capture.frame: pixel right of the box {right} is not the blue background (X mirror?)"
    if save_dir:
        os.makedirs(save_dir, exist_ok=True)
        img.save(os.path.join(save_dir, "capture_frame.png"))
    print(f"PASS[1/5] capture.frame: {fb_w}x{fb_h} PNG, pattern orientation and channel order hold.")

    # 2. capture.region equals the same crop of the full frame. A non-zero y catches a wrong gl_y conversion.
    rx, ry, rw, rh = fb_w // 4, fb_h // 16, fb_w // 2, fb_h // 2
    region = pixel_health.check_payload(client.capture_region(rx, ry, rw, rh), rw, rh).convert("RGB")
    assert region.tobytes() == img.crop((rx, ry, rx + rw, ry + rh)).tobytes(), (
        f"capture.region {{x:{rx},y:{ry},w:{rw},h:{rh}}} differs from the same crop of capture.frame"
    )
    if save_dir:
        region.save(os.path.join(save_dir, "capture_region.png"))
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

    print("PASS: capture.* end-to-end over the socket — deferred capture + region + scale + bad_params + pixel-health all machine-asserted.")


def main(argv) -> int:
    no_launch = "--no-launch" in argv
    port = resolve_port(argv)
    save_dir = _save_dir(argv)
    proc = None
    transport = None
    try:
        if not no_launch:
            host_bin = _find_host_bin(argv)
            if not os.path.isfile(host_bin):
                print(f"error: devapi_host not found at {host_bin}\n"
                      f"  build it with the devapi-smoke configure line in .github/workflows/ci.yml,\n"
                      f"  or attach to a running host with --no-launch --port N", file=sys.stderr)
                return 2
            env = dict(os.environ, NT_DEVAPI_PORT=str(port))
            proc = subprocess.Popen([host_bin], cwd=REPO_ROOT, env=env)
            transport = _connect_with_retry(port)
        else:
            transport = SocketTransport("127.0.0.1", port)
        client = DevApiClient(transport)
        run(client, save_dir)
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
        if proc is not None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()


if __name__ == "__main__":
    args = sys.argv[1:]
    if "--help" in args or "-h" in args:
        print(__doc__)
        sys.exit(0)
    sys.exit(main(args))
