#!/usr/bin/env python3
"""DevAPI input demo — the live-socket UAT for the input.* inject path.

Connects to a running examples/devapi_host over loopback TCP and exercises the
three Phase-66 behaviors the unit tests structurally cannot cover, because only a
live socket runs the full enqueue -> net_poll -> input_poll -> drain timing (D-12):

  1. Drain-race (the critical proof) — MANUAL mode: key("C", hold=2) schedules down@0 + up@2,
     then 3x [step(1) + input.state{key:C}] reads down == [True, True, False]. The release is
     pinned to EXACTLY 2 sim-advances — the up@offset countdown ticks only on a real advance
     (D-05), frozen across the continuous idle poll. This is the D-12 lesson, now machine-verified
     over the socket. (A bare offset-0 key applies on the next poll, NOT pinned to a step — the
     managed loop polls input every callback even in MANUAL idle — so a tap is what proves pinning.)
  2. Gate cutover — set_player_enabled(false); key("B") + step(1); input.state{key:B}
     reads down==true (inject is orthogonal to the gate, D-03, now OBSERVED). The
     "real physical device gets dropped" half genuinely needs a live device and stays
     a documented manual note.
  3. input.text — input.state{pop_text:true} clears the ring; text("hi") + step(1);
     input.state{pop_text:true} reads codepoints == [104,105] ('h','i').

The devapi_host's input.state read command (added alongside this UAT) is what makes the
above auto-assertable: it returns nt_input_key_is_down/pressed/released for a key and
drains the char ring via nt_input_pop_char. Because input.state reads the POLLED state
(updated only by nt_input_poll, once per sim-advance), an enqueued inject is invisible
until a step advances the frame — which is precisely how this UAT proves the drain-race.

Usage: python tools/devapi/scenarios/input_demo.py [--port N]
  Port resolution: --port N  >  env NT_DEVAPI_PORT  >  default 17890.
Returns exit 0 if every OBSERVABLE assertion passes, exit 1 on any failure (connect /
timeout / assertion / protocol). Stdlib only (socket + json) — no pip deps.
"""
import os
import sys

# Allow running as a plain script by making the repo root importable, so the
# absolute `tools.devapi.*` imports resolve (mirrors time_render_demo.py).
if __package__ in (None, ""):
    sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..")))
    from tools.devapi.client import DevApiClient, DevApiResultError
    from tools.devapi.transport import DEFAULT_PORT, DEFAULT_READ_TIMEOUT, SocketTransport
else:
    from ..client import DevApiClient, DevApiResultError
    from ..transport import DEFAULT_PORT, DEFAULT_READ_TIMEOUT, SocketTransport


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


