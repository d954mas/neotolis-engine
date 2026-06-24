#!/usr/bin/env python3
"""DevAPI entity.set demo — the live-socket UAT for the component WRITE group.

Exercises entity.set end-to-end against a REAL running examples/devapi_host over loopback TCP — the
layer only a live socket proves: the host seeds entities with transform/drawable, a bot reads their
named-group shape, WRITES fields through the component apply() hooks (which maintain the engine
invariants: transform dirty, drawable packed mirror, quaternion normalize), and reads the result back.
Also covers the read named-group shape over the wire (the host previously had 0 entities, so that path
was never socket-tested) and every bad_params path + batch whole-or-nothing atomicity.

By default this script LAUNCHES its own devapi_host subprocess on a free port, drives entity.set, and
tears it down. Point it at an already-running host with --port N --no-launch.

Usage: python tools/devapi/scenarios/write_demo.py [--port N] [--no-launch] [--host-bin PATH]
Returns exit 0 if every assertion passes, exit 1 on any failure, exit 2 on a usage/launch error.
Stdlib only (socket + json + subprocess) — no pip deps.
"""
import os
import subprocess
import sys
import time

if __package__ in (None, ""):
    sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..")))
    from tools.devapi.client import DevApiClient, DevApiResultError
    from tools.devapi.transport import DEFAULT_PORT, DEFAULT_READ_TIMEOUT, SocketTransport
else:
    from ..client import DevApiClient, DevApiResultError
    from ..transport import DEFAULT_PORT, DEFAULT_READ_TIMEOUT, SocketTransport

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


def _parse_port(raw: str) -> int:
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
    for i, a in enumerate(argv):
        if a == "--host-bin" and i + 1 < len(argv):
            return argv[i + 1]
        if a.startswith("--host-bin="):
            return a.split("=", 1)[1]
    exe = "devapi_host.exe" if os.name == "nt" else "devapi_host"
    return os.path.join(REPO_ROOT, "build", "examples", "devapi_host", "native-debug", exe)


def _connect_with_retry(port: int, deadline_s: float = 10.0) -> SocketTransport:
    end = time.time() + deadline_s
    last_exc = None
    while time.time() < end:
        try:
            return SocketTransport("127.0.0.1", port, read_timeout=DEFAULT_READ_TIMEOUT)
        except (ConnectionError, TimeoutError, OSError) as exc:
            last_exc = exc
            time.sleep(0.1)
    raise ConnectionError(f"could not connect to 127.0.0.1:{port} within {deadline_s}s: {last_exc}")


def _approx(a, b, tol=0.001) -> bool:
    """element-wise tolerant compare for a scalar or a list of numbers."""
    if isinstance(a, list):
        return isinstance(b, list) and len(a) == len(b) and all(abs(x - y) <= tol for x, y in zip(a, b))
    return abs(a - b) <= tol


def _find_entity_with(client: DevApiClient, component: str) -> dict:
    res = client.result("entity.list", {"limit": 64})
    for e in res["entities"]:
        if component in e:
            return e
    raise AssertionError(f"no seeded entity carries a {component!r} component (got {res})")


def _read_entity(client: DevApiClient, eid: int) -> dict:
    res = client.result("entity.list", {"limit": 64})
    for e in res["entities"]:
        if e["id"] == eid:
            return e
    raise AssertionError(f"entity {eid} vanished from entity.list")


def check_read_shape(client: DevApiClient) -> None:
    """1. entity.list emits each present component as a NAMED GROUP (only a live socket with seeded
    entities proves this end to end)."""
    e = _find_entity_with(client, "transform")
    for k in ("id", "index", "generation", "enabled"):
        assert k in e, f"entity entry missing core field {k!r} (got {sorted(e)})"
    tr = e["transform"]
    assert isinstance(tr, dict), f"transform group is {tr!r}, expected an object"
    for k in ("position", "rotation", "scale"):
        assert isinstance(tr.get(k), list), f"transform.{k} is {tr.get(k)!r}, expected a number array"
    assert len(tr["position"]) == 3 and len(tr["rotation"]) == 4, "transform arity wrong"
    print("PASS[1/6] entity.list named-group shape over the wire: transform{position,rotation,scale}.")


def check_write_position(client: DevApiClient) -> None:
    """2. entity.set a single vec3 field; read it back."""
    eid = _find_entity_with(client, "transform")["id"]
    res = client.result("entity.set", {"id": eid, "component": "transform", "field": "position", "value": [7, 8, 9]})
    assert res["component"] == "transform", f"result.component is {res.get('component')!r}"
    assert res["fields"] == ["position"], f"result.fields is {res.get('fields')!r}, expected ['position']"
    pos = _read_entity(client, eid)["transform"]["position"]
    assert _approx(pos, [7, 8, 9]), f"position read back as {pos}, expected ~[7,8,9]"
    print("PASS[2/6] entity.set position landed + read back over the wire.")


