#!/usr/bin/env python3
"""Check an nt_recorder MP4: one H.264 and one AAC/Opus stream, the expected frame rate and count, both
streams starting at 0, and A/V sync of a flash+beep probe (each white flash onset, at its decoded
timestamp, has a beep onset within one frame).

Needs ffprobe and ffmpeg on PATH (or in FFMPEG_DIR). Stdlib only.

    python tools/devapi/check_recording.py clip.mp4 --fps 30 --frames 90 [--real-time]

--real-time: audio may run up to a frame period past the video (pushed after the last deadline).
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys


def tool(name):
    d = os.environ.get("FFMPEG_DIR")
    path = shutil.which(name, path=d) if d else shutil.which(name)
    if not path:
        sys.exit(f"check_recording: {name} not found (PATH or FFMPEG_DIR)")
    return path


def run(args):
    return subprocess.run(args, check=True, capture_output=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mp4")
    ap.add_argument("--fps", type=int, required=True)
    ap.add_argument("--frames", type=int, required=True)
    ap.add_argument("--real-time", action="store_true")
    a = ap.parse_args()

    probe = json.loads(run([tool("ffprobe"), "-v", "error", "-count_frames", "-show_entries",
                            "stream=codec_type,codec_name,avg_frame_rate,nb_read_frames,sample_rate,channels,duration,start_time", "-of", "json", a.mp4]).stdout)
    streams = probe["streams"]
    video = [s for s in streams if s["codec_type"] == "video"]
    audio = [s for s in streams if s["codec_type"] == "audio"]
    errors = []
    if len(video) != 1 or video[0]["codec_name"] != "h264":
        errors.append(f"expected one h264 video stream, got {video}")
    if len(audio) != 1 or audio[0]["codec_name"] not in ("aac", "opus"):
        errors.append(f"expected one aac/opus audio stream, got {audio}")
    if errors:
        sys.exit("\n".join(errors))
    if video[0]["avg_frame_rate"] != f"{a.fps}/1":
        errors.append(f"avg_frame_rate {video[0]['avg_frame_rate']} != {a.fps}/1")
    if int(video[0]["nb_read_frames"]) != a.frames:
        errors.append(f"frames {video[0]['nb_read_frames']} != {a.frames}")
    if audio[0].get("sample_rate") != "48000" or audio[0].get("channels") != 2:
        errors.append(f"audio is {audio[0].get('sample_rate')} Hz, {audio[0].get('channels')} channels; expected 48000 Hz stereo")
    # A late start (B-frames without an edit list) shifts the whole stream against the other.
    for s in (video[0], audio[0]):
        if abs(float(s.get("start_time", 0))) > 0.5 / a.fps:
            errors.append(f"{s['codec_type']} starts at {s['start_time']} s, expected 0")
    # Padded to the video; the encoder rounds the end up to whole codec frames (AAC 1024 samples,
    # Opus 960); one more frame is margin. In real time audio pushed after the last deadline stays.
    over = float(audio[0]["duration"]) - a.frames / a.fps
    if over < -2048 / 48000 or over > 2048 / 48000 + (1.0 / a.fps if a.real_time else 0.0):
        errors.append(f"audio lasts {audio[0]['duration']} s, video {a.frames / a.fps:.3f} s")

    # Flash onsets: centre luma rising above 200.
    luma = run([tool("ffmpeg"), "-v", "error", "-i", a.mp4, "-vf", "crop=2:2:iw/2:ih/2,scale=1:1",
                "-f", "rawvideo", "-pix_fmt", "gray", "-"]).stdout
    pts = json.loads(run([tool("ffprobe"), "-v", "error", "-select_streams", "v", "-show_entries", "frame=pts_time",
                          "-of", "json", a.mp4]).stdout)["frames"]
    times = [float(f["pts_time"]) for f in pts]
    if len(times) != len(luma):
        sys.exit(f"{len(times)} frame timestamps for {len(luma)} decoded frames")
    flashes = [times[i] for i, v in enumerate(luma) if v > 200 and (i == 0 or luma[i - 1] <= 200)]
    # Beep onsets: ends of silence, except the one at the end of the track.
    log = run([tool("ffmpeg"), "-hide_banner", "-i", a.mp4, "-af", "silencedetect=n=-40dB:d=0.2",
               "-f", "null", "-"]).stderr.decode(errors="replace")
    ends = [float(x) for x in re.findall(r"silence_end: ([0-9.]+)", log)]
    beeps = [e for e in ends if e < a.frames / a.fps - 0.1]
    if not flashes or len(flashes) != len(beeps):
        errors.append(f"flash onsets {flashes} do not pair with beep onsets {beeps}")
    else:
        worst = max(abs(f - b) for f, b in zip(flashes, beeps))
        if worst > 1.0 / a.fps:
            errors.append(f"A/V offset {worst * 1000:.1f} ms exceeds one frame; flashes {flashes}, beeps {beeps}")
        print(f"check_recording: {a.frames} frames at {a.fps} fps, {audio[0]['codec_name']}, "
              f"{len(flashes)} flash/beep pairs, worst offset {worst * 1000:.1f} ms")
    if errors:
        sys.exit("\n".join(errors))


if __name__ == "__main__":
    main()
