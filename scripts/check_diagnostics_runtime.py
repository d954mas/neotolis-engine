#!/usr/bin/env python3
"""Build and test logging floors and independent timing producers under NDEBUG."""

import argparse
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import subprocess
import shutil
import sys

ROOT = Path(__file__).resolve().parent.parent
TARGETS = ("test_log_floor", "test_log_api", "test_log_api_stub", "test_log_ring",
           "test_introspect", "test_resource_timing", "test_nt_ui_timing", "test_nt_gfx_gpu_timing_native", "test_gfx_stub")
CRASH_TARGETS = ("test_crash_record", "test_crash_stub_link")
CRASH_TESTS = ("test_crash_stub_link",) + tuple(f"test_crash_record_{mode}{suffix}" for mode in ("trap", "abort", "segv")
                                                for suffix in ("", "_check"))


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
    cases = ((0, "ON", "ON", "ON", 2, "OFF"), (1, "OFF", "OFF", "ON", 2, "ON"),
             (2, "ON", "OFF", "OFF", 2, "ON"), (3, "OFF", "ON", "OFF", 2, "OFF"),
             (2, "ON", "OFF", "OFF", 1, "ON"), (3, "OFF", "ON", "OFF", 1, "OFF"),
             (2, "ON", "OFF", "ON", 2, "ON"), (3, "OFF", "ON", "ON", 2, "OFF"))
    rows = []
    for floor, ui, gpu, metrics, asserts, resource in cases:
        name = f"diagnostics-floor-{floor}" + ("-trap" if asserts == 1 else "")
        if floor >= 2 and metrics == "ON":
            name += "-obs"
        # Existing consumer contract tests catch assertions through the FULL handler.
        targets = TARGETS + (("test_nt_ui_rich_parse", "test_font", "test_text_renderer",
                              "test_mesh_renderer", "test_gfx", "test_ui_menu", "test_ui_dropdown") if asserts == 2 else ())
        ui_debug = "ON" if floor == 1 else "OFF"
        ui_checks = "ON" if floor == 0 else "OFF"
        obs = "ON" if metrics == "ON" else "OFF"
        capture = gpu
        if asserts == 1 or (floor >= 2 and metrics == "ON"):
            capture = "OFF" if gpu == "ON" else "ON"
        if obs == "ON":
            targets += ("test_devapi_obs",)
        if floor >= 2 and metrics == "ON":
            targets = ("test_devapi_obs", "test_resource_timing", "test_nt_ui_timing", "test_nt_gfx_gpu_timing_native")
        targets += ("test_gfx_observe", "test_nt_gfx_observe_native", "test_gfx_stub") if "test_gfx_stub" not in targets else ("test_gfx_observe", "test_nt_gfx_observe_native")
        tests = targets
        if floor == 3:
            targets += CRASH_TARGETS
            tests += CRASH_TESTS
        configure = ["cmake", "--preset", "native-release-test", "-B", str(args.output / name),
                     f"-DNT_PRESET_NAME={name}", f"-DNT_LOG_MIN_LEVEL={floor}",
                     f"-DNT_RESOURCE_TIMING_ENABLED={resource}", f"-DNT_UI_TIMING_ENABLED={ui}", f"-DNT_GFX_GPU_TIMING_ENABLED={gpu}",
                     f"-DNT_GFX_CAPTURE_ENABLED={capture}",
                     f"-DNT_METRICS_ENABLED={metrics}", f"-DNT_UI_DEBUG_TOOLS={ui_debug}", f"-DNT_UI_CHECKS={ui_checks}",
                     "-DNT_LOG_RING_ENABLED=ON", "-DNT_INTROSPECT_ENABLED=ON",
                     "-DNT_INTROSPECT_WRITE_ENABLED=ON", f"-DNT_DEVAPI_ENABLED={obs}",
                     "-DNT_DEVAPI_GROUP_UI=OFF", f"-DNT_DEVAPI_GROUP_OBS={obs}",
                     f"-DNT_ASSERT_MODE={asserts}",
                     # No target uses HTTP; vendored curl would double each configure.
                     "-DNT_HTTP_CURL=OFF"]
        summary = (f"floor={floor}, UI={ui}, GPU={gpu}, capture={capture}, resource={resource}, metrics={metrics}, "
                   f"inspector={ui_debug}, UI checks={ui_checks}, asserts={asserts}; {len(tests)} tests")
        rows.append((name, args.output / name, targets, tests, capture, configure, summary))
    try:
        # Configures share the source tree's pack manifests, and GL tests share one display:
        # only the builds run concurrently.
        for name, build, targets, tests, capture, configure, summary in rows:
            run(configure, args.output / f"{name}-configure.log")
        with ThreadPoolExecutor(max_workers=len(rows)) as pool:
            builds = [pool.submit(run, ["cmake", "--build", str(build), "--target", *targets], args.output / f"{name}-build.log")
                      for name, build, targets, tests, capture, configure, summary in rows]
            for future in builds:
                future.result()
        for name, build, targets, tests, capture, configure, summary in rows:
            output = run(["ctest", "--test-dir", str(build), "--output-on-failure",
                          "-R", "^(" + "|".join(tests) + ")$"], args.output / f"{name}-test.log")
            if f"100% tests passed, 0 tests failed out of {len(tests)}" not in output:
                raise RuntimeError(f"{name}: expected all {len(tests)} registered tests\n{output}")
            nm = shutil.which("llvm-nm") or shutil.which("nm")
            if nm:
                libraries = list((ROOT / "build" / "engine" / name).glob("*nt_gfx.*"))
                library = next((path for path in libraries if path.suffix in (".a", ".lib")), None)
                if library is None:
                    raise RuntimeError(f"{name}: nt_gfx archive not found among {libraries}")
                symbols = run([nm, "--defined-only", str(library)], args.output / f"{name}-symbols.log")
                # Extern symbols only: statics and inline helpers may vanish from either build.
                for producer in ("g_nt_gfx_capture", "nt_gfx_backend_capture_initial_state", "nt_gfx_capture_read"):
                    if capture == "OFF" and producer in symbols:
                        raise RuntimeError(f"{name}: disabled producer remains: {producer}")
                    if capture == "ON" and producer not in symbols:
                        # Positive control: the absence check must be able to see these symbols.
                        raise RuntimeError(f"{name}: enabled producer {producer} is missing from the symbol listing")
            else:
                print(f"UNVERIFIED: {name} producer symbols; install llvm-nm or nm", flush=True)
            print(f"PASS: {summary}", flush=True)
    except (OSError, RuntimeError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
