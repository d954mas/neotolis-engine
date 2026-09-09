#!/usr/bin/env python3
"""Build and test logging floors and independent timing producers under NDEBUG."""

import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
TARGETS = ("test_log_floor", "test_log_api", "test_log_api_stub", "test_log_ring",
           "test_introspect", "test_nt_ui_timing", "test_nt_gfx_gpu_timing_native")


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
             (2, "ON", "OFF", "OFF", 2), (3, "OFF", "ON", "OFF", 2),
             (2, "ON", "OFF", "OFF", 1), (3, "OFF", "ON", "OFF", 1),
             (2, "ON", "OFF", "ON", 2), (3, "OFF", "ON", "ON", 2))
    try:
        for floor, ui, gpu, metrics, asserts in cases:
            name = f"diagnostics-floor-{floor}" + ("-trap" if asserts == 1 else "")
            if floor >= 2 and metrics == "ON":
                name += "-obs"
            build = args.output / name
            # Existing consumer contract tests catch assertions through the FULL handler.
            targets = TARGETS + (("test_nt_ui_rich_parse", "test_font", "test_text_renderer",
                                  "test_mesh_renderer") if asserts == 2 else ())
            ui_debug = "ON" if floor == 1 else "OFF"
            obs = "ON" if metrics == "ON" else "OFF"
            if obs == "ON":
                targets += ("test_devapi_obs",)
            run(["cmake", "--preset", "native-release-test", "-B", str(build),
                 f"-DNT_PRESET_NAME={name}", f"-DNT_LOG_MIN_LEVEL={floor}",
                 f"-DNT_UI_TIMING_ENABLED={ui}", f"-DNT_GFX_GPU_TIMING_ENABLED={gpu}",
                 f"-DNT_METRICS_ENABLED={metrics}", f"-DNT_UI_DEBUG_TOOLS={ui_debug}",
                 "-DNT_LOG_RING_ENABLED=ON", "-DNT_INTROSPECT_ENABLED=ON",
                 "-DNT_INTROSPECT_WRITE_ENABLED=ON", f"-DNT_DEVAPI_ENABLED={obs}",
                 "-DNT_DEVAPI_GROUP_UI=OFF", f"-DNT_DEVAPI_GROUP_OBS={obs}",
                 f"-DNT_ASSERT_MODE={asserts}"], args.output / f"{name}-configure.log")
            run(["cmake", "--build", str(build), "--target", *targets], args.output / f"{name}-build.log")
            output = run(["ctest", "--test-dir", str(build), "--output-on-failure",
                          "-R", "^(" + "|".join(targets) + ")$"], args.output / f"{name}-test.log")
            if f"100% tests passed, 0 tests failed out of {len(targets)}" not in output:
                raise RuntimeError(f"{name}: expected all {len(targets)} registered tests\n{output}")
            print(f"PASS: floor={floor}, UI={ui}, GPU={gpu}, metrics={metrics}, inspector={ui_debug}, asserts={asserts}; {len(targets)} tests", flush=True)
    except (OSError, RuntimeError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
