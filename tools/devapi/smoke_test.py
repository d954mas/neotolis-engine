#!/usr/bin/env python3
"""End-to-end smoke test driven through nt_devapi.

Launches the native game with the TCP server, confirms the balance config loaded,
then walks menu -> settings -> back -> run, watches the run advance, forces death,
and checks the game-over / retry path. Exit 0 = pass, 1 = fail.

  python tools/devapi/smoke_test.py [PORT]
"""
import json
import os
import socket
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXE = os.path.join(ROOT, "build", "games", "turkic-jam-2026", "native-debug", "turkic_jam.exe")
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 9123

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass


class Bus:
    def __init__(self, sock):
        self.f = sock.makefile("rwb", buffering=0)
        self.sx = 1.0
        self.sy = 1.0

    def req(self, line):
        self.f.write((line + "\n").encode())
        self.f.flush()
        return json.loads(self.f.readline().decode("utf-8", "replace").strip())

    def texts(self):
        return [r.get("text") for r in self.req("ui.tree")["data"] if r.get("text")]

    def click_label(self, label):
        rows = self.req("ui.tree")["data"]
        row = next((r for r in rows if r.get("text") == label), None)
        if not row:
            raise AssertionError(f"can't click {label!r}; have {[r.get('text') for r in rows if r.get('text')]}")
        self.req(f"input.click x={(row['x'] + row['w'] / 2) * self.sx:.0f} y={(row['y'] + row['h'] / 2) * self.sy:.0f}")
        time.sleep(0.35)


def connect(timeout=15.0):
    end = time.time() + timeout
    while time.time() < end:
        try:
            s = socket.create_connection(("127.0.0.1", PORT), timeout=1.0)
            s.settimeout(3.0)
            return s
        except OSError:
            time.sleep(0.25)
    return None


def main():
    if not os.path.exists(EXE):
        print("FAIL: build native-debug first:", EXE)
        return 1
    proc = subprocess.Popen([EXE, "--devapi", str(PORT)], cwd=os.path.dirname(EXE))
    fails = []
    try:
        s = connect()
        if not s:
            print("FAIL: no devapi connection")
            return 1
        bus = Bus(s)
        view = bus.req("view")["data"]
        bus.sx = view["fb_w"] / view["logical_w"]
        bus.sy = view["fb_h"] / view["logical_h"]

        def check(name, cond, extra=""):
            print(("PASS" if cond else "FAIL"), name, "::", extra)
            if not cond:
                fails.append(name)

        cfg = bus.req("game.config").get("data", {})
        check("config loaded", cfg.get("tiles", 0) > 0 and cfg.get("heirs", 0) > 0, str(cfg))

        # Menu is optional (start_in_game=1 boots straight into the run).
        if "START" in bus.texts():
            bus.click_label("Settings")
            check("settings", any("Reset" in (t or "") for t in bus.texts()), " | ".join(bus.texts()))
            bus.click_label("Menu")
            check("back to menu", "START" in bus.texts())
            bus.click_label("START")
        head = next((t for t in bus.texts() if t and t.startswith("Круг")), None)
        check("run started", head is not None, str(head))

        # watch a couple seconds: the cell/circle should advance on its own
        c0 = next((t for t in bus.texts() if t and t.startswith("Круг")), "")
        time.sleep(1.6)
        c1 = next((t for t in bus.texts() if t and t.startswith("Круг")), "")
        check("run advances", c0 != c1, f"{c0!r} -> {c1!r}")

        bus.click_label("Lose")  # force death
        time.sleep(0.4)
        check("game over", any("Retry" in (t or "") for t in bus.texts()), " | ".join(bus.texts()))
        bus.click_label("Retry")
        check("retry -> run", any(t and t.startswith("Круг") for t in bus.texts()))

        s.close()
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()

    print("\n=== %s ===" % ("ALL PASSED" if not fails else f"FAILED: {fails}"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
