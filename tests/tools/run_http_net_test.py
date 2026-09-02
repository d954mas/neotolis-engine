#!/usr/bin/env python3
"""ctest wrapper for test_http_native_net: starts the local echo server on an
OS-assigned port (immune to port squatting / parallel runs), waits for it to
accept connections, runs the test exe (argv[1:]) with NT_HTTP_TEST_BASE set,
then stops the server."""
import os
import socket
import subprocess
import sys
import time

server = subprocess.Popen(
    [sys.executable, os.path.join(os.path.dirname(__file__), "http_echo_server.py"), "0"],
    stdout=subprocess.PIPE,
)
try:
    line = server.stdout.readline().decode()
    assert line.startswith("PORT="), f"unexpected server output: {line!r}"
    port = int(line[len("PORT=") :])
    for _ in range(200):
        try:
            socket.create_connection(("127.0.0.1", port), timeout=0.1).close()
            break
        except OSError:
            time.sleep(0.05)
    else:
        sys.exit(f"echo server on port {port} never accepted a connection")
    # An inherited http_proxy/ALL_PROXY would route the loopback requests through a
    # proxy while the readiness probe above connected directly — bypass it explicitly.
    env = dict(
        os.environ,
        NT_HTTP_TEST_BASE=f"http://127.0.0.1:{port}",
        NO_PROXY="127.0.0.1,localhost",
        no_proxy="127.0.0.1,localhost",
    )
    sys.exit(subprocess.call(sys.argv[1:], env=env))
finally:
    server.terminate()
