#!/usr/bin/env python3
"""Build and test logging floors and independent timing producers under NDEBUG."""

import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
TARGETS = ("test_log_floor", "test_introspect", "test_nt_ui_timing", "test_nt_gfx_gpu_timing_native")


def run(command, log):
    result = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, encoding="utf-8", errors="replace")
    log.write_text(result.stdout, encoding="utf-8")
    if result.returncode:
        raise RuntimeError(f"{command}\n{result.stdout[-6000:]}\nFull log: {log}")
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build" / "_checks" / "diagnostics-runtime")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    # Both independent combinations are exercised without a metrics collector.
    cases = ((0, "ON", "ON", "ON", 2), (1, "OFF", "OFF", "ON", 2),
             (2, "ON", "OFF", "OFF", 1), (3, "OFF", "ON", "OFF", 1))
    try:
        for floor, ui, gpu, metrics, asserts in cases:
            name = f"diagnostics-floor-{floor}"
            build = args.output / name
            run(["cmake", "--preset", "native-release-test", "-B", str(build),
                 f"-DNT_PRESET_NAME={name}", f"-DNT_LOG_MIN_LEVEL={floor}",
                 f"-DNT_UI_TIMING_ENABLED={ui}", f"-DNT_GFX_GPU_TIMING_ENABLED={gpu}",
                 f"-DNT_METRICS_ENABLED={metrics}", "-DNT_UI_DEBUG_TOOLS=OFF",
                 "-DNT_LOG_RING_ENABLED=OFF", "-DNT_INTROSPECT_ENABLED=ON",
                 "-DNT_INTROSPECT_WRITE_ENABLED=ON", "-DNT_DEVAPI_ENABLED=OFF",
                 f"-DNT_ASSERT_MODE={asserts}"], args.output / f"{name}-configure.log")
            run(["cmake", "--build", str(build), "--target", *TARGETS], args.output / f"{name}-build.log")
            output = run(["ctest", "--test-dir", str(build), "--output-on-failure",
                          "-R", "^(" + "|".join(TARGETS) + ")$"], args.output / f"{name}-test.log")
            if "100% tests passed, 0 tests failed out of 4" not in output:
                raise RuntimeError(f"{name}: expected all four registered tests\n{output}")
            print(f"PASS: floor={floor}, UI={ui}, GPU={gpu}, metrics={metrics}, asserts={asserts}; 4 tests", flush=True)
    except (OSError, RuntimeError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
