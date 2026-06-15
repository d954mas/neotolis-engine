"""DevAPI client transports — the wire under DevApiClient.

A pluggable Transport interface (send a line, read a line, close) plus the
native SocketTransport: a loopback TCP connection to the engine's devapi
server, JSON-lines framed on '\\n', with a MANDATORY read timeout (D-19) so a
crashed / deferred-forever engine can never hang the bot or CI.

Phase 70 adds a PlaywrightTransport behind the same interface (the web ccall
bridge); the client never sees which wire it talks over.

Stdlib only (socket) — no pip deps (D-16). Python 3.8+.
"""
import socket
from abc import ABC, abstractmethod

# Default loopback read timeout (seconds). Bounded so a dead/deferred-forever
# server is fatal-fast instead of an infinite hang (D-19).
DEFAULT_READ_TIMEOUT = 5.0
DEFAULT_CONNECT_TIMEOUT = 5.0
# Matches the host's NT_DEVAPI_DEFAULT_PORT (Plan 02/03).
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
    """Loopback TCP, JSON-lines on '\\n', mandatory read timeout (D-19).

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
        self._sock = socket.create_connection((host, port), timeout=connect_timeout)
        # D-19: the read timeout is mandatory, not optional — never block forever.
        self._sock.settimeout(read_timeout)
        # makefile handles partial-recv reassembly + UTF-8 decode; readline frames on '\n'.
        self._f = self._sock.makefile("r", encoding="utf-8")

    def send(self, line: str) -> None:
        # sendall loops internally on partial writes; framing newline is the line terminator.
        self._sock.sendall((line + "\n").encode("utf-8"))

    def recv_line(self) -> str:
        # On read-timeout expiry readline raises socket.timeout/TimeoutError — let it
        # propagate; the client re-raises naming the pending request_id (D-19).
        line = self._f.readline()
        if line == "":
            # Orderly disconnect: recv returned b"" (D-22 mirror). Fail fast, never hang.
            raise ConnectionError("server closed the connection")
        return line.rstrip("\n")

    def close(self) -> None:
        try:
            self._f.close()
        finally:
            self._sock.close()
