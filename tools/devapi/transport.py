"""DevAPI client transports — the wire under DevApiClient.

A Transport moves one request line out and its one reply line back (exchange) and
closes. SocketTransport is the native wire: a loopback TCP connection to the engine's
devapi server, JSON-lines framed on '\\n'. PlaywrightTransport is the web wire: the
page's window.__devapi bridge. An exchange waits as long as the engine needs: a
logically blocked reply fails with stalled, so it stays pending only on a platform
stall or a long requested wait, and the caller's outer deadline (CI step, test runner,
shell) bounds the session. A new client takes over from the previous one.

Stdlib only (socket); PlaywrightTransport only touches the page handle its caller made.
Python 3.8+.
"""
import socket
from abc import ABC, abstractmethod

DEFAULT_CONNECT_TIMEOUT = 5.0
# Matches the host's NT_DEVAPI_DEFAULT_PORT.
DEFAULT_PORT = 17890
# Read memory bound: a framing-desync guard, NOT a protocol limit. Sized to cover the engine's
# capture cap (NT_DEVAPI_CAPTURE_MAX_PIXELS = 4096*4096 worst-case base64 PNG ~= 100 MB) with margin,
# so the client can always read what the server can produce while the read stays bounded.
MAX_LINE_BYTES = 112 * 1024 * 1024


class Transport(ABC):
    """One JSON line out, its one JSON line back. The client frames; the wire moves bytes."""

    @abstractmethod
    def exchange(self, line: str) -> str:
        """Send one serialized JSON line (no trailing newline) and block until the reply line arrives."""

    @abstractmethod
    def close(self) -> None:
        """Release the underlying resource. Idempotent."""


class SocketTransport(Transport):
    """Loopback TCP, JSON-lines on '\\n'.

    Numeric 127.0.0.1 keeps create_connection on IPv4, matching the server's
    INADDR_LOOPBACK bind (an IPv6 ::1 resolution would never reach it).
    """

    def __init__(
        self,
        host: str = "127.0.0.1",
        port: int = DEFAULT_PORT,
        connect_timeout: float = DEFAULT_CONNECT_TIMEOUT,
    ) -> None:
        # Set first so a failure mid-construction still leaves close() safe.
        self._sock = None
        self._f = None
        self._sock = socket.create_connection((host, port), timeout=connect_timeout)
        self._sock.settimeout(None)
        # Binary makefile handles partial-recv reassembly; readline frames on b'\n' and caps the read in
        # BYTES (a text-mode makefile would cap MAX_LINE_BYTES in CHARACTERS, diverging from the contract).
        # 256 KB buffer: multi-MB base64 capture lines through the default 8 KB reader recv so slowly
        # they can trip the ENGINE's send timeout mid-line.
        self._f = self._sock.makefile("rb", buffering=256 * 1024)

    @property
    def port(self) -> int:
        return self._sock.getpeername()[1]

    def __enter__(self) -> "SocketTransport":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def exchange(self, line: str) -> str:
        self._sock.sendall((line + "\n").encode("utf-8"))
        # Bound the read (in BYTES) so a desynced stream can never grow memory without limit.
        raw = self._f.readline(MAX_LINE_BYTES)
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
# nt_devapi_frame_rendered can fill a deferred capture) and "", so the next evaluate polls. Polling only at
# the start keeps a wait abandoned by a killed client from taking the next client's reply. A background
# tab stops rAF and so the wait, the same as a host that stops ticking.
_POLL_AFTER_FRAME_JS = (
    "async () => { const q = window.__devapi.poll(); if (q) return q; "
    "await new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r))); "
    'return ""; }'
)


class PlaywrightTransport(Transport):
    """JSON-lines over window.__devapi.submit/poll, polled once per rendered frame until a line arrives.

    No socket — drives the web devapi bridge over a Playwright page (`page`). submit returns the reply
    at once for an immediate command, or "" for a deferred one, whose line then arrives through poll().
    Construction and close() reset the bridge, dropping a pending reply, so a client takes over from one
    that was killed (a new native connection replaces the old one the same way). Construct it after
    window.__devapi exists. Unlike a native takeover, a replaced live web client gets no error, so drive
    one page with one client. Like SocketTransport it only moves lines: time advance belongs to the client.
    """

    def __init__(self, page) -> None:
        self.page = page
        self._closed = False
        page.evaluate("() => window.__devapi.reset()")

    def exchange(self, line: str) -> str:
        if self._closed:
            raise ConnectionError("transport is closed")
        resp = self.page.evaluate("(l) => window.__devapi.submit(l)", line)
        while not resp:
            resp = self.page.evaluate(_POLL_AFTER_FRAME_JS)
        return resp

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        try:
            self.page.evaluate("() => window.__devapi.reset()")
        except Exception:  # noqa: BLE001
            pass  # a closed or crashed page has nothing to reset; never mask the run's failure
