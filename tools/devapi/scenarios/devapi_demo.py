#!/usr/bin/env python3
"""DevAPI canonical demo — one run(client) scenario driven over both transports (native SocketTransport,
web PlaywrightTransport), proving native<->web parity.

It exercises:
  * discovery        — ping + endpoints/command.describe (game.echo + game.poke, zero engine edits),
  * game.* round-trip — game.echo {msg} read-back + game.poke write flipping an observable host bool,
  * advertised items — perf.stats mem_used + render.set_enabled (immediate) + time.step (deferred),
  * native only      — the newest client takes over the socket.

Usage: python -m tools.devapi.scenarios.devapi_demo [--target native|web] (options: tools/devapi/launch.py)
Exit 0 pass, 1 failure, 2 usage / launch (tools/devapi/launch.py).
"""
import json
import socket
import sys

from .. import launch
from ..client import DevApiClient
from ..transport import SocketTransport


def _drive_advertised_items(client: DevApiClient, advertised: set) -> None:
    """Advertised-aware: a command this host lists in endpoints MUST pass (swallowing its error would let a
    real obs/render/time break still print PASS); a command absent from the host's build is skipped.
    Items: perf.stats mem_used, a render toggle, time.step.
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
    print(f"PASS advertised items: {', '.join(exercised) or '(none advertised)'} exercised.")


def _raw_ping(port: int) -> None:
    # A bounded raw socket, so a host that parks or rejects the newcomer fails here instead of hanging.
    with socket.create_connection(("127.0.0.1", port), timeout=10) as s:
        s.sendall(b'{"method":"ping","request_id":1}\n')
        reply = json.loads(s.makefile("rb").readline())
        assert reply.get("ok") is True, f"the newest client was not served: {reply}"


def _check_newest_client_wins(client: DevApiClient) -> None:
    """The native host serves the newest connection: a second client is served and the first is dropped;
    a client that reconnects at once is served even before its old close is read."""
    port = client.transport.port
    _raw_ping(port)
    try:
        client.result("ping")
    except ConnectionError:
        pass
    else:
        raise AssertionError("the replaced client was still served")
    for _ in range(30):
        _raw_ping(port)
    print("PASS newest client wins: a second client takes over and 30 immediate reconnects are served.")


def run(client: DevApiClient) -> None:
    """The ONE transport-agnostic scenario; raise AssertionError on any contract violation.

    Driven IDENTICALLY by SocketTransport (native) and PlaywrightTransport (web); the takeover check, last,
    is socket-only.
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

    # 3. advertised items (time.step needs MANUAL); capture has its own UAT, capture_demo.py.
    client.set_mode("manual")
    _drive_advertised_items(client, methods)

    # 4. Last: it drops this client.
    if isinstance(client.transport, SocketTransport):
        _check_newest_client_wins(client)


if __name__ == "__main__":
    sys.exit(launch.main(run))
