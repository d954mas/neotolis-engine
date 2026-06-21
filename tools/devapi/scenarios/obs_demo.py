#!/usr/bin/env python3
"""DevAPI obs demo — the live-socket UAT for the observability group.

Exercises the obs command group (log / perf / entity / resource) end-to-end against a REAL running
examples/devapi_host over loopback TCP — the layer only a live socket proves: the host's per-frame
nt_metrics_sample() (D-07) feeds a real window, the log ring captures real nt_log_write output, and
the L2 handlers serialize the live L1 capabilities + reject bad bot params over the wire. The
unit/build gates cover the handlers in isolation; this UAT covers the live-host round trip.

  1. log.tail{n,level}            — last-N {level,domain,msg} array shape + the level filter.
  2. perf.snapshot                — keys present; gpu_ms is null on this host (no GPU timer, D-11);
                                    user_counters carries the host's "frames"/"cpu_ms" counters.
  3. perf.reset -> stepped run    — manual-step a window of frames so the metrics rings fill, then
     -> perf.stats               perf.stats populates the per-channel aggregate schema
                                    (samples>0, avg/median/p95/p99/p99_9 present); a bad budget_ms
                                    and an unknown channel both -> bad_params.
  4. entity.list{offset,limit}    — {total:int} + a bounded entities[] array; a negative offset
                                    -> bad_params.
  5. resource.list{include_assets}— packs[] always present; a flat assets[] only when requested.

By default this script LAUNCHES its own devapi_host subprocess on a free port, drives the obs
commands, and tears it down — so a single `python obs_demo.py` is a self-contained UAT. Point it at an
already-running host with --port N (or env NT_DEVAPI_PORT) + --no-launch.

Usage: python tools/devapi/scenarios/obs_demo.py [--port N] [--no-launch] [--host-bin PATH]
  Port resolution: --port N  >  env NT_DEVAPI_PORT  >  default 17890.
Returns exit 0 if every assertion passes, exit 1 on any failure (connect / timeout / assertion /
protocol), exit 2 on a usage / launch error. Stdlib only (socket + json + subprocess) — no pip deps.
"""
import os
import subprocess
import sys
import time

# Allow running as a plain script by making the repo root importable, so the
# absolute `tools.devapi.*` imports resolve (mirrors ui_demo.py).
if __package__ in (None, ""):
    sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..")))
    from tools.devapi.client import DevApiClient, DevApiResultError
    from tools.devapi.transport import DEFAULT_PORT, DEFAULT_READ_TIMEOUT, SocketTransport
else:
    from ..client import DevApiClient, DevApiResultError
    from ..transport import DEFAULT_PORT, DEFAULT_READ_TIMEOUT, SocketTransport

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))

# Manual-mode frame budget: large enough to fill the metrics rings so the aggregates are non-trivial,
# small enough to stay well under the host's per-step safety cap.
SAMPLE_FRAMES = 200

# The aggregate fields every populated channel/window must expose.
STATS_FIELDS = ("samples", "avg", "min", "max", "median", "p95", "p99", "p99_9")


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


def _find_host_bin(argv) -> str:
    """--host-bin PATH override, else the per-preset devapi_host built by native-debug."""
    for i, a in enumerate(argv):
        if a == "--host-bin" and i + 1 < len(argv):
            return argv[i + 1]
        if a.startswith("--host-bin="):
            return a.split("=", 1)[1]
    exe = "devapi_host.exe" if os.name == "nt" else "devapi_host"
    return os.path.join(REPO_ROOT, "build", "examples", "devapi_host", "native-debug", exe)


def _connect_with_retry(port: int, deadline_s: float = 10.0) -> SocketTransport:
    """Poll connect() until the freshly-launched host's listen socket is up (or time out)."""
    end = time.time() + deadline_s
    last_exc = None
    while time.time() < end:
        try:
            return SocketTransport("127.0.0.1", port, read_timeout=DEFAULT_READ_TIMEOUT)
        except (ConnectionError, TimeoutError, OSError) as exc:
            last_exc = exc
            time.sleep(0.1)
    raise ConnectionError(f"could not connect to 127.0.0.1:{port} within {deadline_s}s: {last_exc}")


def _assert_stats_shape(window: dict, name: str, require_samples: bool) -> None:
    """A channel/user aggregate object must carry every STATS_FIELDS key; numeric when samples>0."""
    st = window.get(name)
    assert isinstance(st, dict), f"perf.stats: channel {name!r} is {st!r}, expected an aggregate object"
    for f in STATS_FIELDS:
        assert f in st, f"perf.stats[{name}] missing field {f!r} (got {sorted(st)})"
    samples = st["samples"]
    assert isinstance(samples, int) and samples >= 0, f"perf.stats[{name}].samples is {samples!r}"
    if require_samples:
        assert samples > 0, f"perf.stats[{name}].samples is {samples}, expected > 0 after a stepped run"
        for f in ("avg", "min", "max", "median", "p95", "p99", "p99_9"):
            assert isinstance(st[f], (int, float)), f"perf.stats[{name}].{f} is {st[f]!r}, expected a number"


