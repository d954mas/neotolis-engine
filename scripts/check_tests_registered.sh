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

# Executable stems referenced by any add_test in the generated CTest files.
tested = set()
add_test_re = re.compile(r"add_test\((.*)$")
for path in glob.glob(build_dir + "/**/CTestTestfile.cmake", recursive=True):
    for line in open(path, encoding="utf-8", errors="replace"):
        m = add_test_re.search(line)
        if m:
            for tok in re.findall(r'"([^"]+)"|\[=+\[(.*?)\]=+\]', m.group(1)):
                for t in tok:
                    if t:
                        tested.add(os.path.splitext(os.path.basename(t))[0])

# Config-gated tests (e.g. sanitizer_proof off on Windows+Clang) are absent
# from this config's DB — accept those on the weaker textual evidence of a
# source-path reference; configs where they build still get the strong check.
cml = open("tests/CMakeLists.txt", encoding="utf-8").read()

failed = []
gated = 0
for f in sorted(glob.glob("tests/unit/test_*.c")):
    rel = f.replace("\\", "/")
    targets = src_targets.get(rel.lower())
    if not targets:
        if "unit/" + os.path.basename(rel) in cml:
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
suffix = f", {gated} config-gated accepted textually" if gated else ""
print(f"check_tests_registered: passed ({total} test sources built and registered{suffix})")
PY
