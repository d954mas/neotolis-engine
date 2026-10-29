"""Compare the paused CPU reference with GPU skinning in skeletal_showcase; requires Pillow and NumPy.

Run from the repository root against a native skeletal_showcase with Skinned Meshes selected, CPU
reference enabled and Bones disabled (attach with --port 17890, or launch with --host-bin):
python -m tools.devapi.scenarios.skeletal_compare --port 17890 --out build/skeletal-compare
The script captures the stage, toggles CPU reference off, captures again and toggles it back on; it
writes gpu.png, cpu.png and result.json to the --out directory.
"""
import base64
import io
import json
import re
import sys
from pathlib import Path

from .. import launch

REFERENCE_ID = "skinned/reference"


def _pose(client):
    labels = [n["text"] for n in client.ui_tree()["nodes"] if n.get("visible") and n.get("text")]
    model = next((name for name in ("Fox", "CesiumMan", "Humanoid") if name + " v" in labels), None)
    pose = next((match for label in labels if (match := re.fullmatch(r"(.+)  ([0-9.]+) / ([0-9.]+) s", label))), None)
    if model is None or pose is None:
        raise AssertionError("Select Skinned Meshes and keep Controls visible to record model, clip and time")
    return model, pose[1], float(pose[2])


def _capture_stage(client, np, Image):
    tree = client.ui_tree()
    stage = client.ui_element("skeletal_showcase/stage")["node"]["bounds"]
    png = base64.b64decode(client.capture_frame(scale=1)["data"])
    frame = np.array(Image.open(io.BytesIO(png)).convert("RGB"))
    viewport = tree["viewport"]
    sx, sy = viewport["w"] / tree["width"], viewport["h"] / tree["height"]
    x = int(viewport["x"] + stage["x"] * sx)
    h = int(stage["h"] * sy)
    y = int(viewport["y"] + (tree["height"] - stage["y"]) * sy) - h
    w = int(stage["w"] * sx)
    return frame[y:y + h, x:x + w], frame[0, 0]


def run(client, out_dir):
    client.set_player_enabled(False)  # a real mouse over the window would hide the bot's clicks
    import numpy as np
    from PIL import Image

    output = Path(out_dir)
    output.mkdir(parents=True, exist_ok=True)
    before = _pose(client)
    cpu, background = _capture_stage(client, np, Image)
    client.ui_click(REFERENCE_ID)
    client.wait_frames(3)
    after = _pose(client)
    gpu, _ = _capture_stage(client, np, Image)
    client.ui_click(REFERENCE_ID)
    client.wait_frames(3)
    if before != after:
        raise AssertionError(f"The pose moved between captures ({before} -> {after}); enable CPU reference first so the player pauses")
    if gpu.shape != cpu.shape or gpu.size == 0:
        raise AssertionError(f"Stage captures differ in shape: {gpu.shape} vs {cpu.shape}")
    if not (np.all(gpu[0] == background) and np.all(cpu[0] == background)):
        raise AssertionError("Stage must have a clear, uniform background")
    covered = np.any(gpu != background, axis=2) | np.any(cpu != background, axis=2)
    mismatches = np.any(np.abs(gpu.astype(int) - cpu.astype(int)) > 2, axis=2) & covered
    coverage = int(covered.sum())
    bad = int(mismatches.sum())
    Image.fromarray(gpu).save(output / "gpu.png")
    Image.fromarray(cpu).save(output / "cpu.png")
    report = {"model": before[0], "clip": before[1], "time": before[2],
              "covered_pixels": coverage, "mismatched_pixels": bad, "channel_tolerance": 2, "max_fraction": 0.005,
              "mismatch_fraction": bad / coverage if coverage else None,
              "gpu": str(output / "gpu.png"), "cpu": str(output / "cpu.png")}
    (output / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    if coverage == 0:
        raise AssertionError("Empty model coverage")
    if bad / coverage > 0.005:
        raise AssertionError(f"GPU/CPU mismatch: {bad}/{coverage} pixels")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    sys.exit(launch.main(run))
