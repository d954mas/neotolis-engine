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
from pathlib import Path

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


def _start(client: DevApiClient, name: str, fps: int, bitrate_bps: int) -> None:
    deadline = time.monotonic() + 30.0
    while True:
        resp = client.request("record.start", {"name": name, "fps": fps, "bitrate_bps": bitrate_bps})
        if resp.get("ok") is True:
            return
        code = resp["error"]["code"]
        assert code == "not_ready", f"record.start failed: {resp['error']}"
        assert time.monotonic() < deadline, "record.start: still not_ready after 30 s"
        time.sleep(0.1)


def _real_time(client: DevApiClient, page, out_dir: str) -> None:
    name = "record_run.mp4" if page else os.path.join(out_dir, "record_run.mp4")
    if page:
        page.set_viewport_size({"width": 320, "height": 180})  # a software encoder (CI) keeps up
        _frame(page)
        page.evaluate(_TAKE_BLOB_JS)
    client.set_mode("run")
    client.resume()
    client.set_fps(50)  # native runs uncapped; 50 fps x scale 7 gives 0.14 s steps
    client.set_scale(7)
    _start(client, name, 5, 2_000_000)
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


def _lockstep(client: DevApiClient, page, downloads: list, out_dir: str) -> None:
    path = os.path.join(out_dir, "record_lockstep.mp4")
    client.set_mode("manual")
    # Half a second off a flash, so the first and last flash/beep pairs are whole.
    t = client.frame_current()["time"]
    steps = round(((math.floor(t) + 1.5 - t) % 1) * 60)
    if steps:
        client.step(steps)
    _start(client, os.path.basename(path) if page else path, 30, 50_000_000)
    assert _error_code(client, "record.start", {"name": "again.mp4", "fps": 30, "bitrate_bps": 50_000_000}) == "recording"
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


def _real_time_file(client: DevApiClient, out_dir: str) -> None:
    """Native: a RUN clip at fps 30 decoded and checked like the lockstep one (web takes its blob)."""
    path = os.path.join(out_dir, "record_run30.mp4")
    client.set_mode("run")
    client.resume()
    _start(client, path, 30, 100_000_000)
    client.time_wait(4.0)
    frames = client.result("record.stop")["frames"]
    assert frames > 0, "real time fps 30: no frames"
    subprocess.run([sys.executable, CHECK, path, "--fps", "30", "--frames", str(frames), "--real-time"], check=True)
    print(f"PASS real time fps 30: {frames} frames, check_recording passed")


