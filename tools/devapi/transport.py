"""DevAPI client transports — the wire under DevApiClient.

A pluggable Transport interface (send a line, read a line, close) plus the
native SocketTransport: a loopback TCP connection to the engine's devapi
server, JSON-lines framed on '\\n', with a MANDATORY read timeout so a
crashed / deferred-forever engine can never hang the bot or CI.

Stdlib only (socket) — no pip deps. Python 3.8+.
"""
import socket
from abc import ABC, abstractmethod

# Default loopback read timeout (seconds). Bounded so a dead/deferred-forever
# server is fatal-fast instead of an infinite hang.
DEFAULT_READ_TIMEOUT = 5.0
DEFAULT_CONNECT_TIMEOUT = 5.0
# Matches the host's NT_DEVAPI_DEFAULT_PORT.
DEFAULT_PORT = 17890


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
        # makefile handles partial-recv reassembly + UTF-8 decode; readline frames on '\n'.
        self._f = self._sock.makefile("r", encoding="utf-8")

    def __enter__(self) -> "SocketTransport":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    def send(self, line: str) -> None:
        # sendall loops internally on partial writes; framing newline is the line terminator.
        self._sock.sendall((line + "\n").encode("utf-8"))

    def recv_line(self) -> str:
        # On read-timeout expiry readline raises socket.timeout/TimeoutError — let it
        # propagate; the client re-raises naming the pending request_id.
        # Bound the read so a desynced stream can never grow memory without limit.
        line = self._f.readline(1_048_576)
        if line == "":
            # Orderly disconnect: recv returned b"" (mirrors the server-side close path). Fail fast, never hang.
            raise ConnectionError("server closed the connection")
        if len(line) >= 1_048_576 and not line.endswith("\n"):
            raise ConnectionError("oversized/unterminated line — framing desync")
        return line.rstrip("\r\n")

    def close(self) -> None:
        try:
            if self._f is not None:
                self._f.close()
        finally:
            if self._sock is not None:
                self._sock.close()
