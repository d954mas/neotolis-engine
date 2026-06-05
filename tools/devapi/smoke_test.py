#!/usr/bin/env python3
"""End-to-end smoke test driven through nt_devapi.

Launches the native game with the TCP server, then walks the whole UI flow and
asserts the expected screen at each step. Exit code 0 = pass, 1 = fail.

  python tools/devapi/smoke_test.py [PORT]

Requires a native-debug build (NT_DEVAPI_ENABLED): turkic_jam.exe must exist.
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

# UI labels include Cyrillic/Turkish; keep printing safe on a cp1251 console.
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass


class Bus:
    def __init__(self, sock):
        self.f = sock.makefile("rwb", buffering=0)
        self.scale_x = 1.0
        self.scale_y = 1.0

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
            raise AssertionError(f"label not found to click: {label!r} (have {[r.get('text') for r in rows if r.get('text')]})")
        cx = (row["x"] + row["w"] / 2.0) * self.scale_x
        cy = (row["y"] + row["h"] / 2.0) * self.scale_y
        self.req(f"input.click x={cx:.0f} y={cy:.0f}")
        time.sleep(0.35)


def connect(timeout=15.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            s = socket.create_connection(("127.0.0.1", PORT), timeout=1.0)
            s.settimeout(3.0)
            return s
        except OSError:
            time.sleep(0.25)
    return None


def main():
    if not os.path.exists(EXE):
        print("FAIL: build the game first (native-debug):", EXE)
        return 1

    proc = subprocess.Popen([EXE, "--devapi", str(PORT)], cwd=os.path.dirname(EXE))
    failures = []
    try:
        s = connect()
        if not s:
            print("FAIL: could not connect to devapi server")
            return 1
        bus = Bus(s)
        view = bus.req("view")["data"]
        bus.scale_x = view["fb_w"] / view["logical_w"]
        bus.scale_y = view["fb_h"] / view["logical_h"]

        def check(name, expected_substrings):
            txt = " | ".join(t for t in bus.texts() if t)
            ok = all(any(e in t for t in bus.texts()) for e in expected_substrings)
            print(("PASS" if ok else "FAIL"), name, "::", txt)
            if not ok:
                failures.append(name)

        check("menu", ["TURKIC JAM 2026", "START", "Settings"])
        bus.click_label("Settings")
        check("settings", ["Settings", "Reset progress"])
        bus.click_label("Menu")
        check("back to menu", ["START"])
        bus.click_label("START")
        check("game", ["Playing", "TAP +1", "Lose"])
        bus.click_label("TAP +1")
        check("score incremented", ["Score: 1"])
        bus.req("input.key key=P mode=tap")
        time.sleep(0.35)
        check("paused", ["Paused", "Resume"])
        bus.click_label("Resume")
        check("resumed (score kept)", ["Score: 1"])
        bus.click_label("Lose")
        check("game over", ["Game Over", "Retry"])
        bus.click_label("Retry")
        check("retry resets", ["Score: 0", "TAP +1"])

        s.close()
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()

    print("\n=== %s ===" % ("ALL PASSED" if not failures else f"FAILED: {failures}"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
