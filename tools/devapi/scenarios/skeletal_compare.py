"""Compare the paused GPU/CPU view in skeletal_showcase; requires Pillow and NumPy.

Run from the repository root after enabling Compare CPU and disabling Bones:
python -m tools.devapi.scenarios.skeletal_compare --output build/skeletal-compare
The compare() function also accepts a client backed by PlaywrightTransport.
"""
import argparse
import base64
import io
import json
import re
from pathlib import Path

from tools.devapi.client import DevApiClient
from tools.devapi.transport import SocketTransport


def compare(client, output, backend="native"):
    import numpy as np
    from PIL import Image

    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    tree = client.ui_tree()
    labels = [n["text"] for n in tree["nodes"] if n.get("visible") and n.get("text")]
    if "CPU reference (paused)" not in labels:
        raise AssertionError("Enable Compare CPU in Skinned Meshes before capturing")
    model = next((name for name in ("Fox", "CesiumMan", "Humanoid") if name + " v" in labels), None)
    pose = next((match for label in labels if (match := re.fullmatch(r"(.+)  ([0-9.]+) / ([0-9.]+) s", label))), None)
    assert model is not None and pose is not None, "Keep controls visible to record model, clip and time"
    stage = client.ui_element("skeletal_showcase/stage")["node"]["bounds"]
    capture = client.capture_frame(scale=1)
    png = base64.b64decode(capture["data"])
    (output / "frame.png").write_bytes(png)
    frame = np.array(Image.open(io.BytesIO(png)).convert("RGB"))
    viewport = tree["viewport"]
    sx, sy = viewport["w"] / tree["width"], viewport["h"] / tree["height"]
    x = int(viewport["x"] + stage["x"] * sx)
    h = int(stage["h"] * sy)
    y = int(viewport["y"] + (tree["height"] - stage["y"]) * sy) - h
    w = int(stage["w"] * sx) // 2
    gpu = frame[y:y + h, x:x + w]
    cpu = frame[y:y + h, x + w:x + 2 * w]
    background = frame[0, 0]
    assert gpu.shape == cpu.shape and gpu.size > 0
    assert np.all(gpu[0] == background) and np.all(cpu[0] == background), "Stage must have a clear, uniform background"
    covered = np.any(gpu != background, axis=2) | np.any(cpu != background, axis=2)
    mismatches = np.any(np.abs(gpu.astype(int) - cpu.astype(int)) > 2, axis=2) & covered
    coverage = int(covered.sum())
    bad = int(mismatches.sum())
    Image.fromarray(gpu).save(output / "gpu.png")
    Image.fromarray(cpu).save(output / "cpu.png")
    report = {"backend": backend, "model": model, "clip": pose[1], "time": float(pose[2]),
              "labels": labels, "covered_pixels": coverage,
              "mismatched_pixels": bad, "channel_tolerance": 2, "max_fraction": 0.005,
              "mismatch_fraction": bad / coverage if coverage else None,
              "gpu": str(output / "gpu.png"), "cpu": str(output / "cpu.png")}
    (output / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    assert coverage > 0, "Empty model coverage"
    assert bad / coverage <= 0.005, f"GPU/CPU mismatch: {bad}/{coverage} pixels"
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=17890)
    parser.add_argument("--output", default="build/skeletal-compare")
    args = parser.parse_args()
    with SocketTransport(port=args.port) as transport:
        report = compare(DevApiClient(transport), args.output)
    print(json.dumps(report, indent=2))
