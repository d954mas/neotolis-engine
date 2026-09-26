"""Capture and validate all Mixing & Crossfades modes in skeletal_showcase."""

import argparse
import base64
import json
import math
import re
from pathlib import Path

from tools.devapi.client import DevApiClient
from tools.devapi.transport import SocketTransport


MODES = ("Crossfade", "Blend space", "Partial body", "Override", "Interruption")


def _texts(client):
    return [node["text"] for node in client.ui_tree()["nodes"] if node.get("visible") and node.get("text")]


def _click_text(client, text):
    node = next(node for node in client.ui_tree()["nodes"] if node.get("visible") and node.get("text") == text)
    bounds = node["bounds"]
    client.ui_click({"x": bounds["x"] + bounds["w"] / 2, "y": bounds["y"] + bounds["h"] / 2})
    client.wait_frames(3)


def _select_scene(client):
    if "Mixing & Crossfades v" in _texts(client):
        return
    client.ui_click("shell/scene_combo")
    client.wait_frames(3)
    _click_text(client, "Mixing & Crossfades")


def _select_mode(client, mode):
    if mode in _texts(client) and mode == next((candidate for candidate in MODES if candidate in _texts(client)), None):
        return
    client.ui_click("mixing/mode")
    client.wait_frames(3)
    _click_text(client, mode)


def _capture(client, output, name):
    result = client.capture_frame(scale=1)
    path = output / f"{name}.png"
    path.write_bytes(base64.b64decode(result["data"]))
    return str(path)


def run(client, output, backend="native"):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    _select_scene(client)
    client.wait_frames(5)
    if "Show sources" not in _texts(client):
        raise AssertionError("Mixing scene controls did not become visible")
    if "Sources: on" not in _texts(client):
        client.ui_click("mixing/sources")
        client.wait_frames(3)
    if "Sources: on" not in _texts(client):
        raise AssertionError("Show sources did not enable source previews")

    captures = {}
    telemetry = {}
    for mode in MODES:
        _select_mode(client, mode)
        client.wait_frames(5)
        texts = _texts(client)
        alpha = next((text for text in texts if text.startswith("alpha ")), None)
        memory = next((text for text in texts if text.startswith("snapshot ")), None)
        if alpha is None or memory is None:
            raise AssertionError(f"{mode}: missing alpha/ownership telemetry")
        phase = next((text for text in texts if text.startswith("phase ")), None)
        if mode == "Blend space" and phase is None:
            raise AssertionError("Blend space: missing normalized phase telemetry")
        if phase is not None:
            phase_value = float(phase.removeprefix("phase "))
            if not math.isfinite(phase_value) or not 0.0 <= phase_value < 1.0:
                raise AssertionError(f"Blend space: invalid normalized phase: {phase}")
        captures[mode] = _capture(client, output, mode.lower().replace(" ", "_"))
        telemetry[mode] = {"alpha": alpha, "memory": memory, "phase": phase}

    _select_mode(client, "Interruption")
    client.ui_click("mixing/reset")
    client.wait_frames(3)
    client.ui_click("mixing/play")
    client.wait_frames(2)
    client.ui_click("mixing/to_jump")
    client.wait_frames(2)
    handoff_texts = _texts(client)
    handoff_zero = next(text for text in handoff_texts if text.endswith("source; handoffs 1"))
    handoff_alpha = next(text for text in handoff_texts if text.startswith("alpha "))
    handoff_memory = next(text for text in handoff_texts if text.startswith("snapshot "))
    if not handoff_alpha.startswith("alpha 0.00 |"):
        raise AssertionError(f"Handoff did not start at alpha zero: {handoff_alpha}")
    client.ui_click("mixing/play")
    client.wait_frames(2)
    client.ui_click("mixing/repeat")
    for _ in range(3):
        client.wait_frames(15)
    repeated_texts = _texts(client)
    repeated = next(text for text in repeated_texts if "source; handoffs" in text)
    repeated_memory = next(text for text in repeated_texts if text.startswith("snapshot "))
    count = int(re.search(r"handoffs (\d+)", repeated).group(1))
    if count < 2:
        raise AssertionError(f"Repeat did not interrupt an active transition: {repeated}")
    if repeated_memory != handoff_memory:
        raise AssertionError(f"Interruption memory changed: {handoff_memory} -> {repeated_memory}")
    captures["Interruption repeated"] = _capture(client, output, "interruption_repeated")

    report = {
        "backend": backend,
        "modes": list(MODES),
        "captures": captures,
        "telemetry": telemetry,
        "handoff_zero": handoff_zero,
        "handoff_alpha": handoff_alpha,
        "repeated": repeated,
        "memory": handoff_memory,
    }
    (output / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=17890)
    parser.add_argument("--output", default="build/skeletal-mixing")
    args = parser.parse_args()
    with SocketTransport(port=args.port) as transport:
        print(json.dumps(run(DevApiClient(transport), args.output), indent=2))
