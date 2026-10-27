#!/usr/bin/env python3
"""record.* check on an examples/devapi_host built with NT_DEVAPI_GROUP_RECORDER, native (Windows) or web.

  1. bad params answer bad_params; a start while recording answers recording; a second stop reports 0.
  2. real time — RUN at scale 7 (steps over the 100 ms drift bound, so a one-step audio offset would
     show), fps 5 for ~1.5 s: max_drift_ms stays within 100 ms. Web: a listener takes the blob, so
     nothing downloads.
  3. native: real time at fps 30 for 4 s of game time, checked with check_recording.py --real-time.
  4. lockstep — MANUAL, start half a second off a flash, 360 steps of 1/60 at fps 30: 180 frames, checked
     with check_recording.py (needs ffmpeg/ffprobe on PATH or FFMPEG_DIR).

A start answers not_ready until the web codec probe settles, a moment after load; it is retried.
Native writes the files to the --out directory; web downloads the lockstep clip there.

Usage: python -m tools.devapi.scenarios.record_demo [--target native|web] (options: tools/devapi/launch.py)
Exit 0 pass, 1 failure, 2 usage / launch (tools/devapi/launch.py).
"""
import math
import os
import subprocess
import sys
import time

from .. import launch
from ..client import DevApiClient

CHECK = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "check_recording.py")

# The web host takes the real-time clip: the event is cancelled, so the browser downloads nothing.
_TAKE_BLOB_JS = """() => {
    window.__rec = null;
    window.addEventListener('nt-recorder', (e) => {
        e.preventDefault();
        const d = e.detail;
        window.__rec = { size: d.blob ? d.blob.size : 0, error: d.error };
    }, { once: true });
}"""


def _frame(page) -> None:
    """One rendered frame: the window reads a viewport resize in its next poll, and a start before that
    would record the old size and end at the first frame."""
    page.evaluate("() => new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r)))")


def _error_code(client: DevApiClient, method: str, params: dict) -> str:
    resp = client.request(method, params)
    assert resp.get("ok") is False, f"{method} {params} succeeded, expected an error"
    return resp["error"]["code"]


def _start(client: DevApiClient, name: str, fps: int) -> None:
    deadline = time.monotonic() + 30.0
    while True:
        resp = client.request("record.start", {"name": name, "fps": fps})
        if resp.get("ok") is True:
            return
        code = resp["error"]["code"]
        assert code == "not_ready", f"record.start failed: {resp['error']}"
        assert time.monotonic() < deadline, "record.start: still not_ready after 30 s"
        time.sleep(0.1)


def _real_time(client: DevApiClient, page) -> None:
    name = "record_run.mp4" if page else os.path.join(launch.out_dir(), "record_run.mp4")
    if page:
        page.set_viewport_size({"width": 320, "height": 180})  # a software encoder (CI) keeps up
        _frame(page)
        page.evaluate(_TAKE_BLOB_JS)
    client.set_mode("run")
    client.resume()
    client.set_fps(50)  # native runs uncapped; 50 fps x scale 7 gives 0.14 s steps
    client.set_scale(7)
    _start(client, name, 5)
    a = client.frame_current()
    time.sleep(1.5)
    b = client.frame_current()
    stats = client.result("record.stop")
    client.set_scale(1)
    client.set_fps(0)
    step = (b["time"] - a["time"]) / max(b["frame"] - a["frame"], 1)
    assert step > 0.1, f"real time: {step * 1000:.0f} ms steps, not over the 100 ms drift bound"
    assert stats["frames"] > 5, f"real time: {stats['frames']} frames"
    assert abs(stats["max_drift_ms"]) <= 100, f"real time: audio drifted {stats['max_drift_ms']:.0f} ms"
    if page:
        rec = page.wait_for_function("() => window.__rec", timeout=30_000).json_value()
        assert rec["error"] is None, f"real time: recorder event error {rec['error']}"
        assert rec["size"] > 0, "real time: empty blob"
        page.set_viewport_size({"width": 1280, "height": 720})
        _frame(page)
    print(f"PASS real time: {stats['frames']} frames, {step * 1000:.0f} ms steps, max drift {stats['max_drift_ms']:+.0f} ms")


def _lockstep(client: DevApiClient, page, downloads: list) -> None:
    path = os.path.join(launch.out_dir(), "record_lockstep.mp4")
    client.set_mode("manual")
    # Half a second off a flash, so the first and last flash/beep pairs are whole.
    t = client.frame_current()["time"]
    steps = round(((math.floor(t) + 1.5 - t) % 1) * 60)
    if steps:
        client.step(steps)
    _start(client, os.path.basename(path) if page else path, 30)
    assert _error_code(client, "record.start", {"name": "again.mp4", "fps": 30}) == "recording"
    client.step(360)
    if page:
        with page.expect_download(timeout=30_000) as info:
            frames = client.result("record.stop")["frames"]
        download = info.value
        assert download.suggested_filename == os.path.basename(path), download.suggested_filename
        download.save_as(path)
        # This download is the barrier: the real-time clip, cancelled earlier, would have arrived first.
        assert len(downloads) == 1, f"{len(downloads)} downloads, expected only the lockstep clip"
    else:
        frames = client.result("record.stop")["frames"]
    assert frames == 180, f"lockstep: {frames} frames, expected 180"
    assert client.result("record.stop")["frames"] == 0, "a second stop reported frames"
    subprocess.run([sys.executable, CHECK, path, "--fps", "30", "--frames", "180"], check=True)
    print("PASS lockstep: 180 frames, check_recording passed")


def _real_time_file(client: DevApiClient) -> None:
    """Native: a RUN clip at fps 30 decoded and checked like the lockstep one (web takes its blob)."""
    path = os.path.join(launch.out_dir(), "record_run30.mp4")
    client.set_mode("run")
    client.resume()
    _start(client, path, 30)
    client.time_wait(4.0)
    frames = client.result("record.stop")["frames"]
    assert frames > 0, "real time fps 30: no frames"
    subprocess.run([sys.executable, CHECK, path, "--fps", "30", "--frames", str(frames), "--real-time"], check=True)
    print(f"PASS real time fps 30: {frames} frames, check_recording passed")


def run(client: DevApiClient) -> None:
    for params in ({"name": "x.mp4", "fps": 0}, {"name": "x.mp4", "fps": 121}, {"name": "", "fps": 5}):
        assert _error_code(client, "record.start", params) == "bad_params", params
    page = getattr(client.transport, "page", None)
    downloads = []
    if page:
        page.on("download", lambda d: downloads.append(d))
    _real_time(client, page)
    if not page:
        _real_time_file(client)
    _lockstep(client, page, downloads)


if __name__ == "__main__":
    sys.exit(launch.main(run))
