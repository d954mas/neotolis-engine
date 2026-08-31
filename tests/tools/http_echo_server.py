#!/usr/bin/env python3
"""Local echo server for the nt_http native net test (test_http_native_net).

Run:  python tests/tools/http_echo_server.py [port]   (default 8124)
Then: build/tests/<preset>/test_http_native_net

Endpoints mirror tests/browser/serve.mjs so both acceptance tests assert the
same contract: POST /echo (byte-exact echo + request headers reflected into
X-Echo-* response headers), GET /hello, GET /status404, GET /slow (5 s stall
for the timeout test).
"""
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def _reply(self, status, body, headers=()):
        self.send_response(status)
        for name, value in headers:
            self.send_header(name, value)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/hello":
            self._reply(200, b"hello-neotolis", [("Content-Type", "text/plain"), ("X-NT-Server", "echo")])
        elif self.path == "/status404":
            self._reply(404, b"missing")
        elif self.path == "/slow":
            time.sleep(5)
            self._reply(200, b"ok")
        else:
            self._reply(404, b"")

    def do_POST(self):
        if self.path == "/echo":
            n = int(self.headers.get("Content-Length", "0"))
            body = self.rfile.read(n)
            self._reply(
                200,
                body,
                [
                    ("Content-Type", "application/octet-stream"),
                    ("X-Echo-Content-Type", self.headers.get("Content-Type", "")),
                    ("X-Echo-X-Nt-Test", self.headers.get("X-NT-Test", "")),
                ],
            )
        else:
            self._reply(404, b"")


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8124
    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    # PORT= line first: run_http_net_test.py parses it (port 0 -> OS-assigned)
    print(f"PORT={server.server_address[1]}", flush=True)
    print(f"nt_http echo server: http://127.0.0.1:{server.server_address[1]}/", flush=True)
    server.serve_forever()