def check_log_tail(client: DevApiClient) -> None:
    """1. log.tail returns a newest-first {level,domain,msg} array; the level filter narrows it."""
    res = client.result("log.tail", {"n": 16})
    entries = res.get("entries")
    assert isinstance(entries, list), f"log.tail.entries is {entries!r}, expected a list"
    assert len(entries) >= 1, "log.tail returned no entries — the host seeds at least one nt_log line"
    for e in entries:
        for k in ("level", "domain", "msg"):
            assert k in e, f"log.tail entry missing {k!r} (got {sorted(e)})"
        assert e["level"] in ("info", "warn", "error"), f"log.tail level token is {e['level']!r}"
    # The level filter: error-only must be a subset (>= filter) of the unfiltered tail.
    only_err = client.result("log.tail", {"n": 16, "level": "error"}).get("entries")
    assert isinstance(only_err, list), f"log.tail(level=error).entries is {only_err!r}"
    for e in only_err:
        assert e["level"] == "error", f"log.tail(level=error) leaked a {e['level']!r} entry"
    # Bad level token -> bad_params.
    try:
        client.result("log.tail", {"level": "verbose"})
        raise AssertionError("log.tail(level=verbose) unexpectedly succeeded; expected bad_params")
    except DevApiResultError as exc:
        assert "bad_params" in str(exc), f"log.tail(bad level) raised {exc!r}, expected bad_params"
    print(f"PASS[1/5] log.tail: {len(entries)} entries, valid level tokens, level filter + bad_params hold.")


def check_perf_snapshot(client: DevApiClient) -> None:
    """2. perf.snapshot exposes the live overlay view; gpu_ms is null on this host (D-11)."""
    snap = client.result("perf.snapshot")
    for k in ("fps", "frame_ms", "cpu_ms", "draw_calls", "user_counters"):
        assert k in snap, f"perf.snapshot missing key {k!r} (got {sorted(snap)})"
    assert "gpu_ms" in snap, "perf.snapshot missing the gpu_ms key"
    assert snap["gpu_ms"] is None, f"perf.snapshot.gpu_ms is {snap['gpu_ms']!r}, expected null (no GPU timer, D-11)"
    uc = snap["user_counters"]
    assert isinstance(uc, dict), f"perf.snapshot.user_counters is {uc!r}, expected an object"
    # The host (Task 1) exercises a count("frames") + count_f("cpu_ms") every frame.
    assert "frames" in uc, f"perf.snapshot.user_counters missing the host 'frames' counter (got {sorted(uc)})"
    # The host writes count_f("cpu_ms") every frame too, so it is an unconditional contract.
    assert "cpu_ms" in uc, f"perf.snapshot.user_counters missing the host 'cpu_ms' counter (got {sorted(uc)})"
    print(f"PASS[2/5] perf.snapshot: keys present, gpu_ms is null (D-11), user_counters={sorted(uc)}.")


def check_perf_stats(client: DevApiClient) -> None:
    """3. perf.reset -> stepped run -> perf.stats: the windowed aggregate schema populates."""
    client.set_mode("manual")
    client.result("perf.reset")
    client.step(count=SAMPLE_FRAMES)  # fill the rings with real per-frame samples

    stats = client.result("perf.stats", {"budget_ms": 16.67})
    channels = stats.get("channels")
    assert isinstance(channels, dict), f"perf.stats.channels is {channels!r}, expected an object"
    assert "frame_ms" in channels, f"perf.stats.channels missing 'frame_ms' (got {sorted(channels)})"
    _assert_stats_shape(channels, "frame_ms", require_samples=True)
    for top in ("user_channels", "fps_low_1pct", "fps_low_01pct", "over_budget_pct", "budget_ms"):
        assert top in stats, f"perf.stats missing top-level key {top!r} (got {sorted(stats)})"
    # The host's count_f("cpu_ms") -> a user channel every frame; assert its aggregate schema too. This
    # is an unconditional contract, not a best-effort check (the host writes cpu_ms on every frame).
    user = stats["user_channels"]
    assert isinstance(user, dict), f"perf.stats.user_channels is {user!r}, expected an object"
    assert "cpu_ms" in user, f"perf.stats.user_channels missing the host 'cpu_ms' channel (got {sorted(user)})"
    _assert_stats_shape(user, "cpu_ms", require_samples=True)

    fm = channels["frame_ms"]
    print(
        f"PASS[3/5] perf.stats after step({SAMPLE_FRAMES}): frame_ms samples={fm['samples']} "
        f"avg={fm['avg']:.3f} median={fm['median']:.3f} p95={fm['p95']:.3f} p99={fm['p99']:.3f} "
        f"(observed, not pinned); over_budget_pct={stats['over_budget_pct']:.2f}."
    )

    # Bad budget_ms -> bad_params (finite + > 0 contract).
    try:
        client.result("perf.stats", {"budget_ms": -1.0})
        raise AssertionError("perf.stats(budget_ms=-1) unexpectedly succeeded; expected bad_params")
    except DevApiResultError as exc:
        assert "bad_params" in str(exc), f"perf.stats(bad budget) raised {exc!r}, expected bad_params"
    # Unknown channel -> bad_params.
    try:
        client.result("perf.stats", {"channels": ["not_a_channel"]})
        raise AssertionError("perf.stats(channels=[not_a_channel]) unexpectedly succeeded; expected bad_params")
    except DevApiResultError as exc:
        assert "bad_params" in str(exc), f"perf.stats(unknown channel) raised {exc!r}, expected bad_params"
    print("PASS[3b/5] perf.stats bad budget_ms + unknown channel -> bad_params.")

    # Restore RUN so a follow-on session is not left frozen in MANUAL.
    client.set_mode("run")
    client.resume()


