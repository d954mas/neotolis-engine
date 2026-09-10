#!/usr/bin/env python3
"""Check diagnostics macros, required defines, and external CMake consumers."""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parent.parent
FIXTURE = ROOT / "tests" / "submodule" / "diagnostics"
LEVELS = ("INFO", "WARN", "ERROR")
FORMS = ("PLAIN", "ONCE", "UNIQUE", "DOMAIN_PLAIN", "DOMAIN_ONCE", "DOMAIN_UNIQUE")
DEFINES = {
    "NT_LOG_MIN_LEVEL": 0,
    "NT_METRICS_ENABLED": 0,
    "NT_LOG_RING_ENABLED": 0,
    "NT_INTROSPECT_ENABLED": 0,
    "NT_INTROSPECT_WRITE_ENABLED": 0,
    "NT_UI_DEBUG_TOOLS": 0,
    "NT_UI_TIMING_ENABLED": 0,
    "NT_GFX_GPU_TIMING_ENABLED": 0,
}


class Checks:
    def __init__(self, args, output):
        self.args = args
        self.output = output
        self.count = 0
        self.cc = [args.cc, "-std=c17", "-O0", "-g0", "-Werror", "-Wformat=2",
                   "-I", str(ROOT / "engine"), "-I", str(ROOT / "shared" / "include")]

    def command(self, label, command, expected_error=None):
        result = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, encoding="utf-8", errors="replace")
        (self.output / f"{label}.log").write_text(
            json.dumps(command) + "\n" + result.stdout, encoding="utf-8")
        if expected_error is None:
            if result.returncode != 0:
                raise RuntimeError(f"{label}: unexpected failure\n{result.stdout[-6000:]}")
        elif result.returncode == 0 or not re.search(expected_error, result.stdout, re.I | re.S):
            raise RuntimeError(f"{label}: expected failure /{expected_error}/\n{result.stdout[-6000:]}")
        self.count += 1
        return result.stdout

    def compile(self, label, source, defines, expected_error=None):
        path = self.output / f"{label}.c"
        path.write_text(source, encoding="utf-8")
        obj = self.output / f"{label}.o"
        self.command(label, self.cc + [f"-D{k}={v}" for k, v in defines.items()]
                     + ["-c", str(path), "-o", str(obj)], expected_error)
        return obj

    def macros(self):
        source = (FIXTURE / "log_calls.c").read_text(encoding="utf-8")
        for floor in range(4):
            obj = self.compile(f"macros-{floor}", source, {"NT_LOG_MIN_LEVEL": floor})
            data = obj.read_bytes()
            for level, name in enumerate(LEVELS):
                for form in FORMS:
                    marker = f"NT426_{name}_{form} %d".encode("ascii")
                    if (marker in data) != (level >= floor):
                        raise RuntimeError(f"floor {floor}: incorrect presence of {marker!r}")
            symbols = self.command(f"symbols-{floor}", [self.args.nm, "--defined-only", str(obj)])
            once_count = sum("nt_log_once_done_" in line for line in symbols.splitlines())
            if once_count != 2 * (3 - floor):
                raise RuntimeError(f"floor {floor}: expected {2 * (3 - floor)} ONCE latches, got {once_count}")
            undefined = self.command(f"undefined-{floor}", [self.args.nm, "--undefined-only", str(obj)])
            for symbol in ("diagnostics_argument", "nt_log_write", "nt_log_write_unique"):
                if bool(re.search(rf"\b{symbol}\b", undefined)) != (floor < 3):
                    raise RuntimeError(f"floor {floor}: incorrect reference to {symbol}")
        print("PASS: all 18 macro forms at floors 0..3; exact marker/latch/reference presence")

    def headers(self):
        self.compile("resource-header-without-timing", '#include "resource/nt_resource.h"\n', DEFINES)
        headers = {
            "NT_LOG_MIN_LEVEL": "log/nt_log.h",
            "NT_METRICS_ENABLED": "metrics/nt_metrics.h",
            "NT_LOG_RING_ENABLED": "log/nt_log_ring.h",
            "NT_INTROSPECT_ENABLED": "introspect/nt_introspect.h",
            "NT_INTROSPECT_WRITE_ENABLED": "introspect/nt_introspect.h",
            "NT_UI_TIMING_ENABLED": "ui/nt_ui.h",
            "NT_GFX_GPU_TIMING_ENABLED": "graphics/nt_gfx.h",
        }
        for define, header in headers.items():
            source = f'#include "{header}"\n'
            self.compile(f"present-{define}", source, DEFINES)
            missing = dict(DEFINES)
            del missing[define]
            self.compile(f"missing-{define}", source, missing, rf"error:[^\n]*{define}[^\n]*defined")
        for floor in (-1, 4):
            self.compile(f"invalid-floor-{floor}", '#include "log/nt_log.h"\n',
                         {"NT_LOG_MIN_LEVEL": floor}, r"error:[^\n]*NT_LOG_MIN_LEVEL must be in 0\.\.3")
        for floor in (0, 3):
            for level in LEVELS:
                for suffix in ("", "_ONCE", "_UNIQUE"):
                    macro = f"NT_LOG_{level}{suffix}"
                    source = f'#include "log/nt_log.h"\nvoid probe(void) {{ (void){macro}("domain"); }}\n'
                    # ONCE is a statement macro, so its positive control must be a statement.
                    if suffix == "_ONCE":
                        source = f'#include "log/nt_log.h"\nvoid probe(void) {{ {macro}("domain"); }}\n'
                    self.compile(f"domain-missing-{floor}-{macro}", source,
                                 {"NT_LOG_MIN_LEVEL": floor}, r"NT_LOG_DOMAIN not defined")
                    self.compile(f"domain-present-{floor}-{macro}", '#define NT_LOG_DOMAIN "probe"\n' + source,
                                 {"NT_LOG_MIN_LEVEL": floor})
        print("PASS: missing configuration, invalid floors and missing domains fail for the intended diagnostics")

    def cmake(self):
        work = Path(tempfile.mkdtemp(prefix="cmake-", dir=self.output)).resolve()
        source = work / "parent"
        shutil.copytree(FIXTURE, source)
        common = [self.args.cmake, "-S", str(source), "-G", self.args.generator,
                  f"-DENGINE_ROOT={ROOT.as_posix()}", f"-DCMAKE_C_COMPILER={self.args.cc}",
                  f"-DCMAKE_CXX_COMPILER={self.args.cxx}", "-DCMAKE_BUILD_TYPE=Release",
                  "-DNT_BUILD_TESTS=OFF", "-DNT_HTTP_CURL=OFF", "-DNT_STATIC_CRT=OFF",
                  "-DNT_DEVAPI_ENABLED=OFF", "-DNT_UI_DEBUG_TOOLS=OFF",
                  "-DNT_DEVAPI_GROUP_UI=ON", "-DNT_DEVAPI_GROUP_OBS=ON", "-DNT_DEVAPI_GROUP_ENTITY_WRITE=ON"]
        policies = {
            "off": {"NT_RESOURCE_TIMING_ENABLED": "OFF", "NT_LOG_MIN_LEVEL": "3", "NT_UI_TIMING_ENABLED": "OFF", "NT_GFX_GPU_TIMING_ENABLED": "OFF",
                    "NT_INTROSPECT_ENABLED": "ON", "NT_INTROSPECT_WRITE_ENABLED": "OFF",
                    "NT_METRICS_ENABLED": "OFF", "NT_LOG_RING_ENABLED": "OFF"},
            "on": {"NT_RESOURCE_TIMING_ENABLED": "ON", "NT_LOG_MIN_LEVEL": "1", "NT_UI_TIMING_ENABLED": "ON", "NT_GFX_GPU_TIMING_ENABLED": "ON",
                   "NT_INTROSPECT_ENABLED": "OFF", "NT_INTROSPECT_WRITE_ENABLED": "OFF",
                   "NT_METRICS_ENABLED": "ON", "NT_LOG_RING_ENABLED": "ON"},
        }
        for name, settings in policies.items():
            build = work / name
            args = [f"-D{k}={v}" for k, v in settings.items()]
            output = self.command(f"configure-{name}", common + ["-B", str(build), f"-DNT_PRESET_NAME=diagnostics-{name}"] + args)
            for block in output.split("\n\n"):
                if "CMake Warning" in block and "NT_DEVAPI" in block:
                    raise RuntimeError(f"disabled devapi emitted a warning:\n{block}")
            self.command(f"build-{name}", [self.args.cmake, "--build", str(build), "--target", "diagnostics_config"])
            for impl in ("real", "stub"):
                binary = (build / f"log_{impl}.path").read_text(encoding="utf-8").strip()
                output = self.command(f"run-{name}-{impl}", [binary])
                if "NT426_CONFIG_PASS" not in output:
                    raise RuntimeError(f"{name}/{impl}: missing behavioral success marker")
            missing = self.command(f"link-missing-{name}",
                                   [self.args.cmake, "--build", str(build), "--target", "log_missing"],
                                   r"undefined|unresolved|LNK2019|LNK1120")
            if "nt_log_write" not in missing:
                raise RuntimeError("missing implementation did not report nt_log_write")
            if name == "off":
                archive = (build / "log_archive.path").read_text(encoding="utf-8").strip()
                symbols = self.command("none-archive-symbols", [self.args.nm, "--defined-only", archive])
                undefined = self.command("none-archive-undefined", [self.args.nm, "--undefined-only", archive])
                if re.search(r"s_sinks|s_sink_user|s_sink_count|s_log_level|s_unique_", symbols):
                    raise RuntimeError("NONE logger retains runtime logger storage")
                if re.search(r"printf|nt_hash|malloc|calloc", undefined):
                    raise RuntimeError("NONE logger retains formatting/hash/allocation references")
        invalid = [
            ("ui-input", {"NT_DEVAPI_GROUP_INPUT": "OFF"}, r"NT_DEVAPI_GROUP_UI requires NT_DEVAPI_GROUP_INPUT"),
            ("ui-debug", {}, r"NT_DEVAPI_GROUP_UI requires NT_UI_DEBUG_TOOLS"),
            ("obs", {"NT_DEVAPI_GROUP_UI": "OFF", "NT_METRICS_ENABLED": "OFF"}, r"NT_DEVAPI_GROUP_OBS requires"),
            ("entity-write", {"NT_DEVAPI_GROUP_UI": "OFF", "NT_DEVAPI_GROUP_OBS": "OFF",
                              "NT_INTROSPECT_WRITE_ENABLED": "OFF"}, r"NT_DEVAPI_GROUP_ENTITY_WRITE requires NT_INTROSPECT_WRITE_ENABLED"),
        ]
        for name, settings, expected in invalid:
            build = work / f"invalid-{name}"
            self.command(f"configure-invalid-{name}", common + ["-B", str(build), "-DNT_DEVAPI_ENABLED=ON"]
                         + [f"-D{k}={v}" for k, v in settings.items()], expected)
        for value in ("-1", "4", "WARN", "1x"):
            self.command(f"configure-floor-{value}", common + ["-B", str(work / f"invalid-floor-{value}"),
                         f"-DNT_LOG_MIN_LEVEL={value}"], r"NT_LOG_MIN_LEVEL must be 0")
        if work.parent != self.output.resolve():
            raise RuntimeError(f"refusing to remove build directory outside evidence: {work}")
        shutil.rmtree(work)
        print("PASS: parent/sibling target propagation, real/stub linking, NONE archive, missing implementation and devapi guards")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "clang"))
    parser.add_argument("--cxx", default=os.environ.get("CXX", "clang++"))
    parser.add_argument("--nm", default="llvm-nm")
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--generator", default="Ninja")
    parser.add_argument("--compile-only", action="store_true", help="Run only isolated C header/object checks")
    parser.add_argument("--output", type=Path, default=ROOT / "build" / "_checks" / "diagnostics-config")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="run-", dir=args.output)).resolve()
    print(f"Diagnostics evidence: {output}", flush=True)
    checks = Checks(args, output)
    try:
        checks.macros()
        checks.headers()
        if not args.compile_only:
            checks.cmake()
    except (OSError, RuntimeError) as error:
        print(f"FAIL: {error}\nEvidence: {output}", file=sys.stderr)
        return 1
    print(f"PASS: {checks.count} tool invocations; evidence: {output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