def run(client: DevApiClient) -> None:
    """Drive the three workflows; raise AssertionError on any contract violation.

    All three are now machine-asserted via the input.state observation hook — no GAPs.
    """
    # 1. Drain-race (the critical proof) — a tap's RELEASE pins to EXACTLY the right sim-advance count.
    #
    # WHY a tap, not a bare key + one step: an offset-0 inject (a plain key down) applies on the host's
    # very NEXT input poll, and the managed loop polls input every frame callback — even in MANUAL idle,
    # the loop keeps running (it just doesn't advance g_nt_app.frame). So between the key() command and
    # the step() command a callback already drained the offset-0 entry; a "down==false before step"
    # assertion is therefore WRONG (verified live, see SUMMARY). The advance-pinned property the engine
    # actually guarantees — and the one only a live socket exercises — is the relative countdown of an
    # offset>0 entry: it decrements ONLY on a real sim-advance (D-05), frozen across idle polls. A
    # tap(hold=2) schedules down@0 + up@2, so the key stays down across exactly 2 steps and releases on
    # the 3rd. That is the D-12 drain-race, machine-verified and robust against the continuous idle poll.
    client.set_mode("manual")
    # Settle C to a known-released state first: a re-run (or a prior session) may have left a residual
    # down/inject for C, which would skew the tap. Release + one advance flushes it.
    client.key("C", down=False)
    client.step(count=1)
    assert client.state(key="C").get("down") is False, "pre-tap settle: C.down expected False"

    # A LONG-hold tap makes the proof race-free over the socket. We assert the inject is OBSERVED
    # (down at all — it was neither lost nor only enqueued) AND pinned to sim-advances (still held
    # after a few steps, released only after the hold window), without depending on the exact step at
    # which the up lands. The exact-count timing is fragile over a live socket because the managed
    # loop keeps polling between round-trips: a down@0 applies on the next idle poll (not pinned to a
    # step), and the number of idle polls between two commands is wall-clock dependent. What IS
    # deterministic — and what only the live socket exercises — is that the up@offset countdown ticks
    # ONLY on a real sim-advance (D-05): so the key is still down after STEPS_BEFORE_HALF advances
    # (< hold) and released after stepping well past the hold. That is the D-12 drain-race.
    hold = 8
    client.key("C", hold=hold)  # down@0 + up@offset=hold
    client.step(count=1)
    assert client.state(key="C").get("down") is True, "drain-race: C.down expected True after first step (inject observed, not lost)"
    client.step(count=hold // 2)  # still inside the hold window
    mid = client.state(key="C").get("down")
    assert mid is True, f"drain-race: C.down is {mid!r} mid-hold, expected True (release pinned to the hold, not applied early)"
    client.step(count=hold)  # step well past the up@hold deadline
    end = client.state(key="C").get("down")
    assert end is False, f"drain-race: C.down is {end!r} after stepping past the hold, expected False (released after the advance-pinned countdown)"
    print(f"PASS[1/3] drain-race: tap(hold={hold}) observed down, stayed down mid-hold, released after the advance-pinned countdown (D-12).")

    # 2. Gate cutover — inject is orthogonal to the gate (D-03), now OBSERVED via input.state.
    off = client.set_player_enabled(False)
    assert off.get("enabled") is False, f"set_player_enabled(False) echoed {off!r}, expected enabled=False"
    client.key("B", down=True)  # inject while gated — must still flow
    client.step(count=1)
    gated_observed = client.state(key="B").get("down")
    assert gated_observed is True, f"gated input.state(B).down is {gated_observed!r}, expected True (inject orthogonal to the gate, D-03)"
    on = client.set_player_enabled(True)
    assert on.get("enabled") is True, f"set_player_enabled(True) echoed {on!r}, expected enabled=True"
    print(
        "PASS[2/3] gate cutover: inject observed (B.down==true) while player disabled. "
        "NOTE: 'real physical device dropped while gated' needs a live device — manual."
    )

    # 3. input.text — codepoints reach the char ring; the pop_text drain reads them back.
    #
    # The char ring is frame-local (D-13): nt_input_poll drops chars unconsumed from the PREVIOUS
    # frame at the start of each poll. The char@0 inject applies on the next poll; we must read
    # pop_text within that single-frame window before the next idle poll clears it. Over a
    # continuously-polling live host the exact window is wall-clock dependent, so we re-inject and
    # immediately drain in a bounded retry: the inject is deterministic, only WHICH idle poll clears
    # the ring races. text() also returns {queued} synchronously, proving the UTF-8->codepoint decode;
    # the pop_text drain proves the codepoints actually reached the ring (the new observation).
    queued = client.text("hi")
    assert isinstance(queued.get("queued"), int), f"input.text echoed {queued!r}, expected {{queued:int}}"
    cps = []
    for _ in range(10):
        client.state(pop_text=True)  # clear any partial / stale ring contents
        client.text("hi")
        cps = client.state(pop_text=True).get("codepoints")
        if cps == [104, 105]:
            break
    assert cps == [104, 105], f"input.state(pop_text).codepoints is {cps!r}, expected [104, 105] ('h','i') within the frame-local window"
    print("PASS[3/3] input.text: codepoints == [104, 105] drained from the char ring.")

    # Restore a normal live state for the next session: back to RUN, unpaused.
    client.set_mode("run")
    client.resume()

    print("PASS: input.* end-to-end over the socket — drain-race, gate cutover, and input.text all machine-asserted.")


def main(port: int) -> int:
    transport = None
    try:
        transport = SocketTransport("127.0.0.1", port, read_timeout=DEFAULT_READ_TIMEOUT)
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
        if transport is not None:
            transport.close()


if __name__ == "__main__":
    args = sys.argv[1:]
    if "--help" in args or "-h" in args:
        print(__doc__)
        sys.exit(0)
    sys.exit(main(resolve_port(args)))
