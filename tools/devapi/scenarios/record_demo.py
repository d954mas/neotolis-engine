#!/usr/bin/env python3
"""Native record.* check: drives a running Windows examples/devapi_host built with
NT_DEVAPI_GROUP_RECORDER over loopback TCP, records one lockstep and one real-time clip of its
flash+beep probe, and checks each MP4 with check_recording.py.

  1. lockstep — MANUAL, start half a second off a flash, 360 steps of 1/60 at fps 30: 180 frames.
  2. real time — RUN for 10 s of game time at fps 30.

Usage: python tools/devapi/scenarios/record_demo.py [--port N] [--out DIR]
  Port: --port N > env NT_DEVAPI_PORT > default 17890. Files go to DIR (default: the working directory).
Exit 0 when both clips pass; needs ffmpeg/ffprobe (PATH or FFMPEG_DIR).
"""
import argparse
import math
import os
import subprocess
import sys

if __package__ in (None, ""):
    sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..")))
    from tools.devapi.client import DevApiClient
    from tools.devapi.transport import DEFAULT_PORT, DEFAULT_READ_TIMEOUT, SocketTransport
else:
    from ..client import DevApiClient
    from ..transport import DEFAULT_PORT, DEFAULT_READ_TIMEOUT, SocketTransport

CHECK = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "check_recording.py")


def check(path, fps, frames, real_time):
    args = [sys.executable, CHECK, path, "--fps", str(fps), "--frames", str(frames)]
    subprocess.run(args + (["--real-time"] if real_time else []), check=True)


def lockstep(client, out):
    path = os.path.join(out, "record_lockstep.mp4")
    client.set_mode("manual")
    # Half a second off a flash, so the first and last flash/beep pairs are whole.
    time = client.frame_current()["time"]
    steps = round(((math.floor(time) + 1.5 - time) % 1) * 60)
    if steps:
        client.step(steps)
    client.result("record.start", {"name": path, "fps": 30})
    for _ in range(12):  # one request per 30 steps keeps a slow encoder inside the read timeout
        client.step(30)
    frames = client.result("record.stop")["frames"]
    assert frames == 180, f"lockstep: {frames} frames, expected 180"
    check(path, 30, frames, False)


def real_time(client, out):
    path = os.path.join(out, "record_run.mp4")
    client.set_mode("run")
    client.resume()
    client.result("record.start", {"name": path, "fps": 30})
    client.wait_game_seconds(10.0)
    frames = client.result("record.stop")["frames"]
    assert frames > 0, "real time: no frames"
    check(path, 30, frames, True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=int(os.environ.get("NT_DEVAPI_PORT", DEFAULT_PORT)))
    ap.add_argument("--out", default=os.getcwd())
    a = ap.parse_args()
    transport = SocketTransport("127.0.0.1", a.port, read_timeout=DEFAULT_READ_TIMEOUT)
    try:
        client = DevApiClient(transport)
        out = os.path.abspath(a.out)
        lockstep(client, out)
        real_time(client, out)
    finally:
        transport.close()
    print("PASS: lockstep and real-time clips")


if __name__ == "__main__":
    main()
