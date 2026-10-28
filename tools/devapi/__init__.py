"""tools.devapi — the Python harness for the Neotolis DevAPI, native and web.

The client and the native transport are stdlib only (socket + json), Python 3.8+. The web target
(launch.py --target web) needs playwright: tools/devapi/requirements-web.txt.

    from tools.devapi import DevApiClient, SocketTransport
    client = DevApiClient(SocketTransport("127.0.0.1", 17890))
    print(client.result("engine.info"))

Scenarios run through tools/devapi/launch.py: python -m tools.devapi.scenarios.<name> [--target web].
"""
from .client import DevApiClient, DevApiResultError
from .transport import (
    DEFAULT_PORT,
    SocketTransport,
    Transport,
)

__all__ = [
    "DevApiClient",
    "DevApiResultError",
    "SocketTransport",
    "Transport",
    "DEFAULT_PORT",
]