def check_entity_list(client: DevApiClient) -> None:
    """4. entity.list returns {total:int} + a bounded array; a negative offset -> bad_params."""
    res = client.result("entity.list", {"offset": 0, "limit": 8})
    total = res.get("total")
    entities = res.get("entities")
    assert isinstance(total, int) and total >= 0, f"entity.list.total is {total!r}, expected a non-negative int"
    assert isinstance(entities, list), f"entity.list.entities is {entities!r}, expected a list"
    assert len(entities) <= 8, f"entity.list returned {len(entities)} entities, exceeds limit=8"
    # entity.list is fully paginated against the honest total; there is no truncated flag.
    assert "truncated" not in res, f"entity.list still returned a truncated flag: {res.get('truncated')!r}"
    try:
        client.result("entity.list", {"offset": -1})
        raise AssertionError("entity.list(offset=-1) unexpectedly succeeded; expected bad_params")
    except DevApiResultError as exc:
        assert "bad_params" in str(exc), f"entity.list(bad offset) raised {exc!r}, expected bad_params"
    print(f"PASS[4/5] entity.list: total={total}, bounded array (got {len(entities)}), bad offset -> bad_params.")


def check_resource_list(client: DevApiClient) -> None:
    """5. resource.list always returns packs[]; assets[] appears flat only when include_assets."""
    base = client.result("resource.list")
    assert isinstance(base.get("packs"), list), f"resource.list.packs is {base.get('packs')!r}, expected a list"
    assert isinstance(base.get("total"), int), f"resource.list.total is {base.get('total')!r}, expected an int"
    assert "assets" not in base, "resource.list omitted include_assets but still returned an assets[]"
    with_assets = client.result("resource.list", {"include_assets": True})
    assert isinstance(with_assets.get("packs"), list), "resource.list(include_assets).packs missing"
    assert isinstance(with_assets.get("assets"), list), "resource.list(include_assets) did not return a flat assets[]"
    # The flat assets[] is DoS-capped; asset_total + assets_truncated report the real count vs cap.
    assert isinstance(with_assets.get("asset_total"), int), f"resource.list.asset_total is {with_assets.get('asset_total')!r}, expected an int"
    assert isinstance(with_assets.get("assets_truncated"), bool), f"resource.list.assets_truncated is {with_assets.get('assets_truncated')!r}, expected a bool"
    print(
        f"PASS[5/5] resource.list: packs[] always (total={base['total']}); "
        f"assets[] flat only with include_assets ({len(with_assets['assets'])} assets)."
    )


def run(client: DevApiClient) -> None:
    """Drive every obs surface; raise AssertionError on any contract violation."""
    check_log_tail(client)
    check_perf_snapshot(client)
    check_perf_stats(client)
    check_entity_list(client)
    check_resource_list(client)
    print("PASS: obs.* end-to-end over the socket — log/perf/entity/resource + the bad_params paths all machine-asserted.")


def main(argv) -> int:
    no_launch = "--no-launch" in argv
    port = resolve_port(argv)
    proc = None
    transport = None
    try:
        if not no_launch:
            host_bin = _find_host_bin(argv)
            if not os.path.isfile(host_bin):
                print(f"error: devapi_host not found at {host_bin}\n"
                      f"  build it: cmake --build build/_cmake/native-debug --target devapi_host\n"
                      f"  or attach to a running host with --no-launch --port N", file=sys.stderr)
                return 2
            env = dict(os.environ, NT_DEVAPI_PORT=str(port))
            proc = subprocess.Popen([host_bin], cwd=REPO_ROOT, env=env)
            transport = _connect_with_retry(port)
        else:
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
        if proc is not None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()


if __name__ == "__main__":
    args = sys.argv[1:]
    if "--help" in args or "-h" in args:
        print(__doc__)
        sys.exit(0)
    sys.exit(main(args))
