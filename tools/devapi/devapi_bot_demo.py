#!/usr/bin/env python3
"""Demo bot: drives the running game over nt_devapi.

Reads the UI tree, finds the START button by its label text, clicks its centre,
then re-reads the tree to confirm the scene changed. Shows the full
observe -> act -> observe loop a real bot would use.

Run the game first:  turkic_jam.exe --devapi 9123
Then:                python devapi_bot_demo.py 9123
"""
import json
import socket
import sys
import time

HOST = "127.0.0.1"

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 9123
    s = socket.create_connection((HOST, port), timeout=5)
    s.settimeout(3)
    f = s.makefile("rwb", buffering=0)

    def req(line):
        f.write((line + "\n").encode())
        f.flush()
        return json.loads(f.readline().decode("utf-8", "replace").strip())

    print("ping        ->", req("ping"))
    print("view        ->", req("view"))

    tree = req("ui.tree")["data"]
    labels = [r.get("text") for r in tree if r.get("text")]
    print("menu labels ->", labels)

    start = next((r for r in tree if r.get("text") == "START"), None)
    if not start:
        print("START not found — is the menu visible?")
        return
    cx = start["x"] + start["w"] / 2.0
    cy = start["y"] + start["h"] / 2.0
    print(f"click START -> {req(f'input.click x={cx:.0f} y={cy:.0f}')}  @({cx:.0f},{cy:.0f})")

    time.sleep(0.4)  # let the click resolve + scene swap
    tree2 = req("ui.tree")["data"]
    labels2 = [r.get("text") for r in tree2 if r.get("text")]
    print("after click ->", labels2)
    print("SCENE CHANGED:", labels != labels2)

    s.close()


if __name__ == "__main__":
    main()
