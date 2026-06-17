"""tools.devapi — the stdlib-only Python harness for the Neotolis DevAPI.

Stdlib only (socket + json), no pip deps, Python 3.8+.

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
