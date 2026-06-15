"""DevApiClient — the reusable harness surface (HARNESS-01).

request / result / batch / wait_frames / step over a pluggable Transport (D-17).
Responses are correlated by request_id through a pending-map (D-20), NEVER by
arrival order — deferred results (and future pipelined batches) can interleave
out of order on the same connection, so trusting "next line = my response" would
mis-attribute a deferred result to the wrong call.

Stdlib only (json + socket) — no pip deps (D-16). Python 3.8+.
"""
import json
import socket
from typing import Any, Dict, List, Optional

from .transport import Transport


class DevApiResultError(RuntimeError):
    """Raised by result() when the server returns {ok:false}, surfacing error.code/message."""


class DevApiClient:
    """Blocking request/response over a Transport, correlated by request_id.

    Each request gets a monotonic id. The read loop returns the matching reply
    and stashes any non-matching reply (an out-of-order deferred/pipelined result)
    in a pending-map for a later request that owns that id (D-20).
    """

    def __init__(self, transport: Transport) -> None:
        self._transport = transport
        self._next_id = 1
        # request_id -> already-received response object waiting to be claimed (D-20).
        self._pending: Dict[Any, Dict[str, Any]] = {}

    def _alloc_id(self) -> int:
        rid = self._next_id
        self._next_id += 1
        return rid

    def _recv_until(self, rid: Any) -> Dict[str, Any]:
        """Return the response whose request_id == rid, stashing others by id (D-20)."""
        # Already arrived earlier (out of order)? Claim it without reading the wire.
        if rid in self._pending:
            return self._pending.pop(rid)
        while True:
            try:
                line = self._transport.recv_line()
            except (socket.timeout, TimeoutError) as exc:
                # D-19: name the pending request_id so a hang is diagnosable, not silent.
                raise TimeoutError(f"no response for request_id={rid}") from exc
            obj = json.loads(line)
            obj_id = obj.get("request_id")
            if obj_id == rid:
                return obj
            # A reply for a different request (deferred/pipelined) — stash and keep reading.
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
        """Send one JSON-array line (PROTO-07 / D-11), receive one array reply, return entries in order.

        A batch reply is a single line that is a JSON array; one rid tags the
        envelope so the array is correlated like any other response (D-20).
        """
        rid = self._alloc_id()
        arr = []
        for c in calls:
            entry: Dict[str, Any] = {"method": c["method"], "request_id": rid}
            if c.get("params") is not None:
                entry["params"] = c["params"]
            arr.append(entry)
        self._transport.send(json.dumps(arr))
        while True:
            try:
                line = self._transport.recv_line()
            except (socket.timeout, TimeoutError) as exc:
                raise TimeoutError(f"no response for batch request_id={rid}") from exc
            obj = json.loads(line)
            if isinstance(obj, list):
                return obj
            # A non-array line is an out-of-order single reply — stash by its id (D-20).
            self._pending[obj.get("request_id")] = obj

    def wait_frames(self, n: int) -> Dict[str, Any]:
        """Surface-present (HARNESS-01). The server's frame.wait command lands in Phase 65 (A5);
        at Phase 64 this returns the server's unknown_method envelope (NOT a silent no-op)."""
        return self.request("frame.wait", {"frames": n})

    def step(self) -> Dict[str, Any]:
        """Surface-present (HARNESS-01). The server's time.step command lands in Phase 65 (A5);
        at Phase 64 this returns the server's unknown_method envelope (NOT a silent no-op)."""
        return self.request("time.step")

    def close(self) -> None:
        self._transport.close()
