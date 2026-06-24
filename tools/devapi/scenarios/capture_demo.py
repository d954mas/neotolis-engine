#!/usr/bin/env python3
"""DevAPI capture demo — the live-socket UAT for the frame-capture group (CAP-01..04).

Exercises capture.frame / capture.region end-to-end against a REAL running examples/capture_host over
loopback TCP — the layer only a live socket + a real GL context proves: capture is the FIRST deferred
command that returns DATA, so the drain-race (Pitfall 4 / Phase-66 D-12; MEMORY
devapi-blocking-cmds-must-defer) only shows here, never in a unit binary. The host renders a known
two-tone pattern; this UAT confirms the deferred command resolves AFTER a render with a decodable,
non-blank PNG (NOT {deferred:true}), region/scale dims are correct, bad params return bad_params over
the wire, and the pixel-health check (decode + dims + not-blank, D-09) passes.

devapi_host inits NO gfx (Pitfall 2), so this scenario launches CAPTURE_HOST (a dedicated gfx-backed
host). By default it launches its own subprocess on a free port, drives the capture commands, and tears
it down. Point it at an already-running host with --port N (or env NT_DEVAPI_PORT) + --no-launch.

Usage: python tools/devapi/scenarios/capture_demo.py [--port N] [--no-launch] [--host-bin PATH] [--save DIR]
  Port resolution: --port N  >  env NT_DEVAPI_PORT  >  default 17890.
Returns exit 0 if every assertion passes, exit 1 on any failure (connect / timeout / assertion /
protocol), exit 2 on a usage / launch error.

Harness-core stays stdlib-only (socket + json + subprocess); Pillow + numpy are imported LAZILY inside
the capture branch via tools/devapi/pixel_health.py (D-10) — a non-capture run never pulls them.
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
    from tools.devapi.transport import DEFAULT_PORT, DEFAULT_READ_TIMEOUT, SocketTransport
else:
    from ..client import DevApiClient, DevApiResultError
    from ..transport import DEFAULT_PORT, DEFAULT_READ_TIMEOUT, SocketTransport

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


def _find_host_bin(argv) -> str:
    """--host-bin PATH override, else the per-preset capture_host built by the capture config.

    capture_host is a CAPTURE-focused host (CORE/DISCOVERY/TIME/CAPTURE groups only), built into a
    dedicated config dir — the shared native-debug build enables every devapi group, which would couple
    the host to subsystems (nt_ui / metrics) it never inits. The cap-host preset dir is the default.
    """
    for i, a in enumerate(argv):
        if a == "--host-bin" and i + 1 < len(argv):
            return argv[i + 1]
        if a.startswith("--host-bin="):
            return a.split("=", 1)[1]
    exe = "capture_host.exe" if os.name == "nt" else "capture_host"
    return os.path.join(REPO_ROOT, "build", "examples", "capture_host", "cap-host", exe)


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
            return SocketTransport("127.0.0.1", port, read_timeout=DEFAULT_READ_TIMEOUT)
        except (ConnectionError, TimeoutError, OSError) as exc:
            last_exc = exc
            time.sleep(0.1)
    raise ConnectionError(f"could not connect to 127.0.0.1:{port} within {deadline_s}s: {last_exc}")


def run(client: DevApiClient, save_dir=None) -> None:
    """Drive every capture surface; raise AssertionError on any contract violation.

    pixel_health (Pillow + numpy) is imported HERE, lazily — the harness core never pulls it (D-10).
    """
    from tools.devapi import pixel_health  # capture-scoped import (D-10): lazy, inside the capture branch.

    # Read the live framebuffer dims so expected sizes are derived, never pinned.
    view = client.result("view")
    fb_w = int(view["fb_width"])
    fb_h = int(view["fb_height"])
    assert fb_w > 0 and fb_h > 0, f"view returned a zero-size framebuffer ({fb_w}x{fb_h})"

    # 1. Drain-race: capture.frame is DEFERRED, so the reply must arrive only AFTER a render carrying a
    #    real PNG payload — NOT the legacy {deferred:true}. result() blocks through the deferred yield
    #    via request_id correlation; receiving width/height/format/data proves the pre-swap producer ran.
    full = client.capture_frame()
    assert full.get("deferred") is not True, "capture.frame returned {deferred:true} — the producer never ran (drain-race)"
    img = pixel_health.check_payload(full, fb_w, fb_h)
    if save_dir:
        os.makedirs(save_dir, exist_ok=True)
        img.save(os.path.join(save_dir, "capture_frame.png"))
    print(f"PASS[1/5] capture.frame: deferred-yield carried a decodable {img.width}x{img.height} PNG, not {{deferred:true}}.")

    # 2. capture.region: a sub-rect returns dims matching the rect (no resampling at scale 1). The rect
    #    is ASYMMETRIC (rx!=ry implied by rw!=rh on a non-square fb) and straddles the host's two-tone
    #    boundary (the centered fg box at fb/4..3fb/4) so the not-blank check has >=2 colors under Mesa
    #    llvmpipe — AND a wrong region y-origin (top-left vs bottom-left) would change the captured
    #    content, so this rect also exercises the region Y-flip contract.
    rx, ry, rw, rh = 0, 0, fb_w * 3 // 4, fb_h * 3 // 4
    region = client.capture_region(rx, ry, rw, rh)
    region_img = pixel_health.check_payload(region, rw, rh)
    if save_dir:
        region_img.save(os.path.join(save_dir, "capture_region.png"))
    print(f"PASS[2/5] capture.region {{x:{rx},y:{ry},w:{rw},h:{rh}}}: PNG dims match the rect ({rw}x{rh}), not-blank.")

    # 3. scale 1/2 and 1/4 -> half / quarter dims (box-average downscale).
    half = client.capture_frame(scale=2)
    pixel_health.check_payload(half, fb_w // 2, fb_h // 2)
    quarter = client.capture_frame(scale=4)
    pixel_health.check_payload(quarter, fb_w // 4, fb_h // 4)
    print(f"PASS[3/5] capture.frame scale 1/2 -> {fb_w // 2}x{fb_h // 2}, 1/4 -> {fb_w // 4}x{fb_h // 4}.")

    # 4. Bad params -> bad_params over the wire (never an assert / crash on untrusted bot input). Carry
    #    the wire method EXPLICITLY in the tuple (not inferred from the label). Both axes are proven
    #    OOB over the real framebuffer: w>fb_w (X) and h>fb_h (Y) (AGENTS.md axis-swap guidance).
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

    # 5. Pixel-health summary: the full-frame image already passed decode + dims + not-blank above.
    print("PASS[5/5] pixel-health: decode + dims + not-blank held on every captured frame (D-09).")

    print("PASS: capture.* end-to-end over the socket — drain-race + region + scale + bad_params + pixel-health all machine-asserted.")


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
                print(f"error: capture_host not found at {host_bin}\n"
                      f"  build it: cmake -B build/_cmake/cap-host -DNT_DEVAPI_ENABLED=ON "
                      f"-DNT_DEVAPI_GROUP_CORE=ON -DNT_DEVAPI_GROUP_DISCOVERY=ON -DNT_DEVAPI_GROUP_TIME=ON "
                      f"-DNT_DEVAPI_GROUP_CAPTURE=ON -G Ninja -DCMAKE_C_COMPILER=clang\n"
                      f"  then: cmake --build build/_cmake/cap-host --target capture_host\n"
                      f"  or attach to a running host with --no-launch --port N", file=sys.stderr)
                return 2
            env = dict(os.environ, NT_DEVAPI_PORT=str(port))
            proc = subprocess.Popen([host_bin], cwd=REPO_ROOT, env=env)
            transport = _connect_with_retry(port)
        else:
            transport = SocketTransport("127.0.0.1", port, read_timeout=DEFAULT_READ_TIMEOUT)
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
