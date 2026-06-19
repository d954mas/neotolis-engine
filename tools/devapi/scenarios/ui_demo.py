#!/usr/bin/env python3
"""DevAPI ui demo — the live-socket UAT for the ui.* read + write surface (UITREE-02/04).

Connects to a running examples/devapi_host over loopback TCP and exercises the ui group end-to-end,
the layer only a live socket proves: resolve a developer string id -> bbox center px -> inject the
SAME low-level pointer events as raw input -> advance the sim -> the widget reacts. The unit/build
gates cover registration + the in-process probe; this UAT covers the inject->settle->react loop and
the bad_params paths over a real socket.

  1. ui.contexts lists the host-registered "hud" context.
  2. ui.tree returns the {space, fb_width, fb_height, dpr, projection} metadata block + nodes, and the
     known "hud_btn" node is present with framebuffer bounds.
  3. ui.click("hud_btn") then stepping the sim toggles the widget: the host flips hud_btn's `enabled`
     on the synthetic click, observed via ui.element read-back (resolve -> inject -> settle -> react).
  4. ui.click("does_not_exist") is caught as bad_params (unknown id -> no crash/assert).
  5. a non-finite-coord / over-cap-frames ui.drag is rejected as bad_params (coords/frames hardened).

ui.* are fire-and-forget/immediate (D-14): the down/up enqueue and apply only as the sim advances, so
the UAT runs in manual mode and step()s between the click and the read-back. Unlike the input UAT there
is NO blocking-drain-race to assert — the exit code IS the assertion.

Usage: python tools/devapi/scenarios/ui_demo.py [--port N]
  Port resolution: --port N  >  env NT_DEVAPI_PORT  >  default 17890.
Returns exit 0 if every observable assertion passes, exit 1 on any failure (connect /
timeout / assertion / protocol). Stdlib only (socket + json) — no pip deps.
"""
import os
import sys

# Allow running as a plain script by making the repo root importable, so the
# absolute `tools.devapi.*` imports resolve (mirrors input_demo.py).
if __package__ in (None, ""):
    sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..")))
    from tools.devapi.client import DevApiClient, DevApiResultError
    from tools.devapi.transport import DEFAULT_PORT, DEFAULT_READ_TIMEOUT, SocketTransport
else:
    from ..client import DevApiClient, DevApiResultError
    from ..transport import DEFAULT_PORT, DEFAULT_READ_TIMEOUT, SocketTransport


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


def _find_node(tree, node_id):
    """Linear scan of a ui.tree `nodes` array for the node whose id_string matches; None if absent."""
    for n in tree.get("nodes", []):
        if n.get("id_string") == node_id:
            return n
    return None


