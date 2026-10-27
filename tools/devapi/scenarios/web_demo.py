#!/usr/bin/env python3
"""Web-only devapi checks on the wasm examples/devapi_host, through the raw window.__devapi bridge where
the client hides the behavior under test:

  1. capture.frame defers (submit returns ""); while its reply is pending every command answers busy;
     reset() drops it and the next command is served.
  2. after two WebGL context lose/restore cycles a MANUAL capture is a non-blank PNG and the frame
     counter did not move.
  3. takeover: the wait of a killed client, parked mid-frame, does not take the next client's reply
     when it wakes.

Usage: python -m tools.devapi.scenarios.web_demo --target web (options: tools/devapi/launch.py)
Exit 0 pass, 1 failure, 2 usage / launch (tools/devapi/launch.py).
"""
import json
import sys

from .. import launch, pixel_health
from ..client import DevApiClient
from ..transport import _POLL_AFTER_FRAME_JS, PlaywrightTransport

_FRAME_JS = "() => new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r)))"

_LOSE_RESTORE_JS = """async () => {
    const canvas = document.querySelector('canvas');
    const loss = canvas.getContext('webgl2').getExtension('WEBGL_lose_context');
    if (!loss) throw new Error('WEBGL_lose_context unavailable');
    const lost = new Promise(r => canvas.addEventListener('webglcontextlost', () => r(), { once: true }));
    loss.loseContext();
    await lost;
    await new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r)));
    const restored = new Promise(r => canvas.addEventListener('webglcontextrestored', () => r(), { once: true }));
    loss.restoreContext();
    await restored;
}"""

# The transport's wait, started with requestAnimationFrame bound to a gate: it polls once (empty) and
# parks on the gate like a killed client's wait parked on a frame. Block body: returning the orphan's
# promise would make page.evaluate wait on it.
_ORPHAN_JS = (
    "() => { let open; window.__gate = new Promise(r => open = r); window.__open = open; "
    "const gatedRaf = cb => window.__gate.then(cb); "
    f"window.__orphan = ((requestAnimationFrame) => ({_POLL_AFTER_FRAME_JS})())(gatedRaf); }}"
)


def _submit(page, obj) -> str:
    return page.evaluate("(l) => window.__devapi.submit(l)", json.dumps(obj))


def _check_defer_busy_reset(page) -> None:
    assert _submit(page, {"method": "capture.frame", "request_id": 900}) == "", "capture.frame answered at once"
    busy = json.loads(_submit(page, {"method": "ping", "request_id": 901}))
    assert busy.get("error", {}).get("code") == "busy", f"a command while a reply is pending: {busy}"
    page.evaluate("() => window.__devapi.reset()")
    served = json.loads(_submit(page, {"method": "ping", "request_id": 902}))
    assert served.get("ok") is True, f"ping after reset: {served}"
    print("PASS capture.frame defers, busy while pending, reset() frees it")


def _check_capture_after_context_loss(client: DevApiClient, page) -> None:
    for _ in range(2):
        page.evaluate(_LOSE_RESTORE_JS)
        page.evaluate(_FRAME_JS)
    view = client.result("view")
    client.set_mode("manual")
    before = client.frame_current()["frame"]
    pixel_health.check_payload(client.capture_frame(), int(view["fb_width"]), int(view["fb_height"]))
    after = client.frame_current()["frame"]
    assert after == before, f"capture advanced the MANUAL sim: frame {before} -> {after}"
    print("PASS capture after two context lose/restore cycles: non-blank PNG, frame unchanged")


def _check_takeover(page) -> None:
    page.evaluate(_ORPHAN_JS)
    PlaywrightTransport(page)  # the next client: resets the bridge
    assert _submit(page, {"method": "capture.frame", "request_id": 950}) == "", "capture.frame answered at once"
    for _ in range(3):
        page.evaluate(_FRAME_JS)  # one rendered frame fills the capture; its reply waits in the queue
    page.evaluate("() => window.__open()")
    orphan = page.evaluate("() => window.__orphan")
    assert orphan == "", f"the killed client's wait took a reply: {orphan[:120]!r}"
    for _ in range(10):
        line = page.evaluate("() => window.__devapi.poll()")
        if line:
            break
        page.evaluate(_FRAME_JS)
    assert line, "the next client's capture reply never arrived"
    reply = json.loads(line)
    assert reply.get("request_id") == 950 and reply.get("ok") is True, f"the next client's reply: {line[:120]!r}"
    print("PASS takeover: a parked wait of a killed client leaves the next client's reply alone")


def run(client: DevApiClient) -> None:
    page = getattr(client.transport, "page", None)
    if page is None:
        raise launch.LaunchError("web_demo needs --target web")
    _check_defer_busy_reset(page)
    _check_capture_after_context_loss(client, page)
    _check_takeover(page)


if __name__ == "__main__":
    sys.exit(launch.main(run))
