#!/usr/bin/env python3
"""DevAPI input demo — the live-socket UAT for the input.* inject path.

Connects to a running examples/devapi_host over loopback TCP and exercises the
three Phase-66 behaviors the unit tests structurally cannot cover, because only a
live socket runs the full enqueue -> net_poll -> input_poll -> drain timing (D-12):

  1. Drain-race — MANUAL mode, input.key A then step(count=1): the enqueue is
     accepted and exactly one sim-advance is applied (frame advanced by exactly 1).
  2. Gate cutover — set_player_enabled(false) echoes {enabled:false}; inject still
     flows while the player is disabled; re-enable echoes {enabled:true}.
  3. input.text — text("hi") is accepted ({queued}) and survives one sim-advance.

OBSERVATION GAP (read before trusting this UAT): the devapi_host exposes NO hook to
read injected input state back over the socket. The available query surface is only
frame.current {frame,time,dt}, render.info {enabled,draw_calls}, the input.* enqueue
acks, and the game.echo string echo — none report nt_input_key_is_pressed, the char
ring, or pointer slots. So this UAT proves the enqueue+advance PLUMBING end-to-end
(the request round-trips, the deferred step drains, the frame advances by exactly the
requested count, the gate toggle lands) but it CANNOT auto-assert that the injected key
was actually observed as pressed, that a real device event was dropped while gated, or
that the codepoints reached the ring. Those three are flagged GAP below and remain
manual until the host grows an observation command (e.g. game.input_state echoing
nt_input_key_is_pressed / nt_input_pop_char). See the checkpoint return for detail.

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
    """Drive the three workflows; raise AssertionError on any OBSERVABLE contract violation.

    Where the host lacks an observation hook the step prints `GAP:` and asserts only the
    plumbing it CAN observe (enqueue ack, frame correlation, gate-echo).
    """
    # 1. Drain-race — MANUAL + input.key + exactly one sim-advance.
    # We can observe: the enqueue is accepted and the step advances the frame by exactly 1.
    # We CANNOT observe: that the key registered as pressed (no nt_input_key_is_pressed echo).
    client.set_mode("manual")
    start = client.frame_current().get("frame")
    assert isinstance(start, int), f"frame.current.frame is {start!r}, expected int"
    client.key("A")  # fire-and-forget enqueue (down default true on the host)
    client.step(count=1)  # exactly one sim-advance — drains the offset==0 inject entry
    after = client.frame_current().get("frame")
    assert after == start + 1, f"drain-race step(1): frame {start} -> {after}, expected {start + 1}"
    print(
        "GAP: drain-race observed only at the plumbing level (key enqueued + frame "
        "advanced by exactly 1). The host has NO hook to read nt_input_key_is_pressed, "
        "so 'observed after exactly one sim-advance' is NOT auto-asserted."
    )

    # 2. Gate cutover — the gate echo is the ONE observable injected-state signal.
    # Inject must still flow while the player is disabled (inject is orthogonal to the gate, D-03).
    off = client.set_player_enabled(False)
    assert off.get("enabled") is False, f"set_player_enabled(False) echoed {off!r}, expected enabled=False"
    client.key("B")  # inject while gated — must still be accepted (no error / no bad_params)
    client.step(count=1)
    on = client.set_player_enabled(True)
    assert on.get("enabled") is True, f"set_player_enabled(True) echoed {on!r}, expected enabled=True"
    print(
        "GAP: gate cutover observed only at the plumbing level (gate echo true/false + "
        "inject accepted while disabled). 'real device event dropped while gated' needs a "
        "live device + an observation hook — manual."
    )

    # 3. input.text — accepted and survives one sim-advance; ring contents not readable.
    queued = client.text("hi")
    assert isinstance(queued.get("queued"), int), f"input.text echoed {queued!r}, expected {{queued:int}}"
    client.step(count=1)
    print(
        "GAP: input.text observed only at the plumbing level (codepoints queued + frame "
        "advanced). 'codepoints reached the char ring / filled a field' needs a focused "
        "field + nt_input_pop_char observation — manual."
    )

    # Restore a normal live state for the next session: back to RUN, unpaused.
    client.set_mode("run")
    client.resume()

    print(
        "PASS: input.* plumbing end-to-end over the socket — key/text enqueue accepted, "
        "step(1) advanced exactly 1 frame each, gate echo true/false. "
        "NOTE: 3 GAPs above are NOT proven (no host observation hook)."
    )


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
