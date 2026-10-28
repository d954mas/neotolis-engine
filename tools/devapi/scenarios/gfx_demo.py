#!/usr/bin/env python3
"""DevAPI gfx recording demo — the live-host UAT for the gfx.* group.

Turns recording on against a real running examples/devapi_host, native or web, steps the sim in MANUAL
and reads the frame of each step by its sim frame, then freezes the ring, lists it and saves every
stored frame's capture text to the --out directory. Checks the text header, the host's PASS and CLEAR
operations, gapless sequences, unknown_frame over the wire, and that recording never advanced the sim.

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

    # 1. The frame of each step, read by its sim frame once it finishes.
    for _ in range(3):
        sim = client.step(1)["frame"]
        read = client.gfx_read(sim_frame=sim)
        text = read["text"]
        assert read["sim_frame"] == sim, f"gfx.read sim_frame {sim} returned sim_frame {read['sim_frame']}"
        assert text.startswith("ntcapture 1\nframe sequence="), f"capture text header missing: {text[:60]!r}"
        assert " OP op=PASS " in text and " OP op=CLEAR " in text, "the step frame lacks the host's pass and clear"
    print("PASS[1/4] gfx.read {sim_frame}: the frame of each of 3 steps, with the host's pass and clear.")

    # 2. Frozen: nothing records, nothing is evicted; the ring is a gapless run with the 3 steps in it.
    frame_before = client.frame_current()["frame"]
    client.gfx_record(False)
    listing = client.gfx_frames()
    frames = listing["frames"]
    assert listing["recording"] is False and frames, f"gfx.frames after off: {listing!r}"
    sequences = [f["sequence"] for f in frames]
    assert sequences == list(range(sequences[0], sequences[0] + len(sequences))), f"stored sequences have gaps: {sequences}"
    assert sum(1 for f in frames if f["advanced"]) >= 3, f"fewer than 3 advanced frames stored: {frames}"
    print(f"PASS[2/4] gfx.frames: {len(frames)} gapless frames after off, the 3 steps among them.")

    # 3. Save every stored frame; the client owns files.
    for f in frames:
        text = client.gfx_read(sequence=f["sequence"])["text"]
        with open(os.path.join(out_dir, f"gfx_frame_{f['sequence']}.ntcapture.txt"), "w", encoding="utf-8", newline="\n") as fh:
            fh.write(text)
    print(f"PASS[3/4] saved {len(frames)} frames to {out_dir}.")

    # 4. A frame before the ring is unknown_frame over the wire; recording never advanced the sim.
    try:
        client.gfx_read(sequence=sequences[0] - 1)
        raise AssertionError("gfx.read of an evicted frame unexpectedly succeeded")
    except DevApiResultError as exc:
        assert "unknown_frame" in str(exc), f"gfx.read before the ring raised {exc!r}, expected unknown_frame"
    assert client.frame_current()["frame"] == frame_before, "gfx recording advanced the MANUAL sim"
    print("PASS[4/4] unknown_frame before the ring; the sim stayed frozen.")

    client.set_mode("run")
    print("PASS: gfx.* end-to-end over the transport — recording mode, step-frame reads, ring listing and saved capture text.")


if __name__ == "__main__":
    sys.exit(launch.main(run))
