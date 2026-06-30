#!/usr/bin/env python3
"""DevAPI canonical demo — one run(client) scenario driven over both transports (native SocketTransport,
self-launched here; web PlaywrightTransport / the devapi.spec.ts gate), proving native<->web parity.

It exercises:
  * discovery        — ping + endpoints/command.describe/features (game.echo + game.poke, zero engine edits),
  * game.* round-trip — game.echo {msg} read-back + game.poke write flipping an observable host bool,
  * deferred capture — capture.frame/region must resolve to a REAL PNG (not {deferred:true}) on a
                       capture-capable host; the native host has no GL, so it returns capture_unavailable,
  * deferred items   — perf.stats mem_used + render.set_enabled + time.step (advertised commands MUST pass).

Usage: python tools/devapi/scenarios/devapi_demo.py [--port N] [--no-launch] [--host-bin PATH]
  Port resolution: --port N  >  env NT_DEVAPI_PORT  >  default 17890.
Exit 0 if every assertion passes, 1 on assertion / protocol / transport failure, 2 on usage / launch.

Stdlib only; the web driver is the TS spec + PlaywrightTransport for local runs.
"""
import os
import subprocess
import sys
import time

# Allow running as a plain script by making the repo root importable (mirrors capture_demo.py).
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
    """--host-bin PATH override, else the native devapi_host (per-preset subdir, then flat fallback)."""
    for i, a in enumerate(argv):
        if a == "--host-bin" and i + 1 < len(argv):
            return argv[i + 1]
        if a.startswith("--host-bin="):
            return a.split("=", 1)[1]
    exe = "devapi_host.exe" if os.name == "nt" else "devapi_host"
    base = os.path.join(REPO_ROOT, "build", "examples", "devapi_host")
    # Per-preset build nests under a preset subdir; fall back to the flat layout (--host-bin pins exactly).
    for cand in (os.path.join(base, "native-debug", exe), os.path.join(base, exe)):
        if os.path.isfile(cand):
            return cand
    return os.path.join(base, exe)


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