def _web_config_checks(page) -> None:
    """Exercise the shipped JS against encoder doubles, isolated from the live WASM page."""
    backend = Path(launch.REPO_ROOT, "engine/recorder/web/nt_recorder_web.js")
    for high in (True, False):
        probe = page.context.new_page()
        try:
            probe.evaluate("""high => {
                window.configs = []; window.warnings = []; window.rejectConfig = false;
                console.warn = msg => warnings.push(msg);
                window.VideoEncoder = class {
                    static async isConfigSupported(c) { return {supported: high || c.codec.includes('42E0')}; }
                    constructor(c) { this.state = 'unconfigured'; window.encoderError = c.error; }
                    configure(c) { if (rejectConfig) throw new Error('test codec refusal'); configs.push(c); }
                    close() { this.state = 'closed'; }
                };
                window.AudioEncoder = class {
                    static async isConfigSupported() { return {supported: true}; }
                    configure() {} close() { this.state = 'closed'; }
                };
                window.NtMediabunny = {
                    Output: class { addVideoTrack() {} addAudioTrack() {} start() { return Promise.resolve(); }
                                    cancel() { return Promise.resolve(); } },
                    Mp4OutputFormat: class {}, BufferTarget: class {},
                    EncodedVideoPacketSource: class {}, EncodedAudioPacketSource: class {}
                };
                window.addEventListener('nt-recorder', e => { e.preventDefault(); window.ended = e.detail; });
            }""", high)
            probe.add_script_tag(path=str(backend))
            probe.wait_for_function("() => NtRecorder.ready()")
            limit31 = 17_500_000 if high else 14_000_000
            cases = [(1280, 720, 30, limit31, "1F"), (1280, 720, 30, limit31 + 1, "20"),
                     (1280, 720, 60, 1_000_000, "20"), (1920, 1080, 30, 50_000_000, "29"),
                     (1920, 1080, 60, 100_000_000, "32"), (8192, 4320, 60, 100_000_000, "3D"),
                     (3840, 128, 30, 1_000_000, "28"), (128, 3840, 30, 1_000_000, "28")]
            for w, h, fps, bitrate, level in cases:
                cfg = probe.evaluate("""([w,h,fps,bps]) => {
                    if (!NtRecorder.open('test.mp4', {}, w,h,fps,bps)) throw new Error('open refused');
                    NtRecorder.close(); return configs.at(-1);
                }""", [w, h, fps, bitrate])
                assert cfg["bitrate"] == bitrate, cfg
                assert cfg["codec"] == "avc1." + ("6400" if high else "42E0") + level, cfg
            assert probe.evaluate("""bps => {
                const before = configs.length;
                return !NtRecorder.open('test.mp4', {}, 640,360,30,bps) && configs.length === before
                    && warnings.at(-1).includes('level 6.2');
            }""", 1_000_000_001 if high else 800_000_001)
            assert probe.evaluate("""() => {
                rejectConfig = true;
                return !NtRecorder.open('test.mp4', {}, 640,360,30,1000000)
                    && warnings.at(-1).includes('test codec refusal');
            }""")
            probe.evaluate("""() => {
                rejectConfig = false; ended = null;
                if (!NtRecorder.open('test.mp4', {}, 640,360,30,1000000)) throw new Error('restart refused');
                encoderError(new Error('test async refusal'));
                if (NtRecorder.video(0)) throw new Error('error swallowed');
                NtRecorder.close();
            }""")
            ended = probe.wait_for_function("() => ended").json_value()
            assert ended["blob"] is None and "test async refusal" in ended["error"], ended
        finally:
            probe.close()
    print("PASS web configurations: High/Baseline levels, exact bitrate, sync/async rejection")


def _backend_limits(client: DevApiClient, page, out_dir: str) -> None:
    if page:
        page.wait_for_function("() => NtRecorder.ready()")
        page.evaluate("""() => {
            window.savedRecorderOpen = NtRecorder.open;
            NtRecorder.open = (...args) => { window.receivedBitrate = args[5]; return false; };
        }""")
        try:
            assert _error_code(client, "record.start", {"name": "unused.mp4", "fps": 30, "bitrate_bps": 4_294_967_295}) == "failed"
            assert page.evaluate("() => receivedBitrate") == 4_294_967_295, "WASM bridge lost the unsigned bitrate"
        finally:
            page.evaluate("() => { NtRecorder.open = savedRecorderOpen; delete window.savedRecorderOpen; }")
        _web_config_checks(page)
    else:
        path = Path(out_dir, "record_overflow.mp4")
        path.write_bytes(b"untouched output")
        assert _error_code(client, "record.start", {"name": str(path), "fps": 30, "bitrate_bps": 2_147_483_648}) == "failed"
        assert path.read_bytes() == b"untouched output", "rejected bitrate changed the output file"
    print("PASS backend bitrate boundary")


def run(client: DevApiClient, out_dir: str) -> None:
    for params in ({"name": "x.mp4", "fps": 0, "bitrate_bps": 50_000_000},
                   {"name": "x.mp4", "fps": 121, "bitrate_bps": 50_000_000},
                   {"name": "", "fps": 5, "bitrate_bps": 50_000_000},
                   {"name": "x.mp4", "fps": 30}):
        assert _error_code(client, "record.start", params) == "bad_params", params
    for bitrate in (None, True, "100000000", 0, -1, 0.5, 4_294_967_296):
        params = {"name": "x.mp4", "fps": 30, "bitrate_bps": bitrate}
        assert _error_code(client, "record.start", params) == "bad_params", params
    page = getattr(client.transport, "page", None)
    _backend_limits(client, page, out_dir)
    downloads = []
    if page:
        page.on("download", lambda d: downloads.append(d))
    _real_time(client, page, out_dir)
    if not page:
        _real_time_file(client, out_dir)
    _lockstep(client, page, downloads, out_dir)


if __name__ == "__main__":
    sys.exit(launch.main(run))
