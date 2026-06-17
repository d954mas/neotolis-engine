#!/usr/bin/env python3
"""DevAPI engine-probe — native transport proof.

Connects to a running examples/devapi_host over loopback TCP, discovers the
self-describing surface (endpoints / command.describe / features), drives the
game-registered game.echo command end-to-end, and asserts correct request_id
correlation. This proves the WHOLE native transport chain over a real socket.

Input/UI probe coverage will extend this as further commands are registered.

Usage: python tools/devapi/engine_probe.py [--port N]
  Port resolution: --port N  >  env NT_DEVAPI_PORT  >  default 17890.
Returns exit 0 if every assertion passes, exit 1 on any failure (connect /
timeout / assertion). Stdlib only (socket + json) — no pip deps.
"""
import os
import sys

# Allow running as a plain script (python tools/devapi/engine_probe.py) by making
# the repo root importable, so `from tools.devapi import ...` resolves.
if __package__ in (None, ""):
    sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
    from tools.devapi.client import DevApiClient
    from tools.devapi.transport import DEFAULT_PORT, DEFAULT_READ_TIMEOUT, SocketTransport
else:
    from .client import DevApiClient
    from .transport import DEFAULT_PORT, DEFAULT_READ_TIMEOUT, SocketTransport


def resolve_port(argv) -> int:
    """--port N (or positional) > env NT_DEVAPI_PORT > DEFAULT_PORT."""
    for i, a in enumerate(argv):
        if a == "--port" and i + 1 < len(argv):
            return int(argv[i + 1])
        if a.startswith("--port="):
            return int(a.split("=", 1)[1])
    env = os.environ.get("NT_DEVAPI_PORT")
    if env:
        return int(env)
    # Lone positional integer is also accepted.
    for a in argv:
        if a.isdigit():
            return int(a)
    return DEFAULT_PORT


def run(client: DevApiClient) -> None:
    """Drive the probe steps; raise AssertionError on any contract violation."""
    # 1. Sanity ping (engine.info/ping exist in the core group).
    client.result("ping")

    # 2. endpoints lists game.echo in group "game".
    endpoints = client.result("endpoints")
    commands = endpoints.get("commands", [])
    echo = next((c for c in commands if c.get("method") == "game.echo"), None)
    assert echo is not None, "endpoints did not list game.echo"
    assert echo.get("group") == "game", f"game.echo group is {echo.get('group')!r}, expected 'game'"

    # 3. command.describe game.echo returns a non-empty params_shape + result_shape.
    desc = client.result("command.describe", {"method": "game.echo"})
    assert desc.get("params_shape"), "command.describe missing params_shape"
    assert desc.get("result_shape"), "command.describe missing result_shape"

    # 4. features lists the "game" group as active.
    features = client.result("features")
    groups = features.get("groups", [])
    assert "game" in groups, f"'game' group not active in features: {groups}"

    # 5. game.echo round-trip — the transport-proof: result echoed AND request_id correlated.
    resp = client.request("game.echo", {"msg": "hello"})
    assert resp.get("ok") is True, f"game.echo not ok: {resp}"
    assert resp.get("result", {}).get("msg") == "hello", f"game.echo result mismatch: {resp}"
    assert "request_id" in resp, "game.echo response missing request_id (correlation broken)"

    print(
        "PASS: ping ok, endpoints+command.describe+features discovered, "
        "game.echo round-trip msg='hello' with request_id={} correlated".format(resp["request_id"])
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
