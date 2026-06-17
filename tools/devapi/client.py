"""DevApiClient — the reusable harness surface.

request / result / batch / wait_frames / step over a pluggable Transport.
Replies are correlated by request_id via a pending-map, never by arrival order
(deferred / pipelined results interleave on one connection).

Stdlib only (json + socket) — no pip deps. Python 3.8+.
"""
import json
from typing import Any, Dict, List, Optional

from .transport import Transport


class DevApiResultError(RuntimeError):
    """Raised by result() when the server returns {ok:false}, surfacing error.code/message."""


class DevApiClient:
    """Blocking request/response over a Transport, correlated by request_id.

    Each request gets a monotonic id. The read loop returns the matching reply
    and stashes any non-matching reply (an out-of-order deferred/pipelined result)
    in a pending-map for a later request that owns that id.
    """

    def __init__(self, transport: Transport) -> None:
        self._transport = transport
        self._next_id = 1
        # request_id -> already-received response object waiting to be claimed.
        self._pending: Dict[Any, Dict[str, Any]] = {}

    def _alloc_id(self) -> int:
        rid = self._next_id
        self._next_id += 1
        return rid

    def _recv_until(self, rid: Any) -> Dict[str, Any]:
        """Return the response whose request_id == rid, stashing others by id."""
        # Already arrived earlier (out of order)? Claim it without reading the wire.
        if rid in self._pending:
            return self._pending.pop(rid)
        while True:
            try:
                line = self._transport.recv_line()
            except TimeoutError as exc:
                # Name the pending request_id so a hang is diagnosable, not silent.
                raise TimeoutError(f"no response for request_id={rid}") from exc
            try:
                obj = json.loads(line)
            except ValueError as exc:
                raise ValueError(f"protocol error: invalid JSON from server: {line!r}") from exc
            obj_id = obj.get("request_id")
            if obj_id == rid:
                return obj
            # A reply for a different request (deferred/pipelined) — stash and keep reading.
            # Never key on None: an unidentified reply can't be claimed by any request.
            if obj_id is not None:
                self._pending[obj_id] = obj

    def request(self, method: str, params: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        """Send one command, block until its correlated reply arrives, return the raw envelope."""
        rid = self._alloc_id()
        msg: Dict[str, Any] = {"method": method, "request_id": rid}
        if params is not None:
            msg["params"] = params
        self._transport.send(json.dumps(msg))
        return self._recv_until(rid)

    def result(self, method: str, params: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        """request() + assert ok; return the result object, raising on {ok:false}."""
        resp = self.request(method, params)
        if resp.get("ok") is not True:
            err = resp.get("error") or {}
            code = err.get("code", "unknown")
            message = err.get("message", "(no message)")
            raise DevApiResultError(f"{method} failed: {code}: {message}")
        return resp.get("result", {})

    def batch(self, calls: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
        """Send one JSON-array line, receive one array reply, return entries in order.

        Each entry carries its OWN request_id so the core's per-entry echoed ids
        are distinct and individually correlatable (a single shared id would
        collide). The reply array is returned in submission order.
        """
        ids = [self._alloc_id() for _ in calls]
        arr = []
        for rid, c in zip(ids, calls):
            entry: Dict[str, Any] = {"method": c["method"], "request_id": rid}
            if c.get("params") is not None:
                entry["params"] = c["params"]
            arr.append(entry)
        self._transport.send(json.dumps(arr))
        while True:
            try:
                line = self._transport.recv_line()
            except TimeoutError as exc:
                raise TimeoutError(f"no response for batch request_ids={ids}") from exc
            try:
                obj = json.loads(line)
            except ValueError as exc:
                raise ValueError(f"protocol error: invalid JSON from server: {line!r}") from exc
            if isinstance(obj, list):
                return obj
            # A non-array line is an out-of-order single reply — stash by its id.
            rid_obj = obj.get("request_id")
            if rid_obj is not None:
                self._pending[rid_obj] = obj

    def wait_frames(self, n: int) -> Dict[str, Any]:
        """Block until `n` simulation frames have ADVANCED (not loop iterations).

        Deferred command — uses request() so the correlation loop handles the yield;
        PAUSE never advances the sim, so this fails fast at the cap (resume/step first).
        """
        return self.request("frame.wait", {"frames": n})

    def step(self, count: int = 1) -> Dict[str, Any]:
        """Advance exactly `count` fixed-dt sim frames (lockstep); requires manual mode.

        Maps to time.step{count}; deterministic — no wall clock, no max_dt clamp.
        """
        return self.result("time.step", {"count": count})

    def pause(self) -> Dict[str, Any]:
        """Zero the simulation dt; the frame callback keeps running (transport poll, bookkeeping)."""
        return self.result("time.pause")

    def resume(self) -> Dict[str, Any]:
        """Clear the pause flag so the loop advances on wall time again (idempotent)."""
        return self.result("time.resume")

    def set_scale(self, scale: float) -> Dict[str, Any]:
        """Multiply dt for slow-mo / fast-forward OBSERVATION only — not a determinism primitive."""
        return self.result("time.set_scale", {"scale": scale})

    def set_mode(self, mode: str) -> Dict[str, Any]:
        """Switch the managed loop between 'run' and 'manual' (lockstep) modes."""
        return self.result("time.set_mode", {"mode": mode})

    def set_fps(self, fps: float) -> Dict[str, Any]:
        """Cap the loop at `fps` frames/sec; fps==0 uncaps (needs vsync OFF on the host)."""
        return self.result("time.set_fps", {"fps": fps})

    def render_set_enabled(self, enabled: bool) -> Dict[str, Any]:
        """Toggle the render pass; disabled => the host skips draw+swap (draw_calls -> 0)."""
        return self.result("render.set_enabled", {"enabled": enabled})

    def render_info(self) -> Dict[str, Any]:
        """Read {enabled, draw_calls} — draw_calls is 0 while render is disabled (TIME-04)."""
        return self.result("render.info")

    def frame_current(self) -> Dict[str, Any]:
        """Read the current {frame, time, dt} from the managed loop's g_nt_app state."""
        return self.result("frame.current")

    def close(self) -> None:
        self._transport.close()