def check_write_rotation_normalized(client: DevApiClient) -> None:
    """3. a non-unit quaternion is normalized by the setter — the readback proves it over the wire."""
    eid = _find_entity_with(client, "transform")["id"]
    client.result("entity.set", {"id": eid, "component": "transform", "field": "rotation", "value": [0, 0, 0, 2]})
    q = _read_entity(client, eid)["transform"]["rotation"]
    assert _approx(q, [0, 0, 0, 1]), f"rotation read back as {q}, expected normalized ~[0,0,0,1]"
    print("PASS[3/6] entity.set rotation normalized over the wire ([0,0,0,2] -> [0,0,0,1]).")


def check_write_color_visible(client: DevApiClient) -> None:
    """4. drawable color (in-range) + visible bool land through set_color / set_visible."""
    eid = _find_entity_with(client, "drawable")["id"]
    client.result("entity.set", {"id": eid, "component": "drawable", "field": "color", "value": [0.2, 0.4, 0.6, 1]})
    client.result("entity.set", {"id": eid, "component": "drawable", "field": "visible", "value": False})
    dr = _read_entity(client, eid)["drawable"]
    assert _approx(dr["color"], [0.2, 0.4, 0.6, 1]), f"color read back as {dr['color']}"
    assert dr["visible"] is False, f"visible read back as {dr['visible']!r}, expected False"
    print("PASS[4/6] entity.set color + visible landed over the wire.")


def check_batch(client: DevApiClient) -> None:
    """5. a whole-or-nothing batch sets two fields of one component atomically."""
    eid = _find_entity_with(client, "transform")["id"]
    res = client.result("entity.set", {"id": eid, "component": "transform", "fields": {"position": [1, 1, 1], "scale": [3, 3, 3]}})
    assert set(res["fields"]) == {"position", "scale"}, f"batch result.fields is {res.get('fields')!r}"
    tr = _read_entity(client, eid)["transform"]
    assert _approx(tr["position"], [1, 1, 1]) and _approx(tr["scale"], [3, 3, 3]), f"batch did not land: {tr}"
    print("PASS[5/6] entity.set batch applied position + scale atomically.")


def check_bad_paths(client: DevApiClient) -> None:
    """6. every rejection is bad_params over the wire; batch atomicity leaves the entity untouched."""
    tid = _find_entity_with(client, "transform")["id"]
    did = _find_entity_with(client, "drawable")["id"]

    def expect_bad(params, what):
        try:
            client.result("entity.set", params)
            raise AssertionError(f"entity.set({what}) unexpectedly succeeded; expected bad_params")
        except DevApiResultError as exc:
            assert "bad_params" in str(exc), f"entity.set({what}) raised {exc!r}, expected bad_params"

    expect_bad({"id": tid, "component": "nope", "field": "position", "value": [1, 2, 3]}, "unknown component")
    expect_bad({"id": tid, "component": "transform", "field": "world_matrix", "value": [1, 2, 3]}, "read-only world_matrix")
    expect_bad({"id": tid, "component": "transform", "field": "rotation", "value": [0, 0, 0, 0]}, "degenerate quaternion")
    expect_bad({"id": tid, "component": "transform", "field": "position", "value": [1, 2, 3, 4]}, "arity mismatch")
    expect_bad({"id": 0, "component": "transform", "field": "position", "value": [1, 2, 3]}, "dead/stale handle")
    expect_bad({"id": did, "component": "drawable", "field": "color", "value": [2, 0, 0, 1]}, "color out of [0,1]")

    # Batch whole-or-nothing: position valid, rotation degenerate -> whole batch rejected, position unchanged.
    before = _read_entity(client, tid)["transform"]["position"]
    expect_bad({"id": tid, "component": "transform", "fields": {"position": [42, 42, 42], "rotation": [0, 0, 0, 0]}}, "batch one-bad")
    after = _read_entity(client, tid)["transform"]["position"]
    assert _approx(after, before), f"batch atomicity violated: position moved {before} -> {after} despite reject"
    print("PASS[6/6] entity.set bad paths + batch whole-or-nothing atomicity all hold over the wire.")


def run(client: DevApiClient) -> None:
    check_read_shape(client)
    check_write_position(client)
    check_write_rotation_normalized(client)
    check_write_color_visible(client)
    check_batch(client)
    check_bad_paths(client)
    print("PASS: entity.set end-to-end over the socket — reads, writes through apply() (dirty/repack/normalize), and every bad_params + atomicity path machine-asserted.")


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
