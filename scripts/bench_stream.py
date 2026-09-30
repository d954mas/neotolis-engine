#!/usr/bin/env python3
"""Runner for examples/bench_stream (#351): serve the wasm build, collect results, summarize.

serve      Serves the wasm-release build, answers GET /bench_stream.cfg with --cfg,
           appends every POSTed window to <out>.jsonl and writes <out>.md when the
           bench reports done. --adb forwards the port to a USB phone, opens the page
           in its browser and tags each window with GPU clock and temperatures read
           from sysfs while that window ran.
summarize  Rebuilds the markdown summary from one or more .jsonl files.

Stdlib only. Examples:
    python scripts/bench_stream.py serve --cfg cfg.txt --out build/bench_stream/p40_run1
    python scripts/bench_stream.py serve --adb --cfg cfg.txt --out build/bench_stream/p40_run1
    python scripts/bench_stream.py summarize build/bench_stream/p40_run1.jsonl
"""
import argparse
import http.server
import json
import re
import statistics
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_WEB_DIR = ROOT / "build" / "examples" / "bench_stream" / "wasm-release"
BASELINE_ARM = {"inst": "ring", "batch": "zero"}  # the pattern master ships today


# region device sampling
class DeviceSampler(threading.Thread):
    """Polls GPU devfreq and thermal-HAL temperatures over adb about once a second."""

    def __init__(self, serial):
        super().__init__(daemon=True)
        self.adb = ["adb"] + (["-s", serial] if serial else [])
        self.samples = []  # (host_time, {name: value})
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.script = self._probe_script()

    def _shell(self, cmd):
        out = subprocess.run(self.adb + ["shell", cmd], capture_output=True, text=True, timeout=10)
        return out.stdout

    def _probe_script(self):
        # sysfs thermal zones are often unreadable without root; thermalservice reads the HAL live (its cached list is stale).
        gpus = [d for d in self._shell("ls -d /sys/class/devfreq/* 2>/dev/null").split() if re.search(r"gpu|mali|g3d", d, re.I)]
        reads = [f"echo gpu_mhz:{d.rsplit('/', 1)[1]} $(cat {d}/cur_freq 2>/dev/null)" for d in gpus]
        reads.append("dumpsys thermalservice 2>/dev/null | grep -A12 'Current temperatures from HAL' | grep 'Temperature{'")
        return "; ".join(reads)

    def run(self):
        while not self.stop.is_set():
            values = {}
            for line in self._shell(self.script).splitlines():
                temp = re.search(r"mValue=(-?[\d.]+).*mName=([\w-]+)", line)
                if temp:
                    values.setdefault(f"temp:{temp.group(2)}", float(temp.group(1)))
                    continue
                name, _, raw = line.partition(" ")
                if name.startswith("gpu_mhz") and raw.strip().isdigit():
                    values[name] = int(raw.strip()) / 1e6
            with self.lock:
                self.samples.append((time.time(), values))
            self.stop.wait(1.0)

    def summary(self, t0, t1):
        with self.lock:
            window = [v for t, v in self.samples if t0 <= t <= t1]
        tags = {}
        for name in sorted({k for v in window for k in v}):
            vals = [v[name] for v in window if name in v]
            tags[name] = [round(min(vals), 1), round(max(vals), 1)]
        return tags


def adb_open(serial, port, package):
    adb = ["adb"] + (["-s", serial] if serial else [])
    subprocess.run(adb + ["reverse", f"tcp:{port}", f"tcp:{port}"], check=True)
    url = f"http://localhost:{port}/index.html?t={int(time.time())}"
    subprocess.run(adb + ["shell", "am", "start", "-a", "android.intent.action.VIEW", "-d", url, package], check=True)
    print(f"[runner] opened {url} in {package}")
# endregion


# region summary
def load(paths):
    rows = []
    for p in paths:
        for line in Path(p).read_text(encoding="utf-8").splitlines():
            d = json.loads(line)
            if "arm" in d:
                rows.append(d)
    return rows


def summarize(rows):
    """Median over every window of an arm (both ABBA halves, all reps); delta against master's pattern."""
    cases = {}
    for r in rows:
        cases.setdefault((r["family"], r["episodes"], r["size"], r["load"]), {}).setdefault(r["arm"], []).append(r)
    out = ["| family | E | size | load | arm | windows | fps | vs base | p50 ms | p95 ms | gpu ms | GL calls | gpu MHz | temps |",
           "|---|---:|---:|---:|---|---:|---:|---:|---:|---:|---:|---:|---|---|"]
    for (family, episodes, size, load_n), arms in sorted(cases.items()):
        base = arms.get(BASELINE_ARM[family])
        base_fps = statistics.median(w["fps"] for w in base) if base else None
        for arm, ws in arms.items():
            fps = statistics.median(w["fps"] for w in ws)
            delta = f"{(fps / base_fps - 1) * 100:+.1f}%" if base_fps else "-"
            gpu = [w["gpu_ms"] for w in ws if w["gpu_ms"] >= 0]
            dev = ws[-1].get("device", {})
            mhz = " ".join(f"{v[0]:.0f}-{v[1]:.0f}" for k, v in dev.items() if k.startswith("gpu_mhz"))
            temps = " ".join(f"{k[5:]}={v[1]:.0f}" for k, v in dev.items() if k.startswith("temp:"))
            gpu_ms = f"{statistics.median(gpu):.3f}" if gpu else "-"
            p50 = statistics.median(w["ms_p50"] for w in ws)
            p95 = statistics.median(w["ms_p95"] for w in ws)
            gl = statistics.median(w["gl_calls"] for w in ws)
            out.append(f"| {family} | {episodes} | {size} | {load_n} | {arm} | {len(ws)} | {fps:.1f} | {delta} | "
                       f"{p50:.2f} | {p95:.2f} | {gpu_ms} | {gl:.0f} | {mhz} | {temps} |")
    return "\n".join(out) + "\n"
