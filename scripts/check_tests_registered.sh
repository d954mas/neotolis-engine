#!/usr/bin/env bash
# Gate: every tests/unit/test_*.c must be BUILT (present in the compile DB) and
# RUN (its target appears in a generated CTestTestfile add_test) — a file that
# is merely mentioned in tests/CMakeLists.txt, or compiled but never add_test'ed,
# silently never runs. Uses a configured build dir as the authority, not text
# matching. Pass a build dir with devapi groups ON (tidy-ci locally, the CI
# lint configure in CI) so devapi-gated tests are visible.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="${1:-build/_cmake/tidy-ci}"
if [ ! -f "$BUILD_DIR/compile_commands.json" ]; then
    echo "ERROR: $BUILD_DIR/compile_commands.json not found — configure it first"
    echo "  (check.sh configures build/_cmake/tidy-ci automatically)"
    exit 1
fi

python - "$BUILD_DIR" << 'PY'
import glob, json, os, re, sys

build_dir = sys.argv[1]

# source (repo-relative, lowercase) -> target names, from "CMakeFiles/<target>.dir" obj paths.
root = os.getcwd().replace("\\", "/").lower().rstrip("/") + "/"
src_targets = {}
for e in json.load(open(build_dir + "/compile_commands.json")):
    f = e["file"].replace("\\", "/").lower()
    if f.startswith(root):
        f = f[len(root):]
    m = re.search(r"CMakeFiles[\\/]+([^\\/]+)\.dir", e.get("command", "") + e.get("output", ""))
    if m:
        src_targets.setdefault(f, set()).add(m.group(1))

# Executable stems referenced by any add_test COMMAND in the generated CTest
# files. The FIRST argument is the test NAME, not a command — counting it would
# let `add_test([=[test_foo]=] "python" ...)` falsely confirm target test_foo.
tested = set()
add_test_re = re.compile(r'add_test\(\s*(?:\[=+\[.*?\]=+\]|"[^"]*")\s*(.*)$')
for path in glob.glob(build_dir + "/**/CTestTestfile.cmake", recursive=True):
    for line in open(path, encoding="utf-8", errors="replace"):
        m = add_test_re.search(line)
        if m:
            for tok in re.findall(r'"([^"]+)"|\[=+\[(.*?)\]=+\]', m.group(1)):
                for t in tok:
                    if t:
                        tested.add(os.path.splitext(os.path.basename(t))[0])

# Explicit allowlist of config-gated tests: legitimately absent from THIS
# config's build graph; a config that does build them (CI Linux) still applies
# the strong check. A textual-mention fallback was rejected — comments and dead
# CMake branches would false-green it.
CONFIG_GATED = {
    "tests/unit/test_sanitizer_proof.c",  # off on WIN32+Clang (sanitizers disabled)
}

failed = []
gated = 0
for f in sorted(glob.glob("tests/unit/test_*.c")):
    rel = f.replace("\\", "/")
    targets = src_targets.get(rel.lower())
    if not targets:
        if rel in CONFIG_GATED:
            gated += 1
        else:
            failed.append(f"{rel}: not compiled by any target (missing add_executable?)")
    elif not (targets & tested):
        failed.append(f"{rel}: built into {sorted(targets)} but no add_test runs it")

if failed:
    print("ERROR: test source(s) that never run under ctest:")
    for msg in failed:
        print("  " + msg)
    print("check_tests_registered: FAILED")
    sys.exit(1)
total = len(glob.glob("tests/unit/test_*.c"))
suffix = f", {gated} config-gated allowlisted" if gated else ""
print(f"check_tests_registered: passed ({total} test sources built and registered{suffix})")
PY
