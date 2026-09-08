"""Compare Clay arena requirements from two native builds with identical test caps."""

import argparse
import re
import subprocess


def arena_size(build_dir):
    result = subprocess.run(
        ["ctest", "--test-dir", build_dir, "--verbose", "-R", "^test_nt_ui_clay_debug_view$"],
        capture_output=True, text=True, encoding="utf-8", errors="replace")
    output = result.stdout + result.stderr
    if result.returncode:
        raise SystemExit(output)
    sizes = re.findall(r"clay_arena_bytes=(\d+)", output)
    if len(sizes) != 1:
        raise SystemExit(f"Expected one Clay arena measurement in {build_dir}:\n{output}")
    return int(sizes[0])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("off_build")
    parser.add_argument("on_build")
    args = parser.parse_args()
    off = arena_size(args.off_build)
    on = arena_size(args.on_build)
    if not 0 < off < on:
        raise SystemExit(f"Clay arena exclusion failed: OFF={off}, ON={on}; expected 0 < OFF < ON")
    print(f"PASS: Clay arena OFF={off}, ON={on}, saved={on - off} bytes")


if __name__ == "__main__":
    main()
