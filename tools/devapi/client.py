"""DevApiClient — the reusable harness surface.

request / result / batch / wait_frames / step over a pluggable Transport.
Replies are correlated by request_id via a pending-map, never by arrival order
(deferred / pipelined results interleave on one connection).

Stdlib only (json + socket) — no pip deps. Python 3.8+.
"""
import json
from typing import Any, Dict, List, Optional, Set, Tuple

from .transport import Transport

# Default for NT_DEVAPI_STEP_MAX (engine/devapi/nt_devapi_time_internal.h): a
# larger count is rejected engine-side, orphaning any capture already sent with
# it. Builds overriding the limit via -D pass theirs to DevApiClient(step_max=).
_STEP_MAX = 1 << 20


class DevApiResultError(RuntimeError):
    """Raised by result() when the server returns {ok:false}, surfacing error.code/message."""


class DevApiClient:
    """Blocking request/response over a Transport, correlated by request_id.

    Each request gets a monotonic id. The read loop returns the matching reply
    and stashes any non-matching reply (an out-of-order deferred/pipelined result)
    in a pending-map for a later request that owns that id.
    """

    # Cap the unclaimed-reply stash: a server that never lets ids be claimed (framing desync)
    # must fail fast, not grow memory without bound.
    _MAX_PENDING = 256

    def __init__(self, transport: Transport, step_max: int = _STEP_MAX) -> None:
        # step_max mirrors the engine's NT_DEVAPI_STEP_MAX; pass the engine's
        # value here when the build overrides it with -DNT_DEVAPI_STEP_MAX.
        self._transport = transport
        self._step_max = step_max
        self._next_id = 1
        # request_id -> already-received response object waiting to be claimed.
        self._pending: Dict[Any, Dict[str, Any]] = {}
        # Replies for paired requests abandoned after their sibling failed.
        self._abandoned: Set[Any] = set()

    def _alloc_id(self) -> int:
        rid = self._next_id
        self._next_id += 1
        return rid

    def _stash(self, obj_id: Any, obj: Dict[str, Any]) -> None:
        """Hold an out-of-order reply for a later request. A None id can't be claimed -> dropped."""
        if obj_id is None:
            return
        if obj_id in self._abandoned:
            self._abandoned.remove(obj_id)
            return
        if obj_id not in self._pending and len(self._pending) >= self._MAX_PENDING:
            raise ConnectionError("pending reply map overflow — server replies not being claimed (framing desync)")
        self._pending[obj_id] = obj

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
            self._stash(obj_id, obj)

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
            self._stash(obj.get("request_id"), obj)
            # If every batch id arrived as an individual reply, the server isn't framing batches
            # as arrays — fail instead of blocking forever waiting for an array that won't come.
            if all(i in self._pending for i in ids):
                raise ConnectionError("batch: server returned individual envelopes, not an array")

    def wait_frames(self, n: int) -> Dict[str, Any]:
        """Block until `n` simulation frames have ADVANCED (not loop iterations).

        Deferred — the correlation loop handles the yield. Uses result() so a server
        rejection (frame.wait is RUN-only: manual/paused return bad_params) raises
        DevApiResultError instead of silently returning an {ok:false} envelope.
        """
        return self.result("frame.wait", {"frames": n})

    def time_wait(self, seconds: float) -> Dict[str, Any]:
        """Block until `seconds` of GAME time elapse (not wall, not frames).

        Deferred, RUN-only: game time advances on the variable RUN dt, so a fixed frame
        count can't express it (that's the point). Rejected in manual/paused/scale==0 where
        time is frozen (raises DevApiResultError). Wall duration is ~seconds/scale.
        """
        return self.result("time.wait", {"seconds": seconds})

    def step(self, count: int = 1) -> Dict[str, Any]:
        """Advance exactly `count` fixed-dt sim frames (lockstep); requires manual mode.

        Maps to time.step{count}; deterministic — no wall clock, no max_dt clamp.
        Deferred: blocks until all `count` sim-advances have completed, so a follow-up
        frame.current reads frame already advanced by exactly count (not the queue mid-drain).
        """
        return self.result("time.step", {"count": count})

    def step_seconds(self, seconds: float) -> Dict[str, Any]:
        """Advance the MANUAL sim by `seconds` of game time = ceil(seconds/step_dt) fixed-dt frames.

        Sugar over time.step{seconds} for time-authored mechanics (cooldowns, durations). Linear
        and deterministic in MANUAL since step_dt is fixed. Requires manual mode (raises
        DevApiResultError in RUN). Deferred; blocks until the steps drain.
        """
        return self.result("time.step", {"seconds": seconds})

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
        """Read {enabled, draw_calls} — draw_calls is 0 while render is disabled."""
        return self.result("render.info")

    def frame_current(self) -> Dict[str, Any]:
        """Read the current {frame, time, dt} from the managed loop's g_nt_app state."""
        return self.result("frame.current")

    def wait_game_seconds(self, total: float, chunk: float = 4.0) -> None:
        """Wait `total` seconds of GAME time by looping time.wait in <=chunk segments.

        For LIVE-RUN observation only. The engine caps a single time.wait at
        NT_DEVAPI_TIME_WAIT_MAX_SECONDS (4.0s, sized to the 5s socket read timeout); this
        helper chains shorter waits to exceed that without raising the client read timeout.
        For DETERMINISTIC cooldown/duration tests prefer MANUAL mode + step_seconds() — real-time
        waiting is non-deterministic and slow. RUN + unpaused + scale>0 required (time.wait rejects
        frozen time).
        """
        if total < 0:
            raise ValueError("total must be >= 0")
        # chunk <= 0 would never decrement remaining (infinite loop); chunk > 4.0 exceeds the engine
        # cap (NT_DEVAPI_TIME_WAIT_MAX_SECONDS) so every segment raises. Bound it to (0, 4.0].
        if not 0 < chunk <= 4.0:
            raise ValueError("chunk must be in (0, 4.0]")
        remaining = total
        while remaining > 0:
            seg = chunk if remaining > chunk else remaining
            self.time_wait(seg)  # result() raises on bad_params (manual/paused/scale==0)
            remaining -= seg

    # #region input.* wrappers — fire-and-forget inject. result() just confirms ok / raises.
    def key(self, name: str, down: Optional[bool] = None, hold: Optional[float] = None) -> Dict[str, Any]:
        """Inject a key edge (down default true on the host) OR a tap (hold frames -> down@0 + up@hold)."""
        params: Dict[str, Any] = {"key": name}
        if down is not None:
            params["down"] = down
        if hold is not None:
            params["hold"] = hold
        return self.result("input.key", params)

    def move(
        self, x: float, y: float, id: Optional[int] = None, type: Optional[str] = None
    ) -> Dict[str, Any]:
        """Inject a pointer move on the default mouse slot (reserved id) unless id/type given.

        x/y are Y-up (origin bottom-left) in FRAMEBUFFER px — input.* uses the device framebuffer as
        its basis. This is NOT the same basis as ui.* {x,y} / ui_tree bounds, which are ctx-LAYOUT px:
        the two coincide only when dpr==1 AND the ctx begin-dims equal the framebuffer (no nt_ui_scale,
        no HiDPI). Under a scaled/HiDPI ctx, feed ui.click({x,y}) ctx-layout coords, NOT input.* fb coords.
        """
        params: Dict[str, Any] = {"x": x, "y": y}
        if id is not None:
            params["id"] = id
        if type is not None:
            params["type"] = type
        return self.result("input.move", params)

    def click(
        self,
        x: float,
        y: float,
        button: Optional[int] = None,
        id: Optional[int] = None,
        hold: Optional[int] = None,
    ) -> Dict[str, Any]:
        """Inject a pointer down+up (2 atomic entries) on the mouse slot.

        x/y are Y-up (origin bottom-left) in FRAMEBUFFER px — input.*'s device-fb basis, which differs
        from ui.* {x,y} / ui_tree bounds (ctx-LAYOUT px); they coincide only at dpr==1 AND begin-dims==fb.
        Default is a 1-frame hold (down@0 + up@1) — a realistic click held across one frame.
        Pass hold=0 for an instant same-frame click (down+up collapsed into one frame). `hold`
        is omitted from the request when None so the host-side default of 1 is the single
        source of truth.
        """
        params: Dict[str, Any] = {"x": x, "y": y}
        if button is not None:
            params["button"] = button
        if id is not None:
            params["id"] = id
        if hold is not None:
            params["hold"] = hold
        return self.result("input.click", params)

    def pointer(
        self,
        action: str,
        id: int,
        x: Optional[float] = None,
        y: Optional[float] = None,
        type: Optional[str] = None,
        buttons: Optional[int] = None,
    ) -> Dict[str, Any]:
        """The pointer primitive: action down/move/up on a given id (default mouse type).

        x/y are Y-up (origin bottom-left) in FRAMEBUFFER px — input.*'s device-fb basis, which differs
        from ui.* {x,y} / ui_tree bounds (ctx-LAYOUT px); they coincide only at dpr==1 AND begin-dims==fb.
        """
        params: Dict[str, Any] = {"action": action, "id": id}
        if x is not None:
            params["x"] = x
        if y is not None:
            params["y"] = y
        if type is not None:
            params["type"] = type
        if buttons is not None:
            params["buttons"] = buttons
        return self.result("input.pointer", params)

    def wheel(
        self, dx: float = 0.0, dy: float = 0.0, x: Optional[float] = None, y: Optional[float] = None
    ) -> Dict[str, Any]:
        """Scroll the mouse slot (notches).

        x/y (when given) are Y-up (origin bottom-left) in FRAMEBUFFER px — input.*'s device-fb basis,
        which differs from ui.* {x,y} / ui_tree bounds (ctx-LAYOUT px); they coincide only at dpr==1 AND
        begin-dims==fb. dx/dy are notch deltas (content-scroll sign), NOT spatial coords, so no axis
        flip applies to them. With x and y, the scroll is self-contained — a move to (x,y) then the
        wheel, so it lands at (x,y) regardless of prior state. Without x/y it applies at the mouse
        slot's apply-time position (move first, or it is a no-op if no slot exists).
        """
        params: Dict[str, Any] = {"dx": dx, "dy": dy}
        if x is not None:
            params["x"] = x
        if y is not None:
            params["y"] = y
        return self.result("input.wheel", params)

    def gesture(
        self,
        id: int,
        points: List[List[float]],
        type: Optional[str] = None,
        frame_stride: Optional[int] = None,
    ) -> Dict[str, Any]:
        """Inject down@0 + a move per point (frame_stride apart) + up; NO interpolation.

        Each point's x/y is Y-up (origin bottom-left) in FRAMEBUFFER px — input.*'s device-fb basis,
        which differs from ui.* {x,y} / ui_tree bounds (ctx-LAYOUT px); they coincide only at dpr==1 AND
        begin-dims==fb.
        """
        params: Dict[str, Any] = {"id": id, "points": points}
        if type is not None:
            params["type"] = type
        if frame_stride is not None:
            params["frame_stride"] = frame_stride
        return self.result("input.gesture", params)

    def button(self, buttons: int, id: Optional[int] = None) -> Dict[str, Any]:
        """Set the mouse-button mask {1,2,4} on the given id (default reserved mouse slot)."""
        params: Dict[str, Any] = {"buttons": buttons}
        if id is not None:
            params["id"] = id
        return self.result("input.button", params)

    def set_player_enabled(self, enabled: bool) -> Dict[str, Any]:
        """Toggle the player-input gate (false suppresses the real device; inject still flows)."""
        return self.result("input.set_player_enabled", {"enabled": enabled})

    def text(self, s: str) -> Dict[str, Any]:
        """Decode a UTF-8 string -> codepoints and enqueue them into the char ring."""
        return self.result("input.text", {"text": s})

    def state(self, key: Optional[str] = None, pop_text: bool = False) -> Dict[str, Any]:
        """IMMEDIATE read of polled input state — the observation hook.

        Returns the result dict (a READ, unlike the fire-and-forget wrappers above). With `key`,
        result carries {down,pressed,released} for that key; the state is STALE until a sim-advance
        polls the inject queue (the drain-race). With `pop_text=True`, result carries
        `codepoints` (a raw-codepoint number array) and DRAINS the char ring as a side effect.
        """
        params: Dict[str, Any] = {}
        if key is not None:
            params["key"] = key
        if pop_text:
            params["pop_text"] = True
        return self.result("input.state", params)

    # #endregion

    # #region ui.* wrappers — reads return the result dict; writes are fire-and-forget (result() confirms ok).
    def ui_tree(self, ctx: Optional[str] = None) -> Dict[str, Any]:
        """IMMEDIATE read of the last completed frame's UI tree.

        Returns the result dict: a {space, origin, y_axis, width, height, dpr, projection} metadata
        block plus `nodes` (ALL nodes incl. invisible/disabled — the bot filters). Bounds are Y-up
        (origin bottom-left) — the SAME space ui_click/drag/scroll {x,y} take, so a bot may feed
        bounds straight back with no flip. `ctx` selects a registered context by name (omitted ->
        the host's sole/first).
        """
        params: Dict[str, Any] = {}
        if ctx is not None:
            params["ctx"] = ctx
        return self.result("ui.tree", params)

    def ui_element(self, id: str, ctx: Optional[str] = None) -> Dict[str, Any]:
        """Read one UI node by developer string id (unknown/stale id raises DevApiResultError)."""
        params: Dict[str, Any] = {"id": id}
        if ctx is not None:
            params["ctx"] = ctx
        return self.result("ui.element", params)

    def ui_contexts(self) -> Dict[str, Any]:
        """List the host-registered UI context names (result dict carries `contexts`)."""
        return self.result("ui.contexts", {})

    def ui_click(
        self, id: Any, hold: Optional[int] = None, ctx: Optional[str] = None
    ) -> Dict[str, Any]:
        """Resolve `id` (a string id, or an {"x":..,"y":..} dict) -> bbox center -> synthetic click.

        {x,y} are Y-up (origin bottom-left), the SAME space as ui_tree bounds — feed a node's bounds
        center straight in with no flip. Fire-and-forget: the down+up enqueue, applied only once the
        sim advances — pair with step()/wait_frames() before reading the widget back. `hold` is the
        down->up frame gap (omitted -> host default of 1).
        """
        params: Dict[str, Any] = {"id": id}
        if hold is not None:
            params["hold"] = hold
        if ctx is not None:
            params["ctx"] = ctx
        return self.result("ui.click", params)

    def ui_scroll(
        self,
        id: Any,
        dx: float = 0.0,
        dy: float = 0.0,
        ctx: Optional[str] = None,
    ) -> Dict[str, Any]:
        """Resolve `id` (string id or Y-up {x,y}, origin bottom-left) -> element center -> synthetic wheel(dx,dy) notches."""
        params: Dict[str, Any] = {"id": id, "dx": dx, "dy": dy}
        if ctx is not None:
            params["ctx"] = ctx
        return self.result("ui.scroll", params)

    def ui_drag(
        self,
        from_: Any,
        to: Any,
        frames: Optional[int] = None,
        ctx: Optional[str] = None,
    ) -> Dict[str, Any]:
        """Synthetic drag from `from_` to `to` (each a string id or Y-up {x,y}, origin bottom-left).

        {x,y} are Y-up — the SAME space as ui_tree bounds (no bot-side flip). `frames` interpolated
        move points are emitted between down@from and up@to (omitted -> host default).
        Fire-and-forget — advance the sim to apply.
        """
        params: Dict[str, Any] = {"from": from_, "to": to}
        if frames is not None:
            params["frames"] = frames
        if ctx is not None:
            params["ctx"] = ctx
        return self.result("ui.drag", params)

    # #endregion

    # #region capture.* wrappers — deferred DATA commands; result() blocks until a render fills the payload.
    def capture_frame(self, scale: Optional[int] = None) -> Dict[str, Any]:
        """Capture the full framebuffer as a PNG. Result: {width,height,format:"png",data:<base64>}.

        Deferred: result() blocks until the next pre-swap render fills the payload, so it returns the
        real PNG (NOT {deferred:true}). `scale` is the integer divisor {1,2,4} (1=full, 2=half,
        4=quarter); omitted -> full resolution.
        """
        params: Dict[str, Any] = {}
        if scale is not None:
            params["scale"] = scale
        return self.result("capture.frame", params)

    def capture_region(
        self, x: int, y: int, w: int, h: int, scale: Optional[int] = None
    ) -> Dict[str, Any]:
        """Capture an (x,y,w,h) sub-rect of the framebuffer as a PNG. Result: {width,height,format:"png",data:<base64>}.

        Deferred like capture_frame. Top-left origin, bounded to the framebuffer (out-of-bounds /
        zero-size / over-cap -> bad_params over the wire). `scale` is the integer divisor {1,2,4}
        applied after the crop.
        """
        params: Dict[str, Any] = {"x": x, "y": y, "w": w, "h": h}
        if scale is not None:
            params["scale"] = scale
        return self.result("capture.region", params)

    def capture_frame_and_step(
        self, count: int = 1, scale: Optional[int] = None
    ) -> Tuple[Dict[str, Any], Dict[str, Any]]:
        """Capture + advance MANUAL time in one flush; returns (capture_result, step_result).

        capture.frame defers until a frame is presented — under manual time mode sending it
        alone deadlocks (no frame will ever present until time.step, which was never sent).
        Both requests go out before any read. The step reply is read FIRST: a rejected step
        means no frame ever presents and the capture reply never arrives — waiting on the
        capture first would turn that clear error into a read timeout.
        """
        if not isinstance(count, int) or isinstance(count, bool) or count <= 0:
            raise ValueError("count must be a positive integer")
        if count > self._step_max:
            raise ValueError(f"count {count} exceeds NT_DEVAPI_STEP_MAX ({self._step_max})")
        cap_params: Dict[str, Any] = {}
        if scale is not None:
            cap_params["scale"] = scale
        cap_id = self._alloc_id()
        step_id = self._alloc_id()
        self._transport.send(json.dumps({"method": "capture.frame", "request_id": cap_id, "params": cap_params}))
        self._transport.send(json.dumps({"method": "time.step", "request_id": step_id, "params": {"count": count}}))
        step_resp = self._recv_until(step_id)
        if step_resp.get("ok") is not True:
            if cap_id in self._pending:
                self._pending.pop(cap_id)
            else:
                self._abandoned.add(cap_id)
            err = step_resp.get("error") or {}
            raise DevApiResultError(
                f"time.step failed: {err.get('code', 'unknown')}: {err.get('message', '(no message)')}"
            )
        # Step succeeded, so a frame presented and the capture reply is on the wire
        # (an immediate bad_params reply is stashed by _recv_until either way).
        cap_resp = self._recv_until(cap_id)
        if cap_resp.get("ok") is not True:
            err = cap_resp.get("error") or {}
            raise DevApiResultError(
                f"capture.frame failed: {err.get('code', 'unknown')}: {err.get('message', '(no message)')}"
            )
        return cap_resp.get("result", {}), step_resp.get("result", {})

    # #endregion

    def close(self) -> None:
        self._transport.close()
