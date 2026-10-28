#!/usr/bin/env python3
"""DevAPI obs demo — the live-host UAT for the observability group.

Exercises the obs command group (log / perf / entity / resource) end-to-end against a REAL running
examples/devapi_host over its devapi transport (native or web) — the layer only a live host proves: the host's per-frame
nt_metrics_sample() feeds a real window, the log ring captures real nt_log_write output, and
the L2 handlers serialize the live L1 capabilities + reject bad bot params over the wire. The
unit/build gates cover the handlers in isolation; this UAT covers the live-host round trip.

Usage: python -m tools.devapi.scenarios.obs_demo [--target native|web] (options: tools/devapi/launch.py)
Exit 0 pass, 1 failure, 2 usage / launch (tools/devapi/launch.py).
"""
import sys

from .. import launch
from ..client import DevApiClient, DevApiResultError

# Manual-mode frame budget: large enough to fill the metrics rings so the aggregates are non-trivial.
SAMPLE_FRAMES = 200

# The aggregate fields every populated channel/window must expose.
STATS_FIELDS = ("samples", "avg", "min", "max", "median", "p95", "p99", "p99_9")


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
    """2. perf.snapshot exposes the live nt_metrics current-frame snapshot; gpu_ms is null on this host (no GPU timer)."""
    snap = client.result("perf.snapshot")
    for k in ("fps", "frame_ms", "cpu_ms", "draw_calls", "user_counters"):
        assert k in snap, f"perf.snapshot missing key {k!r} (got {sorted(snap)})"
    assert "gpu_ms" in snap, "perf.snapshot missing the gpu_ms key"
    assert snap["gpu_ms"] is None, f"perf.snapshot.gpu_ms is {snap['gpu_ms']!r}, expected null (no GPU timer)"
    uc = snap["user_counters"]
    assert isinstance(uc, dict), f"perf.snapshot.user_counters is {uc!r}, expected an object"
    # The host exercises a count("frames") + count_f("frame_cpu_ms") every frame.
    assert "frames" in uc, f"perf.snapshot.user_counters missing the host 'frames' counter (got {sorted(uc)})"
    # The host writes count_f("frame_cpu_ms") every frame too (named so it does not shadow the fixed
    # cpu_ms channel), so it is an unconditional contract.
    assert "frame_cpu_ms" in uc, f"perf.snapshot.user_counters missing the host 'frame_cpu_ms' counter (got {sorted(uc)})"
    print(f"PASS[2/5] perf.snapshot: keys present, gpu_ms is null, user_counters={sorted(uc)}.")


def check_perf_stats(client: DevApiClient) -> None:
    """3. perf.reset -> stepped run -> perf.stats: the windowed aggregate schema populates."""
    client.set_mode("manual")
    # Restore RUN no matter how the body exits — an assert/protocol error mid-body must not leave an
    # attached host (the --port path shares one) frozen in MANUAL.
    try:
        client.result("perf.reset")
        client.step(count=SAMPLE_FRAMES)  # fill the rings with real per-frame samples

        stats = client.result("perf.stats", {"budget_ms": 16.67})
        channels = stats.get("channels")
        assert isinstance(channels, dict), f"perf.stats.channels is {channels!r}, expected an object"
        # frame_ms + the memory channels the host pushes every frame must all populate.
        for ch in ("frame_ms", "mem_used", "scratch_hwm", "scratch_used"):
            assert ch in channels, f"perf.stats.channels missing {ch!r} (got {sorted(channels)})"
            _assert_stats_shape(channels, ch, require_samples=True)
        for top in ("user_channels", "fps_low_1pct", "fps_low_01pct", "over_budget_pct", "budget_ms"):
            assert top in stats, f"perf.stats missing top-level key {top!r} (got {sorted(stats)})"
        # The host's count_f("frame_cpu_ms") -> a user channel every frame; assert its aggregate schema
        # too. This is an unconditional contract (the host writes it on every frame).
        user = stats["user_channels"]
        assert isinstance(user, dict), f"perf.stats.user_channels is {user!r}, expected an object"
        assert "frame_cpu_ms" in user, f"perf.stats.user_channels missing the host 'frame_cpu_ms' channel (got {sorted(user)})"
        _assert_stats_shape(user, "frame_cpu_ms", require_samples=True)

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
    finally:
        # Restore RUN so a follow-on session is not left frozen in MANUAL; best effort, so a dead host
        # does not mask the run's own failure.
        try:
            client.set_mode("run")
            client.resume()
        except Exception:  # noqa: BLE001
            pass


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


def run(client: DevApiClient, out_dir: str) -> None:
    """Drive every obs surface; raise AssertionError on any contract violation."""
    # window.__devapi (web) or the socket (native) exists before the first frame completes; the
    # snapshot reads the last completed frame's counters.
    client.wait_frames(1)
    check_log_tail(client)
    check_perf_snapshot(client)
    check_perf_stats(client)
    check_entity_list(client)
    check_resource_list(client)
    print("PASS: obs.* end-to-end over the transport — log/perf/entity/resource + the bad_params paths all machine-asserted.")


if __name__ == "__main__":
    sys.exit(launch.main(run))
