"""Shared scenario launcher: one run(client, out_dir) on a native or a web devapi host.

A scenario module defines run(client, out_dir) and ends with
    if __name__ == "__main__":
        sys.exit(launch.main(run))
and runs from the repo root as `python -m tools.devapi.scenarios.<name> [options]`:

  --target native  (default) launch the native devapi_host (--host-bin, default the devapi-smoke build)
                   on a free loopback port passed as NT_DEVAPI_PORT; or attach to a running host
                   with --port N.
  --target web     serve the wasm build (--web-dir, default the wasm-debug build) on a free loopback
                   port and drive it in headless Chromium (playwright-python, the web CI job's pinned
                   dependency: tools/devapi/requirements-web.txt).
  --out DIR        where scenarios write artifacts (default: build/devapi-out); created, made absolute
                   and passed to run as out_dir.

A scenario finds its platform in the client: client.transport is a SocketTransport (.port) or a
PlaywrightTransport (.page). Exit 0 pass, 1 assertion / protocol / transport failure, 2 usage / launch.
There is no read timeout and no retry: the caller's outer deadline (a CI step's timeout) bounds a run.
"""
import argparse
import functools
import http.server
import os
import socket
import subprocess
import sys
import threading
import time
import traceback

from .client import DevApiClient, DevApiResultError
from .transport import PlaywrightTransport, SocketTransport

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
_HOST_EXE = "devapi_host.exe" if os.name == "nt" else "devapi_host"
_DEFAULT_HOST_BIN = os.path.join(REPO_ROOT, "build", "examples", "devapi_host", "devapi-smoke", _HOST_EXE)
_DEFAULT_WEB_DIR = os.path.join(REPO_ROOT, "build", "examples", "devapi_host", "wasm-debug")
# SwiftShader WebGL2, as tests/browser/playwright.config.ts: headless CI has no GPU.
_CHROMIUM_ARGS = ["--use-gl=angle", "--use-angle=swiftshader", "--enable-unsafe-swiftshader", "--ignore-gpu-blocklist"]

class LaunchError(Exception):
    """The host could not be started or reached; exit code 2."""


def _free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def _run_native(args, run) -> None:
    if args.port is not None:
        try:
            transport = SocketTransport(port=args.port)
        except OSError as exc:
            raise LaunchError(f"cannot attach to 127.0.0.1:{args.port}: {exc}") from exc
        with transport:
            run(DevApiClient(transport))
        return
    host_bin = os.path.abspath(args.host_bin or _DEFAULT_HOST_BIN)
    if not os.path.isfile(host_bin):
        raise LaunchError(f"devapi_host not found at {host_bin}; build it (docs/build.md) or pass --host-bin / --port")
    port = _free_port()
    # cwd = the host's directory, so a host that loads relative asset packs finds them.
    try:
        proc = subprocess.Popen([host_bin], cwd=os.path.dirname(host_bin), env=dict(os.environ, NT_DEVAPI_PORT=str(port)))
    except OSError as exc:
        raise LaunchError(f"cannot start {host_bin}: {exc}") from exc
    try:
        transport = None
        deadline = time.monotonic() + 10.0
        while transport is None:
            if proc.poll() is not None:
                raise LaunchError(f"devapi_host exited with {proc.returncode} before accepting a client")
            try:
                transport = SocketTransport(port=port)
            except OSError as exc:
                if time.monotonic() > deadline:
                    raise LaunchError(f"could not connect to 127.0.0.1:{port} within 10 s: {exc}") from exc
                time.sleep(0.1)
        with transport:
            run(DevApiClient(transport))
        if proc.poll() is not None:
            raise AssertionError(f"devapi_host exited with {proc.returncode} during the run")
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()


class _Handler(http.server.SimpleHTTPRequestHandler):
    # Keep-alive: with HTTP/1.0 (a close per response) Chrome on Windows often saw index.wasm.map reset,
    # and the debug build then waits on its source map forever.
    protocol_version = "HTTP/1.1"
    # Explicit: Windows mimetypes reads the registry, which may lack .wasm or map .js to text/plain.
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        ".wasm": "application/wasm",
        ".js": "text/javascript",
    }

    def end_headers(self):
        # Cross-origin isolation with same-origin resources, as tests/browser/serve.mjs.
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        super().end_headers()

    def log_message(self, format, *args):
        pass


