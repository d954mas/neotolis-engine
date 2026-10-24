"""DevAPI client transports — the wire under DevApiClient.

A pluggable Transport interface (send a line, read a line, close) plus the
native SocketTransport: a loopback TCP connection to the engine's devapi
server, JSON-lines framed on '\\n', with a MANDATORY read timeout so a
crashed / deferred-forever engine can never hang the bot or CI.

Stdlib only (socket) — no pip deps. Python 3.8+.
"""
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


# One wait per evaluate: a queued line returns at once; otherwise a double rAF (one rendered frame, so
# the pre-swap seam can fill a deferred capture) races a timer for the time left, then one poll. rAF
# stops in a background tab; the timer keeps the wait bounded.
_POLL_AFTER_FRAME_JS = (
    "async (ms) => { const q = window.__devapi.poll(); if (q) return q; let t; "
    "await Promise.race(["
    "new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r))), "
    "new Promise(r => { t = setTimeout(r, ms); })]); "
    "clearTimeout(t); return window.__devapi.poll(); }"
)


class PlaywrightTransport(Transport):
    """JSON-lines over window.__devapi.submit/poll, polled once per rendered frame until a line arrives.

    No socket — drives the web devapi bridge over a Playwright page. send() calls
    window.__devapi.submit and stashes the SYNCHRONOUS response (a non-deferred command returns its
    response line immediately; a deferred command — C returned NULL — returns "" and its data line
    arrives later via poll()). recv_line() returns a stashed line, else polls after each frame until a
    line appears or the MANDATORY read_timeout fires. Like SocketTransport it only moves lines: time
    advance belongs to the client.

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

    def send(self, line: str) -> None:
        if self._closed:
            raise ConnectionError("transport is closed")
        # submit returns "" on defer (C returned NULL), else the synchronous response line.
        resp = self._page.evaluate("(l) => window.__devapi.submit(l)", line)
        if resp:
            self._inbox.append(resp)

    def recv_line(self) -> str:
        if self._closed:
            raise ConnectionError("transport is closed")
        if self._inbox:
            return self._inbox.pop(0)
        deadline = time.monotonic() + self._timeout
        while True:
            left = deadline - time.monotonic()
            if left <= 0:
                raise TimeoutError("no web devapi line within read_timeout")
            line = self._page.evaluate(_POLL_AFTER_FRAME_JS, max(1, int(left * 1000)))
            if line:
                return line

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        self._inbox.clear()
        self._page.evaluate("() => window.__devapi.reset()")
