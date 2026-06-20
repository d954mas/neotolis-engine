#!/usr/bin/env python3
"""DevAPI ui demo — the live-socket UAT for the ui.* read + write surface (UITREE-02/04).

Connects to a running examples/devapi_host over loopback TCP and exercises the ui group end-to-end,
the layer only a live socket proves: resolve a developer string id -> bbox center px -> inject the
SAME low-level pointer events as raw input -> advance the sim -> the widget reacts. The unit/build
gates cover registration + the in-process probe; this UAT covers the inject->settle->react loop and
the bad_params paths over a real socket.

  1. ui.contexts lists the host-registered "hud" context.
  2. ui.tree returns the {space, origin, y_axis, width, height, dpr, projection} metadata block + nodes,
     explicitly declaring the Y-up (origin bottom-left) coordinate contract, and the known "hud_btn"
     node is present with bounds in that space.
  3. ui.click("hud_btn") then stepping the sim toggles the widget: the host flips hud_btn's `enabled`
     on the synthetic click, observed via ui.element read-back (resolve -> inject -> settle -> react).
  4. read hud_btn's Y-up bounds from ui.tree, compute the center, and ui.click({x,y}) with those Y-up
     coords WITHOUT any bot-side flip — the click lands and the widget toggles, proving read==write.
  5. select the SCALED "hud_scaled" ctx (non-identity viewport: 2x scale + letterbox margin), read
     scaled_btn's Y-up bounds, ui.click({x,y}) with those layout coords, advance, and the widget toggles
     — proving ui.click lands on a SCALED ctx (the layout->device map goes through the ctx viewport).
  6. ui.click("does_not_exist") is caught as bad_params (unknown id -> no crash/assert).
  7. a non-finite-coord / over-cap-frames ui.drag is rejected as bad_params (coords/frames hardened).

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


def _btn_enabled(client: DevApiClient, btn_id="hud_btn", ctx=None):
    """Read a widget's current `enabled` via ui.element; asserts it is a bool."""
    v = client.ui_element(btn_id, ctx=ctx).get("node", {}).get("enabled")
    assert isinstance(v, bool), f"ui.element({btn_id}).enabled is {v!r}, expected a bool"
    return v


def _click_until_toggled(client: DevApiClient, do_click, before, btn_id="hud_btn", ctx=None) -> bool:
    """Re-issue do_click() + step until the widget's enabled flips off `before`; True if it toggled."""
    for _ in range(20):  # generous budget: down@0/up@1 apply across advancing frames (1-frame IM lag)
        do_click()           # enqueue down+up (fire-and-forget)
        client.step(count=4)  # advance: release+apply the down, then the up; the click lands
        if _btn_enabled(client, btn_id, ctx) != before:
            return True
    return False


