#!/usr/bin/env python3
"""DevAPI input demo — the live-host UAT for the input.* inject path.

Connects to a running examples/devapi_host over its devapi transport (native or web) and exercises the three
behaviors the unit tests structurally cannot cover, because only a live host runs the
full enqueue -> net_poll -> input_poll -> drain timing:

  1. Drain-race — MANUAL mode: a tap (key down@0 + up@offset) stays down across the hold
     window and releases only after stepping past it, proving the up countdown ticks only
     on a real sim-advance (frozen across idle polls).
  2. Gate cutover — set_player_enabled(false) then inject a key: input.state observes
     it pressed, then released, proving inject is orthogonal to the gate. (The "real physical device dropped
     while gated" half needs a live device and stays a manual note.)
  3. input.text — text("hi") + one sim-advance drains into the char ring; input.state{text:true}
     reads codepoints == [104,105] ('h','i').

Scheduling lives in the devapi layer (nt_input is a pure apply layer): an enqueued inject releases
into nt_input's immediate buffer ONLY on a real sim-advance, then nt_input_poll applies it. So an
enqueued inject is invisible until a step advances the frame — which is exactly what lets this UAT
machine-assert the drain-race (offset>0 release pinned to advances) over the transport.

Usage: python -m tools.devapi.scenarios.input_demo [--target native|web] (options: tools/devapi/launch.py)
Exit 0 pass, 1 failure, 2 usage / launch (tools/devapi/launch.py).
"""
import sys

from .. import launch
from ..client import DevApiClient

def _frozen_state(client: DevApiClient, **params) -> dict:
    # One round trip first: the second read lands after a MANUAL-idle tick whose poll cleared key edges
    # and text, so it proves input.state still reports the last sim-advancing poll.
    client.state()
    return client.state(**params)


def run(client: DevApiClient) -> None:
    """Drive the three workflows; raise AssertionError on any contract violation.

    The host keeps game-owned input state across clients (a disconnect resets only devapi state), so
    the keys and the player gate this run changes are restored even when it fails.
    """
    try:
        _run(client)
    finally:
        try:
            for k in ("B", "C"):  # keys this run may have left down
                client.key(k, down=False)
            client.set_player_enabled(True)
            client.set_mode("run")
            client.resume()
            client.wait_frames(1)  # apply the key-ups before the close drops the schedule
        except (DevApiResultError, ConnectionError, OSError):
            pass  # best effort: never mask the run's own failure


def _run(client: DevApiClient) -> None:
    # 1. Drain-race — a tap's release pins to the sim-advance count, not wall-clock.
    client.set_mode("manual")
    # Settle C to a known-released state: a prior session may have left a residual down/inject.
    client.key("C", down=False)
    client.step(count=1)
    assert client.state(key="C").get("down") is False, "pre-tap settle: C.down expected False"

    # Long hold makes the proof race-free: exact landing step is fragile over a live host, but the
    # up@offset countdown is deterministic (ticks only on a real advance).
    hold = 8
    client.key("C", hold=hold)  # down@0 + up@offset=hold
    client.step(count=1)
    first = _frozen_state(client, key="C")
    assert first.get("down") is True and first.get("pressed") is True, f"drain-race: C is {first!r} after the first step, expected down and pressed (inject observed, not lost)"
    client.step(count=hold // 2)  # still inside the hold window
    mid = client.state(key="C").get("down")
    assert mid is True, f"drain-race: C.down is {mid!r} mid-hold, expected True (release pinned to the hold, not applied early)"
    client.step(count=hold)  # step well past the up@hold deadline
    end = client.state(key="C").get("down")
    assert end is False, f"drain-race: C.down is {end!r} after stepping past the hold, expected False (released after the advance-pinned countdown)"
    print(f"PASS[1/3] drain-race: tap(hold={hold}) observed down, stayed down mid-hold, released after the advance-pinned countdown.")

    # 2. Gate cutover — inject is orthogonal to the gate, observed via input.state.
    off = client.set_player_enabled(False)
    assert off.get("enabled") is False, f"set_player_enabled(False) echoed {off!r}, expected enabled=False"
    client.key("B", down=True)  # inject while gated — must still flow
    client.step(count=1)
    gated_observed = _frozen_state(client, key="B")
    assert gated_observed.get("down") is True and gated_observed.get("pressed") is True, f"gated input.state(B) is {gated_observed!r}, expected down and pressed (inject orthogonal to the gate)"
    client.key("B", down=False)
    client.step(count=1)
    released = _frozen_state(client, key="B")
    assert released.get("down") is False and released.get("released") is True, f"gated input.state(B) is {released!r} after the up, expected released"
    on = client.set_player_enabled(True)
    assert on.get("enabled") is True, f"set_player_enabled(True) echoed {on!r}, expected enabled=True"
    print(
        "PASS[2/3] gate cutover: inject observed (B pressed, then released) while player disabled. "
        "NOTE: 'real physical device dropped while gated' needs a live device — manual."
    )

    # 3. input.text — codepoints reach the char ring; input.state{text} reads them back.
    # A text() inject is invisible until a step() advances; input.state then reads that advance's text.
    queued = client.text("hi")
    assert queued.get("queued") == 2, f"input.text echoed {queued!r}, expected {{queued:2}}"
    client.step(count=1)
    cps = _frozen_state(client, text=True).get("codepoints")
    assert cps == [104, 105], f"input.state(text).codepoints is {cps!r}, expected [104, 105] ('h','i') after an advancing step"
    print("PASS[3/3] input.text: codepoints == [104, 105] drained from the char ring after a sim-advance.")

    # Restore a normal live state for the next session: back to RUN, unpaused.
    client.set_mode("run")
    client.resume()

    print("PASS: input.* end-to-end over the transport — drain-race, gate cutover, and input.text all machine-asserted.")


if __name__ == "__main__":
    sys.exit(launch.main(run))
