#!/usr/bin/env python3
"""DevAPI gfx recording demo — the live-host UAT for the gfx.* group.

Turns recording on against a real running examples/devapi_host, native or web, steps the sim in MANUAL
and reads the frame of each step, found through gfx.frames, then turns recording off, lists the ring and
saves every stored frame's capture text to the --out directory. Checks the text header, the host's PASS
and CLEAR operations, gapless sequences, unknown_frame over the wire, and that recording never advanced
the sim.

Usage: python -m tools.devapi.scenarios.gfx_demo [--target native|web] (options: tools/devapi/launch.py)
Exit 0 pass, 1 failure, 2 usage / launch (tools/devapi/launch.py).
"""
import os
import sys

from .. import launch
from ..client import DevApiClient, DevApiResultError


def run(client: DevApiClient, out_dir: str) -> None:
    """Drive the gfx recording surface; raise AssertionError on any contract violation."""
    client.set_mode("manual")
    client.gfx_record(True)
    sim_start = client.frame_current()["frame"]

    # 1. The frame of each step, found through gfx.frames and read by its sequence at once.
    for _ in range(3):
        sim = client.step(1)["frame"]
        sequence = client.gfx_step_frame(sim)
        read = client.gfx_read(sequence)
        text = read["text"]
        assert read["sequence"] == sequence and read["sim_frame"] == sim, f"step frame {sim}/{sequence} read as {read['sim_frame']}/{read['sequence']}"
        assert text.startswith("ntcapture 2\nframe sequence="), f"capture text header missing: {text[:60]!r}"
        assert " OP op=PASS " in text and " OP op=CLEAR " in text, "the step frame lacks the host's pass and clear"
    print("PASS[1/4] gfx_step_frame + gfx.read {sequence}: the frame of each of 3 steps, with the host's pass and clear.")

    # 2. Off: the ring stops changing once the frame in flight is stored, and is a gapless run.
    client.gfx_record(False)
    listing = client.gfx_frames()
    frames = listing["frames"]
    assert listing["recording"] is False and frames, f"gfx.frames after off: {listing!r}"
    sequences = [f["sequence"] for f in frames]
    assert sequences == list(range(sequences[0], sequences[0] + len(sequences))), f"stored sequences have gaps: {sequences}"
    assert client.gfx_frames() == listing, "the ring changed after off"
    print(f"PASS[2/4] gfx.frames: {len(frames)} gapless frames after off, stable.")

    # 3. Save every stored frame into one file: a capture file is its documents back to back; the client owns files.
    path = os.path.join(out_dir, "gfx_frames.ntcapture.txt")
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        for f in frames:
            fh.write(client.gfx_read(sequence=f["sequence"])["text"])
    print(f"PASS[3/4] saved {len(frames)} frames to {path}.")

    # 4. A frame before the ring is unknown_frame over the wire; the sim advanced only by the 3 steps.
    try:
        client.gfx_read(sequence=sequences[0] - 1)
        raise AssertionError("gfx.read of a frame before the ring unexpectedly succeeded")
    except DevApiResultError as exc:
        assert "unknown_frame" in str(exc), f"gfx.read before the ring raised {exc!r}, expected unknown_frame"
    assert client.frame_current()["frame"] == sim_start + 3, "gfx recording advanced the MANUAL sim"
    print("PASS[4/4] unknown_frame before the ring; the sim advanced only by the steps.")

    client.set_mode("run")
    print("PASS: gfx.* end-to-end over the transport — recording mode, step-frame reads, ring listing and saved capture text.")


if __name__ == "__main__":
    sys.exit(launch.main(run))
