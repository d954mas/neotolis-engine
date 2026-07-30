import glob
import json
import re
import sys
from pathlib import Path

from check_logic import (
    allowed_missing_tests,
    cmake_generated_value,
    ctest_command_stems,
    missing_registered_targets,
)


def main():
    build_dir = Path(sys.argv[1])
    root = Path.cwd().resolve().as_posix().lower().rstrip("/") + "/"
    src_targets = {}
    compile_db = json.loads((build_dir / "compile_commands.json").read_text(encoding="utf-8"))
    for entry in compile_db:
        source = entry["file"].replace("\\", "/").lower()
        if source.startswith(root):
            source = source[len(root) :]
        target = re.search(
            r"CMakeFiles[\\/]+([^\\/]+)\.dir",
            entry.get("command", "") + entry.get("output", ""),
        )
        if target:
            src_targets.setdefault(source, set()).add(target.group(1))

    registered = set()
    for path in build_dir.glob("**/CTestTestfile.cmake"):
        registered.update(
            ctest_command_stems(path.read_text(encoding="utf-8", errors="replace").splitlines())
        )

    system_name = cmake_generated_value(build_dir, "CMakeSystem.cmake", "CMAKE_SYSTEM_NAME")
    compiler_id = cmake_generated_value(build_dir, "CMakeCCompiler.cmake", "CMAKE_C_COMPILER_ID")
    allowed_missing = allowed_missing_tests(system_name, compiler_id)

    failed = []
    gated = 0
    sources = sorted(glob.glob("tests/unit/test_*.c"))
    for source_path in sources:
        relative = source_path.replace("\\", "/")
        targets = src_targets.get(relative.lower())
        if not targets:
            if relative in allowed_missing:
                gated += 1
            else:
                failed.append(f"{relative}: not compiled by any target (missing add_executable?)")
            continue
        missing = missing_registered_targets(targets, registered)
        if missing:
            failed.append(f"{relative}: targets missing add_test: {sorted(missing)}")

    if failed:
        print("ERROR: test source(s) that never run under ctest:")
        for message in failed:
            print("  " + message)
        print("check_tests_registered: FAILED")
        return 1

    suffix = f", {gated} config-gated allowlisted" if gated else ""
    print(f"check_tests_registered: passed ({len(sources)} test sources built and registered{suffix})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