def run(client: DevApiClient) -> None:
    """Drive the ui read + write workflows; raise AssertionError on any contract violation."""
    # Manual mode so the fire-and-forget inject's down/up apply on explicit, deterministic step()s.
    client.set_mode("manual")
    client.step(count=1)

    # 1. ui.contexts lists the host-registered contexts (order-independent).
    contexts = client.ui_contexts().get("contexts")
    assert set(contexts) == {"hud", "hud_scaled"}, f"ui.contexts is {contexts!r}, expected hud + hud_scaled"
    print("PASS[1/7] ui.contexts lists the registered 'hud' + 'hud_scaled' contexts.")

    # 2. ui.tree returns the metadata block declaring the Y-up contract; hud_btn is present with bounds.
    tree = client.ui_tree()
    for key in ("space", "origin", "y_axis", "width", "height", "dpr", "projection", "nodes"):
        assert key in tree, f"ui.tree missing top-level key {key!r} (got {sorted(tree)})"
    assert tree["space"] == "ui", f"ui.tree space is {tree['space']!r}, expected 'ui'"
    assert tree["origin"] == "bottom-left", f"ui.tree origin is {tree['origin']!r}, expected 'bottom-left'"
    assert tree["y_axis"] == "up", f"ui.tree y_axis is {tree['y_axis']!r}, expected 'up'"
    assert tree["height"] > 0, f"ui.tree height is {tree['height']!r}, expected > 0"
    assert tree["projection"] in ("2d", "3d"), f"ui.tree projection is {tree['projection']!r}"
    btn = _find_node(tree, "hud_btn")
    assert btn is not None, "ui.tree did not contain the 'hud_btn' node"
    bounds = btn.get("bounds") or {}
    for k in ("x", "y", "w", "h"):
        assert k in bounds, f"hud_btn bounds missing {k!r} (got {bounds})"
    assert bounds["w"] > 0 and bounds["h"] > 0, f"hud_btn has degenerate bounds {bounds}"
    print(f"PASS[2/7] ui.tree declares the Y-up (origin bottom-left) contract + hud_btn bounds {bounds}.")

    # 3. ui.click("hud_btn") -> step -> the host toggles hud_btn.enabled (resolve->inject->settle->react).
    before = _btn_enabled(client)
    toggled = _click_until_toggled(client, lambda: client.ui_click("hud_btn"), before)
    assert toggled, f"ui.click('hud_btn') did not toggle enabled off {before!r} after stepping (resolve->inject->react failed)"
    print("PASS[3/7] ui.click('hud_btn') resolved -> injected -> settled -> the widget toggled (read-back confirms).")

    # 4. THE regression for the original bug: read hud_btn Y-up bounds from ui.tree, compute the center,
    #    and ui.click({x,y}) with those Y-up coords directly (NO bot-side flip). read==write -> it lands.
    btn = _find_node(client.ui_tree(), "hud_btn")
    assert btn is not None, "ui.tree did not contain 'hud_btn' on the {x,y} regression read"
    b = btn["bounds"]
    cx = b["x"] + (b["w"] * 0.5)
    cy = b["y"] + (b["h"] * 0.5)  # Y-up center, straight from the bounds — exactly what we feed back.
    before = _btn_enabled(client)
    toggled = _click_until_toggled(client, lambda: client.ui_click({"x": cx, "y": cy}), before)
    assert toggled, f"ui.click({{x:{cx}, y:{cy}}}) (Y-up bounds center) did not toggle hud_btn — read-bounds != click-{{x,y}} (the flip bug)"
    print(f"PASS[4/7] ui.click({{x:{cx:.1f}, y:{cy:.1f}}}) from Y-up bounds toggled hud_btn (read-bounds -> click-{{x,y}} lands, no bot flip).")

    # 5. THE scaled regression: the "hud_scaled" ctx has a non-identity viewport (2x scale + letterbox
    #    margin). Read scaled_btn's Y-up bounds, ui.click({x,y}) with those LAYOUT coords (no bot flip),
    #    and the widget toggles — proving the layout->device map goes through the ctx viewport so a
    #    SCALED ui.click lands. ui.tree also exposes the device viewport rect for the bot.
    tree_scaled = client.ui_tree(ctx="hud_scaled")
    vp = tree_scaled.get("viewport") or {}
    for k in ("x", "y", "w", "h"):
        assert k in vp, f"ui.tree(hud_scaled) viewport missing {k!r} (got {vp})"
    sbtn = _find_node(tree_scaled, "scaled_btn")
    assert sbtn is not None, "ui.tree(hud_scaled) did not contain the 'scaled_btn' node"
    sb = sbtn["bounds"]
    scx = sb["x"] + (sb["w"] * 0.5)
    scy = sb["y"] + (sb["h"] * 0.5)
    before = _btn_enabled(client, "scaled_btn", ctx="hud_scaled")
    toggled = _click_until_toggled(client, lambda: client.ui_click({"x": scx, "y": scy}, ctx="hud_scaled"), before, "scaled_btn", "hud_scaled")
    assert toggled, f"ui.click({{x:{scx}, y:{scy}}}, ctx=hud_scaled) did not toggle scaled_btn — the scaled layout->device map failed"
    print(f"PASS[5/7] ui.click({{x:{scx:.1f}, y:{scy:.1f}}}, ctx=hud_scaled) toggled scaled_btn (scaled layout->device via the ctx viewport {vp}).")

    # 6. unknown id -> bad_params, never a crash/assert.
    try:
        client.ui_click("does_not_exist")
        raise AssertionError("ui.click('does_not_exist') unexpectedly succeeded; expected bad_params")
    except DevApiResultError as exc:
        assert "bad_params" in str(exc), f"ui.click(unknown) raised {exc!r}, expected a bad_params error"
    print("PASS[6/7] ui.click(unknown id) -> bad_params (no crash/assert on bot input).")

    # 7. Non-finite coord and over-cap frames on ui.drag -> bad_params (untrusted coords/frames hardened).
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
    print("PASS[7/7] ui.drag(NaN coord / over-cap frames) -> bad_params (whole-or-nothing, never a partial inject).")

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
