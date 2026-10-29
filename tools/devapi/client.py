"""DevApiClient — the reusable harness surface.

request / result / wait_frames / step over a pluggable Transport. One request is in flight at a
time and its reply must carry its request_id. The engine keeps one deferred reply in flight: a
command sent while one is pending answers busy, and closing the transport drops it.
A request waits as long as the engine needs; the caller's outer deadline bounds the session.

Stdlib only (json + socket) — no pip deps. Python 3.8+.
"""
import json
import time
from typing import Any, Dict, Optional

from .transport import Transport


class DevApiResultError(RuntimeError):
    """Raised by result() when the server returns {ok:false}, surfacing error.code/message."""


class DevApiClient:
    """Blocking request/response over a Transport (`transport`), one request in flight.

    Each request gets a monotonic id; the engine echoes it on every reply to a well-formed request,
    so a reply with any other id is a protocol fault and raises at once.
    """

    def __init__(self, transport: Transport) -> None:
        self.transport = transport
        self._next_id = 1

    def request(self, method: str, params: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        """Send one command, block until its reply arrives, return the raw envelope."""
        rid = self._next_id
        self._next_id += 1
        msg: Dict[str, Any] = {"method": method, "request_id": rid}
        if params is not None:
            msg["params"] = params
        line = self.transport.exchange(json.dumps(msg))
        try:
            obj = json.loads(line)
        except ValueError as exc:
            raise ConnectionError(f"protocol: invalid JSON from server: {line!r}") from exc
        if not isinstance(obj, dict) or obj.get("request_id") != rid:
            raise ConnectionError(f"protocol: expected the reply to request_id {rid}, got {line!r}")
        return obj

    def result(self, method: str, params: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        """request() + assert ok; return the result object, raising on {ok:false}."""
        resp = self.request(method, params)
        if resp.get("ok") is not True:
            err = resp.get("error") or {}
            code = err.get("code", "unknown")
            message = err.get("message", "(no message)")
            raise DevApiResultError(f"{method} failed: {code}: {message}")
        return resp.get("result", {})

    def wait_frames(self, n: int) -> Dict[str, Any]:
        """Block until `n` simulation frames have ADVANCED (not loop iterations).

        Deferred; returns {frame,time,dt} at the reply. RUN-only: manual/paused raise
        DevApiResultError (bad_params), and a pause or mode switch while waiting raises it
        with stalled.
        """
        return self.result("frame.wait", {"frames": n})

    def time_wait(self, seconds: float) -> Dict[str, Any]:
        """Block until `seconds` of GAME time elapse (not wall, not frames).

        Deferred, RUN-only: game time advances on the variable RUN dt, so a fixed frame
        count can't express it (that's the point). Rejected in manual/paused/scale==0 where
        time is frozen, and stalled if time freezes while waiting (both raise
        DevApiResultError). Wall duration is ~seconds/scale. Returns {frame,time,dt}.
        """
        return self.result("time.wait", {"seconds": seconds})

    def step(self, count: int = 1) -> Dict[str, Any]:
        """Advance exactly `count` fixed-dt sim frames (lockstep); requires manual mode.

        Maps to time.step{count}; deterministic — no wall clock, no max_dt clamp.
        Deferred: blocks until every queued step is done and returns {frame,time,dt} then.
        A mode switch while stepping drops the queue and raises DevApiResultError (stalled).
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
        """Read {enabled, draw_calls} — draw_calls is 0 while render is disabled."""
        return self.result("render.info")

    def frame_current(self) -> Dict[str, Any]:
        """Read the current {frame, time, dt} from the managed loop's g_nt_app state."""
        return self.result("frame.current")

    # #region input.* wrappers — fire-and-forget inject. result() just confirms ok / raises.
    def key(self, name: str, down: Optional[bool] = None, hold: Optional[float] = None) -> Dict[str, Any]:
        """Inject a key edge (down default true on the host) OR a tap (hold frames -> down@0 + up@hold)."""
        params: Dict[str, Any] = {"key": name}
        if down is not None:
            params["down"] = down
        if hold is not None:
            params["hold"] = hold
        return self.result("input.key", params)

    def set_player_enabled(self, enabled: bool) -> Dict[str, Any]:
        """Toggle the player-input gate (false suppresses the real device; inject still flows)."""
        return self.result("input.set_player_enabled", {"enabled": enabled})

    def text(self, s: str) -> Dict[str, Any]:
        """Decode a UTF-8 string -> codepoints and enqueue them into the char ring."""
        return self.result("input.text", {"text": s})

    def state(self, key: Optional[str] = None, text: bool = False) -> Dict[str, Any]:
        """IMMEDIATE read of input state — the observation hook.

        Returns the result dict (a READ, unlike the fire-and-forget wrappers above). With `key`,
        result carries {down,pressed,released} for that key, all from the last sim-advancing poll,
        so an inject shows only after a sim-advance (the drain-race). With `text=True`, result
        carries `codepoints` (a raw-codepoint number array): that poll's text the game left unread.
        """
        params: Dict[str, Any] = {}
        if key is not None:
            params["key"] = key
        if text:
            params["text"] = True
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

    # #region capture.* wrappers — deferred commands; result() blocks until a render builds the PNG.
    def capture_frame(self, scale: Optional[int] = None) -> Dict[str, Any]:
        """Capture the full framebuffer, at the rendered frame's size, as a PNG. Result: {width,height,format:"png",data:<base64>}.

        Deferred: result() blocks until the next pre-swap render builds the PNG. Works in any time
        mode and never advances the sim: in MANUAL it shows the current state; step first for a new
        one. Raises DevApiResultError (stalled) while rendering is off. `scale` is the integer
        divisor {1,2,4} (1=full, 2=half, 4=quarter); omitted -> full resolution.
        """
        params: Dict[str, Any] = {}
        if scale is not None:
            params["scale"] = scale
        return self.result("capture.frame", params)

    def capture_region(
        self, x: int, y: int, w: int, h: int, scale: Optional[int] = None
    ) -> Dict[str, Any]:
        """Capture an (x,y,w,h) sub-rect of the framebuffer as a PNG. Result: {width,height,format:"png",data:<base64>}.

        Deferred like capture_frame. Top-left origin, bounded to the framebuffer of the rendered frame
        (out-of-bounds / zero-size / over-cap -> bad_params over the wire). `scale` is the integer divisor {1,2,4}
        applied after the crop.
        """
        params: Dict[str, Any] = {"x": x, "y": y, "w": w, "h": h}
        if scale is not None:
            params["scale"] = scale
        return self.result("capture.region", params)

    # #endregion

    # #region gfx.* wrappers — gfx command recording; every frame records into the host's ring while on.
    def gfx_record(self, enabled: bool) -> Dict[str, Any]:
        """Turn gfx recording on or off (off keeps the ring; off then on clears it). Result: {}."""
        return self.result("gfx.record", {"enabled": enabled})

    def gfx_frames(self) -> Dict[str, Any]:
        """List the stored frames, oldest first. Result: {recording, frames:[{sequence,sim_frame,advanced,records,overflow}]}.

        After off the frame in flight is still stored, so frame_not_ready is retried until it is.
        """
        return self._result_when_ready("gfx.frames", {})

    def gfx_read(self, sequence: Optional[int] = None) -> Dict[str, Any]:
        """Read one stored frame's capture text: the newest, or by a sequence from gfx.frames. Result: {sequence, sim_frame, text}.

        The newest is frame_not_ready until a frame is stored (and after off until the frame in flight is); that is retried. Any other error raises at once.
        """
        return self._result_when_ready("gfx.read", {} if sequence is None else {"sequence": sequence})

    def gfx_step_frame(self, sim_frame: int) -> int:
        """The sequence of the stored frame in which the sim advanced to `sim_frame` (the frame of a step).

        A step replies before its frame is stored, so gfx.frames is polled until that frame is listed. Raises
        DevApiResultError when it cannot come: recording is off, or the newest stored frame is at or past
        `sim_frame` without it (evicted, or it began before recording turned on).
        """
        while True:
            listing = self.gfx_frames()
            frames = listing["frames"]
            for f in frames:
                if f["advanced"] and f["sim_frame"] == sim_frame:
                    return f["sequence"]
            if not listing["recording"] or (frames and (frames[-1]["sim_frame"] - sim_frame) % 2**32 < 2**31):
                raise DevApiResultError(f"gfx_step_frame: no stored frame advanced to sim frame {sim_frame}")
            time.sleep(0.005)

    def _result_when_ready(self, method: str, params: Dict[str, Any]) -> Dict[str, Any]:
        while True:
            try:
                return self.result(method, params)
            except DevApiResultError as exc:
                if "failed: frame_not_ready:" not in str(exc):
                    raise
            time.sleep(0.005)

    # #endregion

    def close(self) -> None:
        self.transport.close()
