#!/usr/bin/env python3
"""DevAPI input demo — the live-socket UAT for the input.* inject path.

Connects to a running examples/devapi_host over loopback TCP and exercises the three
behaviors the unit tests structurally cannot cover, because only a live socket runs the
full enqueue -> net_poll -> input_poll -> drain timing:

  1. Drain-race — MANUAL mode: a tap (key down@0 + up@offset) stays down across the hold
     window and releases only after stepping past it, proving the up countdown ticks only
     on a real sim-advance (frozen across idle polls).
  2. Gate cutover — set_player_enabled(false) then inject a key: input.state still observes
     it down, proving inject is orthogonal to the gate. (The "real physical device dropped
     while gated" half needs a live device and stays a manual note.)
  3. input.text — text("hi") + one sim-advance drains into the char ring; input.state{pop_text:true}
     reads codepoints == [104,105] ('h','i').

Scheduling lives in the devapi layer (nt_input is a pure apply layer): an enqueued inject releases
into nt_input's immediate buffer ONLY on a real sim-advance, then nt_input_poll applies it. So an
enqueued inject is invisible until a step advances the frame — which is exactly what lets this UAT
machine-assert the drain-race (offset>0 release pinned to advances) over the socket.

Usage: python tools/devapi/scenarios/input_demo.py [--port N]
  Port resolution: --port N  >  env NT_DEVAPI_PORT  >  default 17890.
Returns exit 0 if every observable assertion passes, exit 1 on any failure (connect /
assertion / protocol). Stdlib only (socket + json) — no pip deps.
"""
import os
import sys
import time

# Allow running as a plain script by making the repo root importable, so the
# absolute `tools.devapi.*` imports resolve (mirrors time_render_demo.py).
if __package__ in (None, ""):
    sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..")))
    from tools.devapi.client import DevApiClient, DevApiResultError
    from tools.devapi.transport import DEFAULT_PORT, SocketTransport
else:
    from ..client import DevApiClient, DevApiResultError
    from ..transport import DEFAULT_PORT, SocketTransport


def _parse_port(raw: str) -> int:
    """int() + range-check; clean exit(2) on garbage instead of a raw traceback."""
    try:
        port = int(raw)
    except ValueError:
        print("error: --port must be an integer 1..65535", file=sys.stderr)
        sys.exit(2)
    if not (1 <= port <= 65535):
        print("error: --port must be an integer 1..65535", file=sys.stderr)
        sys.exit(2)
    return port


def resolve_port(argv) -> int:
    """--port N (or positional) > env NT_DEVAPI_PORT > DEFAULT_PORT."""
    for i, a in enumerate(argv):
        if a == "--port" and i + 1 < len(argv):
            return _parse_port(argv[i + 1])
        if a.startswith("--port="):
            return _parse_port(a.split("=", 1)[1])
    env = os.environ.get("NT_DEVAPI_PORT")
    if env:
        return _parse_port(env)
    for a in argv:
        if a.isdigit():
            return _parse_port(a)
    return DEFAULT_PORT


def _frozen_state(client: DevApiClient, **params) -> dict:
    # Let MANUAL-idle ticks run first: their polls clear key edges and text, so the read proves that
    # input.state still reports the last sim-advancing poll.
    time.sleep(0.1)
    return client.state(**params)


def run(client: DevApiClient) -> None:
    """Drive the three workflows; raise AssertionError on any contract violation."""
    # 1. Drain-race — a tap's release pins to the sim-advance count, not wall-clock.
    client.set_mode("manual")
    # Settle C to a known-released state: a prior session may have left a residual down/inject.
    client.key("C", down=False)
    client.step(count=1)
    assert client.state(key="C").get("down") is False, "pre-tap settle: C.down expected False"

    # Long hold makes the proof race-free: exact landing step is fragile over a live socket, but the
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

    # 3. input.text — codepoints reach the char ring; the pop_text drain reads them back.
    # A text() inject is invisible until a step() advances; input.state then reads that advance's text.
    queued = client.text("hi")
    assert queued.get("queued") == 2, f"input.text echoed {queued!r}, expected {{queued:2}}"
    client.step(count=1)
    cps = _frozen_state(client, pop_text=True).get("codepoints")
    assert cps == [104, 105], f"input.state(pop_text).codepoints is {cps!r}, expected [104, 105] ('h','i') after an advancing step"
    print("PASS[3/3] input.text: codepoints == [104, 105] drained from the char ring after a sim-advance.")

    # Restore a normal live state for the next session: back to RUN, unpaused.
    client.set_mode("run")
    client.resume()

    print("PASS: input.* end-to-end over the socket — drain-race, gate cutover, and input.text all machine-asserted.")


def main(port: int) -> int:
    transport = None
    try:
        transport = SocketTransport("127.0.0.1", port)
        client = DevApiClient(transport)
        run(client)
        return 0
    except AssertionError as exc:
        print(f"FAIL: assertion: {exc}")
        return 1
    except (DevApiResultError, ValueError) as exc:
        print(f"FAIL: protocol error: {exc}")
        return 1
    except (ConnectionError, TimeoutError, OSError) as exc:
        print(f"FAIL: transport error talking to 127.0.0.1:{port}: {exc}")
        return 1
    finally:
        # Best-effort restore so an assertion failure mid-run can't leave the long-lived host
        # MANUAL / paused / gated for the next session. Must not mask the original failure/exit
        # code, so any restore error is swallowed.
        if transport is not None:
            try:
                client = DevApiClient(transport)
                for k in ("B", "C"):  # keys this run may have left down
                    client.key(k, down=False)
                client.set_player_enabled(True)
                client.set_mode("run")
                client.resume()
            except (DevApiResultError, ValueError, ConnectionError, TimeoutError, OSError):
                pass
            transport.close()


if __name__ == "__main__":
    args = sys.argv[1:]
    if "--help" in args or "-h" in args:
        print(__doc__)
        sys.exit(0)
    sys.exit(main(resolve_port(args)))