def _drive_deferred_capture(client: DevApiClient) -> None:
    """Deferred capture.frame/region — the drain race only a live transport exposes.

    A capture-capable host (web: pre-swap seam armed) MUST reply with a real PNG, never {deferred:true};
    the native host (no GL/seam) rejects synchronously with capture_unavailable (the documented outcome).
    """
    try:
        full = client.capture_frame()
    except DevApiResultError as exc:
        if "capture_unavailable" in str(exc):
            print("PASS capture: host drives no pre-swap seam -> capture_unavailable (native devapi_host, expected).")
            return
        raise
    # Capture-capable host (web): the deferred producer must have run.
    assert full.get("deferred") is not True, (
        "capture.frame returned {deferred:true} — the pre-swap producer never ran (web drain-race)"
    )
    assert full.get("format") == "png", f"capture.frame format is {full.get('format')!r}, expected 'png'"
    assert full.get("data"), "capture.frame returned an empty data payload"
    # web half: a sub-rect crop returns the same png contract.
    view = client.result("view")
    fb_w, fb_h = int(view["fb_width"]), int(view["fb_height"])
    region = client.capture_region(0, 0, fb_w * 3 // 4, fb_h * 3 // 4)
    assert region.get("deferred") is not True, "capture.region returned {deferred:true} (drain-race)"
    assert region.get("format") == "png" and region.get("data"), "capture.region payload is not a non-empty PNG"
    print(f"PASS capture: deferred capture.frame + capture.region yielded real PNGs ({full.get('width')}x{full.get('height')}), not {{deferred:true}}.")


def _drive_deferred_items(client: DevApiClient, advertised: set) -> None:
    """Exercise the web deferrals, advertised-aware: a command this host lists in endpoints MUST pass
    (swallowing its error would let a real obs/render/time break still print PASS); a command absent
    from the host's build is skipped. Items: perf.stats mem_used, a render toggle, time.step.
    """
    exercised = []
    # (1) obs memory read — perf.snapshot is last-frame and lacks mem_used; perf.stats carries it.
    if "perf.stats" in advertised:
        stats = client.result("perf.stats", {"channels": ["mem_used"]})
        mem = stats.get("channels", {}).get("mem_used")
        assert isinstance(mem, dict) and "samples" in mem, (
            f"perf.stats advertised but the requested mem_used channel is missing/malformed: {stats}"
        )
        exercised.append("perf.stats[mem_used]")
    # (2) render toggle — flip the host render flag off then back on.
    if "render.set_enabled" in advertised:
        client.render_set_enabled(False)
        client.render_set_enabled(True)
        exercised.append("render.set_enabled")
    # (3) time.step — a deterministic MANUAL advance.
    if "time.step" in advertised:
        client.step(1)
        exercised.append("time.step")
    print(f"PASS deferred-items: {', '.join(exercised) or '(none advertised)'} exercised (advertised commands asserted).")


def run(client: DevApiClient) -> None:
    """The ONE transport-agnostic scenario; raise AssertionError on any contract violation.

    Driven IDENTICALLY by SocketTransport (native) and PlaywrightTransport / devapi.spec.ts (web).
    """
    # 1. Discovery (probe half) — endpoints lists BOTH game commands (zero-engine-edit registration).
    client.result("ping")
    eps = client.result("endpoints")
    commands = eps.get("commands", [])
    methods = {c.get("method") for c in commands}
    assert "game.echo" in methods, "endpoints did not list game.echo"
    assert "game.poke" in methods, "endpoints did not list game.poke"
    desc = client.result("command.describe", {"method": "game.echo"})
    assert desc.get("params_shape") and desc.get("result_shape"), "command.describe missing shapes for game.echo"
    features = client.result("features")
    assert "game" in features.get("groups", []), "'game' group not active in features"

    # 2. game.* round-trip — echo read-back (request_id correlated) + poke write flips host state.
    echoed = client.request("game.echo", {"msg": "hi"})
    assert echoed.get("ok") is True, f"game.echo not ok: {echoed}"
    assert echoed.get("result", {}).get("msg") == "hi", f"game.echo result mismatch: {echoed}"
    assert "request_id" in echoed, "game.echo response missing request_id (correlation broken)"
    poked = client.result("game.poke")
    assert "poked" in poked, f"game.poke missing the observable 'poked' field: {poked}"
    before = bool(poked["poked"])
    poked2 = client.result("game.poke")
    assert bool(poked2["poked"]) != before, "game.poke did not flip the observable host bool"
    print("PASS discovery + game.echo/game.poke: endpoints list both, echo round-trips, poke flips host state.")

    # 3. Deterministic pump + the riskiest web path: a deferred capture -> real PNG.
    client.set_mode("manual")
    _drive_deferred_capture(client)

    # 4. deferred items — advertised-aware: a command this host lists in endpoints MUST pass.
    _drive_deferred_items(client, methods)

    print("PASS: canonical run(client) — discovery + game.* + deferred capture + deferred items all machine-checked.")


def main(argv) -> int:
    no_launch = "--no-launch" in argv
    port = resolve_port(argv)
    proc = None
    transport = None
    try:
        if not no_launch:
            host_bin = _find_host_bin(argv)
            if not os.path.isfile(host_bin):
                print(f"error: devapi_host not found at {host_bin}\n"
                      f"  build it: cmake --build build/_cmake/native-debug --target devapi_host\n"
                      f"  or attach to a running host with --no-launch --port N", file=sys.stderr)
                return 2
            env = dict(os.environ, NT_DEVAPI_PORT=str(port))
            proc = subprocess.Popen([host_bin], cwd=REPO_ROOT, env=env)
            transport = _connect_with_retry(port)
        else:
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
