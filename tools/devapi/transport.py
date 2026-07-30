"""DevAPI client transports — the wire under DevApiClient.

A pluggable Transport interface (send a line, read a line, close) plus the
native SocketTransport: a loopback TCP connection to the engine's devapi
server, JSON-lines framed on '\\n', with a MANDATORY read timeout so a
crashed / deferred-forever engine can never hang the bot or CI.

Stdlib only (socket) — no pip deps. Python 3.8+.
"""
import json
import socket
import time
from abc import ABC, abstractmethod

# Default loopback read timeout (seconds). Bounded so a dead/deferred-forever
# server is fatal-fast instead of an infinite hang.
DEFAULT_READ_TIMEOUT = 5.0
DEFAULT_CONNECT_TIMEOUT = 5.0
# Matches the host's NT_DEVAPI_DEFAULT_PORT.
DEFAULT_PORT = 17890
# recv_line memory bound: a framing-desync guard, NOT a protocol limit. Sized to cover the engine's
# capture cap (NT_DEVAPI_CAPTURE_MAX_PIXELS = 4096*4096 worst-case base64 PNG ~= 100 MB) with margin,
# so the client can always read what the server can produce while the read stays bounded.
MAX_LINE_BYTES = 112 * 1024 * 1024


class Transport(ABC):
    """One JSON line out, one JSON line in. The client frames; the wire moves bytes."""

    @abstractmethod
    def send(self, line: str) -> None:
        """Send one already-serialized JSON line (no trailing newline — the transport adds framing)."""

    @abstractmethod
    def recv_line(self) -> str:
        """Block until one framed line arrives; return it WITHOUT the trailing newline."""

    @abstractmethod
    def close(self) -> None:
        """Release the underlying resource. Idempotent."""


class SocketTransport(Transport):
    """Loopback TCP, JSON-lines on '\\n', mandatory read timeout.

    Numeric 127.0.0.1 keeps create_connection on IPv4, matching the server's
    INADDR_LOOPBACK bind (an IPv6 ::1 resolution would never reach it).
    """

    def __init__(
        self,
        host: str = "127.0.0.1",
        port: int = DEFAULT_PORT,
        connect_timeout: float = DEFAULT_CONNECT_TIMEOUT,
        read_timeout: float = DEFAULT_READ_TIMEOUT,
    ) -> None:
        # Set first so a failure mid-construction still leaves close() safe.
        self._sock = None
        self._f = None
        self._sock = socket.create_connection((host, port), timeout=connect_timeout)
        # The read timeout is mandatory, not optional — never block forever.
        self._sock.settimeout(read_timeout)
        # Binary makefile handles partial-recv reassembly; readline frames on b'\n' and caps the read in
        # BYTES (a text-mode makefile would cap MAX_LINE_BYTES in CHARACTERS, diverging from the contract).
        # 256 KB buffer: multi-MB base64 capture lines through the default 8 KB reader recv so slowly
        # they can trip the ENGINE's send timeout mid-line.
        self._f = self._sock.makefile("rb", buffering=256 * 1024)

    def __enter__(self) -> "SocketTransport":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def send(self, line: str) -> None:
        # sendall loops internally on partial writes; framing newline is the line terminator.
        self._sock.sendall((line + "\n").encode("utf-8"))

    def recv_line(self) -> str:
        # Bound the read (in BYTES) so a desynced stream can never grow memory without limit.
        try:
            raw = self._f.readline(MAX_LINE_BYTES)
        except (socket.timeout, TimeoutError) as exc:
            # On py3.8 socket.timeout is NOT a TimeoutError subclass; normalize so callers catch
            # one type. A mid-read timeout also leaves the buffered reader in an undefined state,
            # so invalidate the transport — a retry must fail fast, not read corrupted framing.
            self.close()
            raise TimeoutError(str(exc) or "read timed out") from exc
        if raw == b"":
            # Orderly disconnect: recv returned b"" (mirrors the server-side close path). Fail fast, never hang.
            raise ConnectionError("server closed the connection")
        if len(raw) >= MAX_LINE_BYTES and not raw.endswith(b"\n"):
            raise ConnectionError("oversized/unterminated line — framing desync")
        return raw.decode("utf-8").rstrip("\r\n")

    def close(self) -> None:
        try:
            if self._f is not None:
                self._f.close()
        finally:
            if self._sock is not None:
                self._sock.close()


