#!/usr/bin/env python3
"""DevAPI entity.set demo — the live-host UAT for the component WRITE group.

Exercises entity.set end-to-end against a REAL running examples/devapi_host over its devapi transport (native or web) — the
layer only a live host proves: the host seeds entities with transform/drawable, a bot reads their
named-group shape, WRITES fields through the component apply() hooks (which maintain the engine
invariants: transform dirty, drawable color packing, quaternion normalize), and reads the result back.
Also covers the read named-group shape over the wire (the host previously had 0 entities, so that path
was never tested live) and every bad_params path + batch whole-or-nothing atomicity.

Usage: python -m tools.devapi.scenarios.write_demo [--target native|web] (options: tools/devapi/launch.py)
Exit 0 pass, 1 failure, 2 usage / launch (tools/devapi/launch.py).
"""
import sys

from .. import launch
from ..client import DevApiClient, DevApiResultError


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
    """1. entity.list emits each present component as a NAMED GROUP (only a live host with seeded
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
    assert _approx(dr["color"], [0.2, 0.4, 0.6, 1], tol=0.5 / 255), f"color read back as {dr['color']}"  # RGBA8 step
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
    print("PASS: entity.set end-to-end over the transport — reads, writes through apply() (dirty/repack/normalize), and every bad_params + atomicity path machine-asserted.")


if __name__ == "__main__":
    sys.exit(launch.main(run))
