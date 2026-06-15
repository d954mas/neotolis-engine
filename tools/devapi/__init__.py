"""tools.devapi — the stdlib-only Python harness for the Neotolis DevAPI.

The first non-builder Python in the repo. Stdlib only (socket + json), no pip
deps (D-16), Python 3.8+. Every later phase's smoke test builds on this client.

    from tools.devapi import DevApiClient, SocketTransport
    client = DevApiClient(SocketTransport("127.0.0.1", 17890))
    print(client.result("engine.info"))
"""
from .client import DevApiClient, DevApiResultError
from .transport import (
    DEFAULT_PORT,
    DEFAULT_READ_TIMEOUT,
    SocketTransport,
    Transport,
)

__all__ = [
    "DevApiClient",
    "DevApiResultError",
    "SocketTransport",
    "Transport",
    "DEFAULT_PORT",
    "DEFAULT_READ_TIMEOUT",
]