# A deterministic rAF barrier: a Promise that resolves after two nested requestAnimationFrame
# callbacks. One nested rAF lets the deferred capture's pre-swap seam run (the producer fills its
# payload INSIDE the swap that the second rAF schedules).
_RAF_BARRIER_JS = "() => new Promise(r => requestAnimationFrame(() => requestAnimationFrame(() => r())))"


class PlaywrightTransport(Transport):
    """JSON-lines over window.__devapi.submit/poll, pumped once per rAF until a line arrives.

    No socket — drives the web devapi bridge over a Playwright page. send() calls
    window.__devapi.submit and stashes the SYNCHRONOUS response (a non-deferred command returns its
    response line immediately; a deferred command — C returned NULL — returns "" and its data line
    arrives later via poll()). recv_line() returns a stashed line, else loops a rAF barrier +
    window.__devapi.poll() until a line appears or the MANDATORY read_timeout fires — mirroring
    SocketTransport's bounded read so a deferred-forever capture can never hang CI.

    Playwright Python is NOT a CI dep (the CI web gate is the TS spec); this class is the local/dev
    web wire and touches only the page handle the caller constructed, so the module stays stdlib-only
    importable for the native SocketTransport path.
    """

    def __init__(self, page, read_timeout: float = DEFAULT_READ_TIMEOUT) -> None:
        self._page = page
        self._timeout = read_timeout
        self._closed = False
        # Lines received synchronously from submit() but not yet handed to recv_line().
        self._inbox = []
        # Outbound bookkeeping for the MANUAL frame pump: the last method sent and whether the host is in
        # MANUAL (where the frame counter advances only on time.step, so a frame-keyed deferred like
        # capture never drains on rAF alone).
        self._manual = False
        self._last_method = None

    def _note_outbound(self, line: str) -> None:
        """Track the outbound method + MANUAL mode so recv_line pumps frames correctly."""
        self._last_method = None
        try:
            msg = json.loads(line)
        except ValueError:
            return
        if not isinstance(msg, dict):
            return  # a batch array — no single method
        self._last_method = msg.get("method")
        if self._last_method == "time.set_mode":
            mode = (msg.get("params") or {}).get("mode")
            if mode in ("manual", "run"):
                self._manual = mode == "manual"

    def send(self, line: str) -> None:
        if self._closed:
            raise ConnectionError("transport is closed")
        if len(line.encode("utf-8")) > MAX_LINE_BYTES:
            raise ValueError("outbound line exceeds MAX_LINE_BYTES")
        self._note_outbound(line)
        # submit returns "" on defer (C returned NULL), else the synchronous response line.
        resp = self._page.evaluate("(l) => window.__devapi.submit(l)", line)
        if resp:
            self._inbox.append(resp)

    def recv_line(self) -> str:
        if self._closed:
            raise ConnectionError("transport is closed")
        if self._inbox:
            return self._inbox.pop(0)
        # Bounded poll: never an unbounded loop — a deferred-forever capture must fail fast.
        deadline = time.time() + self._timeout
        while time.time() < deadline:
            # Advance one MANUAL frame so a frame-keyed deferred (capture) drains — but NOT when the
            # caller's own command is time.step, which is itself a deferred sim-advance (an extra step
            # would over-advance the sim). Under RUN the rAF below advances the frame.
            if self._manual and self._last_method != "time.step":
                self._page.evaluate("() => window.__devapi.tick(1)")
            self._page.evaluate(_RAF_BARRIER_JS)
            line = self._page.evaluate("() => window.__devapi.poll()")
            if not line:
                continue
            if len(line.encode("utf-8")) >= MAX_LINE_BYTES:
                raise ConnectionError("oversized line from window.__devapi.poll — framing/cap desync")
            # Drop the pump's own step replies (the bridge tick uses NEGATIVE request_ids; a caller only
            # ever allocates positive ones) so they never pollute the client's correlation map.
            try:
                rid = json.loads(line).get("request_id")
            except ValueError:
                rid = None
            if isinstance(rid, int) and rid < 0:
                continue
            return line
        raise TimeoutError("no web devapi line within read_timeout")

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        self._inbox.clear()
        self._page.evaluate("() => window.__devapi.reset()")