def _run_web(args, run) -> None:
    try:
        from playwright.sync_api import sync_playwright
    except ImportError as exc:
        raise LaunchError("--target web needs playwright: pip install -r tools/devapi/requirements-web.txt "
                          "&& python -m playwright install chromium") from exc
    web_dir = os.path.abspath(args.web_dir or _DEFAULT_WEB_DIR)
    if not os.path.isfile(os.path.join(web_dir, "index.html")):
        raise LaunchError(f"no index.html in {web_dir}; build devapi_host for wasm (docs/build.md) or pass --web-dir")
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(_Handler, directory=web_dir))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        with sync_playwright() as pw:
            try:
                browser = pw.chromium.launch(args=_CHROMIUM_ARGS)
            except Exception as exc:
                raise LaunchError(f"chromium did not start ({exc}); python -m playwright install chromium") from exc
            errors = []
            try:
                page = browser.new_context(viewport={"width": 1280, "height": 720}, accept_downloads=True).new_page()
                # Printed as they happen: a trap leaves the reply pending until the CI step timeout kills
                # the run, so nothing printed at the end would show the cause.
                page.on("pageerror", lambda exc: (errors.append(str(exc)), print(f"[pageerror] {exc}", file=sys.stderr, flush=True)))
                # The engine log goes to the console; forward it so a failure shows its WARN/ERROR lines.
                page.on("console", lambda msg: print(f"[page] {msg.text}", flush=True))
                try:
                    page.goto(f"http://127.0.0.1:{server.server_address[1]}/index.html")
                    page.wait_for_function("() => window.__devapi !== undefined", timeout=30_000)
                except Exception as exc:
                    raise LaunchError(f"the page did not install window.__devapi ({exc})") from exc
                transport = PlaywrightTransport(page)
                try:
                    run(DevApiClient(transport))
                finally:
                    transport.close()
                assert not errors, f"page errors: {errors}"
            finally:
                if errors:
                    print(f"page errors: {errors}", file=sys.stderr)
                browser.close()
    finally:
        server.shutdown()
        server.server_close()


def _port(text: str) -> int:
    port = int(text)
    if not 1 <= port <= 65535:
        raise argparse.ArgumentTypeError(f"{text} is not a port in 1..65535")
    return port


def main(run) -> int:
    """Parse the launcher options, run `run(client, out_dir)` on the chosen target, return the exit code."""
    ap = argparse.ArgumentParser(description=(sys.modules["__main__"].__doc__ or "").split("\n")[0])
    ap.add_argument("--target", choices=("native", "web"), default="native")
    ap.add_argument("--port", type=_port, help="native: attach to a running host on this port")
    ap.add_argument("--host-bin", help="native: devapi_host executable to launch")
    ap.add_argument("--web-dir", help="web: directory with the wasm build's index.html")
    ap.add_argument("--out", default=os.path.join(REPO_ROOT, "build", "devapi-out"), help="artifact directory")
    args = ap.parse_args()
    out_dir = os.path.abspath(args.out)
    scenario = functools.partial(run, out_dir=out_dir)
    try:
        try:
            os.makedirs(out_dir, exist_ok=True)
        except OSError as exc:
            raise LaunchError(f"cannot create --out {out_dir}: {exc}") from exc
        if args.target == "native":
            _run_native(args, scenario)
        else:
            _run_web(args, scenario)
    except LaunchError as exc:
        print(f"FAIL: launch: {exc}", file=sys.stderr)
        return 2
    except AssertionError as exc:
        print(f"FAIL: assertion: {exc}")
        return 1
    except DevApiResultError as exc:
        print(f"FAIL: protocol: {exc}")
        return 1
    except (ConnectionError, OSError) as exc:
        print(f"FAIL: transport: {exc}")
        return 1
    except Exception:  # a driver error (playwright timeout, closed page) is a failed run, not a crash
        traceback.print_exc()
        print("FAIL: driver error (traceback above)")
        return 1
    print("PASS")
    return 0
