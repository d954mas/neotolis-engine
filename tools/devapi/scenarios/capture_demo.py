#!/usr/bin/env python3
"""DevAPI capture demo — the live-socket UAT for the frame-capture group.

Exercises capture.frame / capture.region end-to-end against a real running examples/capture_host over
loopback TCP. Capture is a deferred DATA command, so the drain-race (the reply must arrive only after a
render fills the payload) only surfaces with a live socket + real GL context, never in a unit binary.
The host renders a known two-tone pattern; this UAT confirms the command resolves after a render with a
decodable, non-blank PNG (not {deferred:true}), region/scale dims are correct, bad params return
bad_params over the wire, and the pixel-health check passes.

devapi_host inits no gfx, so this scenario launches capture_host (a dedicated gfx-backed host). By
default it launches its own subprocess on a free port and tears it down; attach to an already-running
host with --no-launch.

Usage: python tools/devapi/scenarios/capture_demo.py [--port N] [--no-launch] [--host-bin PATH] [--save DIR]
  Port resolution: --port N  >  env NT_DEVAPI_PORT  >  default 17890.
Exit 0 if every assertion passes, 1 on any failure (connect / timeout / assertion / protocol),
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

    capture_host enables only CORE/DISCOVERY/TIME/CAPTURE groups, built into a dedicated cap-host dir —
    the shared native-debug build enables every group, coupling the host to subsystems it never inits.
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

    pixel_health (Pillow + numpy) is imported HERE, lazily — the harness core never pulls it.
    """
    from tools.devapi import pixel_health  # capture-scoped import: lazy, inside the capture branch.

    # Read the live framebuffer dims so expected sizes are derived, never pinned.
    view = client.result("view")
    fb_w = int(view["fb_width"])
    fb_h = int(view["fb_height"])
    assert fb_w > 0 and fb_h > 0, f"view returned a zero-size framebuffer ({fb_w}x{fb_h})"

    # 1. Drain-race: capture.frame is deferred, so the reply must arrive only after a render fills the
    #    payload — not the legacy {deferred:true}. Receiving width/height/format/data proves it ran.
    full = client.capture_frame()
    assert full.get("deferred") is not True, "capture.frame returned {deferred:true} — the producer never ran (drain-race)"
    img = pixel_health.check_payload(full, fb_w, fb_h)
    # Channel order (RGB vs BGR): the centered fg box is R-dominant orange; a red/blue swap would invert
    # it and slip past every channel-symmetric check (not-blank, split-means).
    r_mean, _g_mean, b_mean = pixel_health.center_channel_means(img)
    assert r_mean > b_mean, (
        f"capture channel order looks swapped (RGB vs BGR): center-fg mean R={r_mean:.1f} <= B={b_mean:.1f} "
        "(the orange foreground must be red-dominant)"
    )
    if save_dir:
        os.makedirs(save_dir, exist_ok=True)
        img.save(os.path.join(save_dir, "capture_frame.png"))
    print(f"PASS[1/5] capture.frame: deferred-yield carried a decodable {img.width}x{img.height} PNG, not {{deferred:true}}.")

    # 2. capture.region: a sub-rect returns dims matching the rect. The dims + not-blank check is
    #    position-blind, so the region origin is verified separately below (a flip passes dims+not-blank).
    rx, ry, rw, rh = 0, 0, fb_w * 3 // 4, fb_h * 3 // 4
    region = client.capture_region(rx, ry, rw, rh)
    region_img = pixel_health.check_payload(region, rw, rh)
    # Region Y-origin: under top-left origin the orange fg sits in the lower band of this crop (top
    # fb_h/4 rows are pure bg), so bottom must out-mean top — a bottom-left readback inverts this.
    top_mean, bottom_mean = pixel_health.vertical_split_means(region_img)
    assert bottom_mean > top_mean, (
        f"capture.region y-origin looks flipped: top-half mean {top_mean:.1f} >= bottom-half "
        f"{bottom_mean:.1f} (the orange foreground must sit in the lower band under top-left origin)"
    )
    # Region X-origin: a rect straddling the fg box's right edge — left half inside the fg, right half in
    # the bg, so left must out-mean right. The stub-backed C tests discard x/y, so this is the only
    # end-to-end x-origin guard.
    xr, xy, xw, xh = fb_w // 2, fb_h // 4, fb_w // 2, fb_h // 2
    xspan_img = pixel_health.check_payload(client.capture_region(xr, xy, xw, xh), xw, xh)
    left_mean, right_mean = pixel_health.horizontal_split_means(xspan_img)
    assert left_mean > right_mean, (
        f"capture.region x-origin looks shifted: left-half mean {left_mean:.1f} <= right-half "
        f"{right_mean:.1f} (the fg box's left portion must dominate a rect straddling its right edge)"
    )
    if save_dir:
        region_img.save(os.path.join(save_dir, "capture_region.png"))
    print(f"PASS[2/5] capture.region {{x:{rx},y:{ry},w:{rw},h:{rh}}}: {rw}x{rh}, not-blank + Y-origin verified (fg in lower band).")

    # 3. scale 1/2 and 1/4 -> half / quarter dims (box-average downscale).
    half = client.capture_frame(scale=2)
    pixel_health.check_payload(half, fb_w // 2, fb_h // 2)
    quarter = client.capture_frame(scale=4)
    pixel_health.check_payload(quarter, fb_w // 4, fb_h // 4)
    print(f"PASS[3/5] capture.frame scale 1/2 -> {fb_w // 2}x{fb_h // 2}, 1/4 -> {fb_w // 4}x{fb_h // 4}.")

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

    # 5. Pixel-health summary: the full-frame image already passed decode + dims + not-blank above.
    print("PASS[5/5] pixel-health: decode + dims + not-blank held on every captured frame.")

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
