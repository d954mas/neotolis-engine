#!/usr/bin/env python3
"""Minimal client for the nt_devapi TCP command bus.

Usage:
  python devapi_cli.py [PORT] "endpoint args..."   # one command
  python devapi_cli.py [PORT]                        # REPL (stdin)
  echo -e "ping\\nui.tree" | python devapi_cli.py 9123   # piped

Protocol: send one command line, read one JSON response line.
"""
import json
import socket
import sys

HOST = "127.0.0.1"

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass


def connect(port, timeout=5.0):
    s = socket.create_connection((HOST, port), timeout=timeout)
    s.settimeout(timeout)
    return s


def request(sock_file, line):
    sock_file.write((line + "\n").encode())
    sock_file.flush()
    raw = sock_file.readline().decode("utf-8", "replace").strip()
    try:
        return json.loads(raw)
    except json.JSONDecodeError:
        return {"ok": False, "raw": raw}


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 9123
    s = connect(port)
    f = s.makefile("rwb", buffering=0)
    args = sys.argv[2:]
    if args:
        print(json.dumps(request(f, " ".join(args)), ensure_ascii=False))
    else:
        for line in sys.stdin:
            line = line.strip()
            if not line:
                continue
            if line in ("quit", "exit"):
                break
            print(json.dumps(request(f, line), ensure_ascii=False))
    s.close()


if __name__ == "__main__":
    main()