# endregion


# region server
def serve(args):
    web_dir = Path(args.web_dir)
    if not (web_dir / "index.html").exists():
        sys.exit(f"missing {web_dir / 'index.html'}; build bench_stream with the wasm-release preset first")
    cfg = Path(args.cfg).read_bytes() if args.cfg else b""
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    jsonl = out.with_suffix(".jsonl")
    sampler = DeviceSampler(args.serial) if args.adb else None
    if sampler:
        sampler.start()
    state = {"t_prev": time.time(), "windows": 0, "done": threading.Event(), "lock": threading.Lock()}

    class Handler(http.server.SimpleHTTPRequestHandler):
        extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map, ".wasm": "application/wasm", ".js": "text/javascript"}

        def __init__(self, *a, **kw):
            super().__init__(*a, directory=str(web_dir), **kw)

        def end_headers(self):
            self.send_header("Cache-Control", "no-store")
            super().end_headers()

        def log_message(self, fmt, *a):
            pass

        def do_GET(self):
            if self.path.split("?")[0] == "/bench_stream.cfg":
                self.send_response(200 if cfg else 404)
                self.send_header("Content-Type", "text/plain")
                self.end_headers()
                self.wfile.write(cfg)
                state["t_prev"] = time.time()
                return
            super().do_GET()

        def do_POST(self):
            body = self.rfile.read(int(self.headers.get("Content-Length", 0))).decode("utf-8")
            self.send_response(200)
            self.end_headers()
            now = time.time()
            d = json.loads(body)
            if "done" in d:
                return  # POSTs race; the window count in record() decides completion
            d["host_time"] = round(now, 3)
            d["user_agent"] = self.headers.get("User-Agent", "")
            with state["lock"]:
                record(d, now)

    def record(d, now):
        if sampler:
            d["device"] = sampler.summary(state["t_prev"], now)
        state["t_prev"] = now
        with jsonl.open("a", encoding="utf-8") as f:
            f.write(json.dumps(d) + "\n")
        print(f"[{d['index'] + 1}/{d['of']}] {d['family']:5} {d['arm']:11} E={d['episodes']:<3} n={d['size']:<4} "
              f"fps={d['fps']:8.1f} p95={d['ms_p95']:6.2f}ms {d.get('device', '')}")
        state["windows"] += 1
        if state["windows"] == d["of"]:
            write_summary(jsonl, out)
            state["done"].set()

    httpd = http.server.ThreadingHTTPServer(("0.0.0.0" if args.lan else "127.0.0.1", args.port), Handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    print(f"[runner] serving {web_dir} on :{args.port}; results -> {jsonl}")
    if args.adb:
        adb_open(args.serial, args.port, args.package)
    else:
        print(f"[runner] open http://localhost:{args.port}/index.html")
    try:
        state["done"].wait()
    except KeyboardInterrupt:
        pass
    httpd.shutdown()
    if sampler:
        sampler.stop.set()


def write_summary(jsonl, out):
    rows = load([jsonl])
    ua = rows[0].get("user_agent", "") if rows else ""
    md = out.with_suffix(".md")
    md.write_text(f"# bench_stream {out.name}\n\nUA: `{ua}`\n\n" + summarize(rows), encoding="utf-8")
    print(f"[runner] done; summary -> {md}")
# endregion


def main():
    sys.stdout.reconfigure(line_buffering=True)
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("serve")
    s.add_argument("--cfg", help="config served as bench_stream.cfg (defaults compiled into the bench when omitted)")
    s.add_argument("--out", required=True, help="output path without extension")
    s.add_argument("--web-dir", default=str(DEFAULT_WEB_DIR))
    s.add_argument("--port", type=int, default=8351)
    s.add_argument("--lan", action="store_true", help="listen on all interfaces (phone over Wi-Fi instead of adb reverse)")
    s.add_argument("--adb", action="store_true", help="forward the port to a USB phone, open the page, sample GPU clock/temps")
    s.add_argument("--serial", help="adb device serial when several are attached")
    s.add_argument("--package", default="com.android.chrome", help="browser package to open the page in")
    m = sub.add_parser("summarize")
    m.add_argument("files", nargs="+")
    args = ap.parse_args()
    if args.cmd == "serve":
        serve(args)
    else:
        print(summarize(load(args.files)), end="")


if __name__ == "__main__":
    main()