def run(client: DevApiClient) -> None:
    """Drive the ui read + write workflows; raise AssertionError on any contract violation."""
    # Manual mode so the fire-and-forget inject's down/up apply on explicit, deterministic step()s.
    client.set_mode("manual")
    client.step(count=1)

    # 1. ui.contexts lists the host-registered "hud" context.
    contexts = client.ui_contexts().get("contexts")
    assert contexts == ["hud"], f"ui.contexts is {contexts!r}, expected ['hud']"
    print("PASS[1/5] ui.contexts lists the registered 'hud' context.")

    # 2. ui.tree returns the metadata block + nodes; hud_btn is present with framebuffer bounds.
    tree = client.ui_tree()
    for key in ("space", "fb_width", "fb_height", "dpr", "projection", "nodes"):
        assert key in tree, f"ui.tree missing top-level key {key!r} (got {sorted(tree)})"
    assert tree["space"] == "framebuffer", f"ui.tree space is {tree['space']!r}, expected 'framebuffer'"
    assert tree["projection"] in ("2d", "3d"), f"ui.tree projection is {tree['projection']!r}"
    btn = _find_node(tree, "hud_btn")
    assert btn is not None, "ui.tree did not contain the 'hud_btn' node"
    bounds = btn.get("bounds") or {}
    for k in ("x", "y", "w", "h"):
        assert k in bounds, f"hud_btn bounds missing {k!r} (got {bounds})"
    assert bounds["w"] > 0 and bounds["h"] > 0, f"hud_btn has degenerate bounds {bounds}"
    print(f"PASS[2/5] ui.tree returns the metadata block + hud_btn with framebuffer bounds {bounds}.")

    # 3. ui.click("hud_btn") -> step -> the host toggles hud_btn.enabled (resolve->inject->settle->react).
    before = client.ui_element("hud_btn").get("node", {}).get("enabled")
    assert isinstance(before, bool), f"ui.element(hud_btn).enabled is {before!r}, expected a bool"
    toggled = False
    for _ in range(20):  # generous budget: the down@0/up@1 apply across advancing frames (1-frame IM lag)
        client.ui_click("hud_btn")  # enqueue down+up (fire-and-forget)
        client.step(count=4)        # advance: release+apply the down, then the up; the click lands
        now = client.ui_element("hud_btn").get("node", {}).get("enabled")
        if now != before:
            toggled = True
            break
    assert toggled, f"ui.click('hud_btn') did not toggle enabled off {before!r} after stepping (resolve->inject->react failed)"
    print("PASS[3/5] ui.click('hud_btn') resolved -> injected -> settled -> the widget toggled (read-back confirms).")

    # 4. unknown id -> bad_params, never a crash/assert.
    try:
        client.ui_click("does_not_exist")
        raise AssertionError("ui.click('does_not_exist') unexpectedly succeeded; expected bad_params")
    except DevApiResultError as exc:
        assert "bad_params" in str(exc), f"ui.click(unknown) raised {exc!r}, expected a bad_params error"
    print("PASS[4/5] ui.click(unknown id) -> bad_params (no crash/assert on bot input).")

    # 5. Non-finite coord and over-cap frames on ui.drag -> bad_params (untrusted coords/frames hardened).
    # 1e39 is a VALID-JSON finite double that narrows to float +inf (FLT_MAX ~ 3.4e38) — it exercises
    # the host's parse_finite_coord isfinite() guard over the wire (a literal NaN/Infinity is not legal
    # JSON, so the realistic bot-supplied non-finite arrives as an overflowing finite number).
    try:
        client.ui_drag({"x": 1e39, "y": 0.0}, "hud_btn")
        raise AssertionError("ui.drag with a non-finite coord unexpectedly succeeded; expected bad_params")
    except DevApiResultError as exc:
        assert "bad_params" in str(exc), f"ui.drag(1e39) raised {exc!r}, expected bad_params"
    try:
        client.ui_drag("hud_btn", "hud_btn_b", frames=1_000_000)
        raise AssertionError("ui.drag with over-cap frames unexpectedly succeeded; expected bad_params")
    except DevApiResultError as exc:
        assert "bad_params" in str(exc), f"ui.drag(over-cap frames) raised {exc!r}, expected bad_params"
    print("PASS[5/5] ui.drag(NaN coord / over-cap frames) -> bad_params (whole-or-nothing, never a partial inject).")

    # Restore a normal live state for the next session: back to RUN.
    client.set_mode("run")
    client.resume()

    print("PASS: ui.* end-to-end over the socket — contexts, tree+metadata, click->react, and the bad_params paths all machine-asserted.")


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
        # Best-effort restore so an assertion failure mid-run can't leave the long-lived host
        # MANUAL / paused for the next session. Must not mask the original failure/exit code.
        if transport is not None:
            try:
                client = DevApiClient(transport)
                client.set_mode("run")
                client.resume()
            except (DevApiResultError, ValueError, ConnectionError, TimeoutError, OSError):
                pass
            transport.close()


if __name__ == "__main__":
    args = sys.argv[1:]
    if "--help" in args or "-h" in args:
        print(__doc__)
        sys.exit(0)
    sys.exit(main(resolve_port(args)))
