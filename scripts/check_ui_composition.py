"""Build and verify optional UI link composition using production libraries."""

import argparse
import math
from pathlib import Path
import re
import shutil
import subprocess


CURVES = tuple("nt_ui_rich_fx_" + name for name in (
    "wave", "shake", "rainbow", "pulse", "fade_in", "bounce", "glow", "sway"))
TARGETS = ("ui_plain", "ui_custom", "ui_wave", "ui_all_fx")


def run(args):
    result = subprocess.run(args, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path)
    parser.add_argument("--nm", default=shutil.which("llvm-nm"))
    parser.add_argument("--node", default=shutil.which("node"))
    args = parser.parse_args()
    build = args.build_dir.resolve()
    run(["cmake", "--build", str(build), "--target", *TARGETS, "ui_font_contract"])
    executables = (build / "ui/executables.txt").read_text(encoding="utf-8").splitlines()
    assert len(executables) == 4
    for index, (target, executable) in enumerate(zip(TARGETS, executables)):
        wasm = Path(executable).suffix == ".js"
        if wasm:
            assert args.node, "Node is required to execute the WASM witnesses"
        command = [args.node, executable] if wasm else [executable]
        values = []
        for extra in ([], ["second-time"]):
            output = run(command + extra)
            match = re.search(r"^ui-composition (\S+)$", output, re.M)
            assert match, output
            values.append(float(match[1]))
        assert all(math.isfinite(v) for v in values), values
        if index == 0:
            assert values == [1.0, 1.0], values
        elif index == 1:
            assert values == [5.75, 6.0], values
        else:
            assert values[0] != values[1], (target, values)
        if wasm:
            paths = (build / "ui/out" / (target + ".paths")).read_text(encoding="utf-8").splitlines()
            assert paths[0] == executable and len(paths) == 3
            names = {line.split(":", 1)[1] for line in Path(paths[2]).read_text(encoding="utf-8").splitlines() if ":" in line}
            assert {"nt_ui_walk", "nt_ui_label"} <= names, (target, "UI roots missing")
            assert ("nt_ui_rich_text" in names) == (index > 0), (target, "rich reachability")
            expected = set() if index < 2 else ({CURVES[0]} if index == 2 else set(CURVES))
            assert names.intersection(CURVES) == expected, (target, names.intersection(CURVES))
            assert "nt_ui_rich_fx_stock" not in names, target
            if index == 0:
                assert not any(name.startswith("nt_ui_rich_") for name in names), target
            print(f"{target}: wasm={Path(paths[1]).stat().st_size} js={Path(paths[0]).stat().st_size} checksum={values}")
        else:
            print(f"{target}: checksum={values}")
    assert args.nm, "llvm-nm is required for archive dependency checks"
    archives = (build / "ui/archives.txt").read_text(encoding="utf-8").splitlines()
    ui_refs = run([args.nm, "--undefined-only", "--extern-only", archives[0]])
    rich_refs = run([args.nm, "--undefined-only", "--extern-only", archives[1]])
    assert "nt_ui_rich_" not in ui_refs, ui_refs
    assert not any(name in rich_refs for name in (*CURVES, "nt_ui_rich_fx_stock")), rich_refs
    negative = subprocess.run(["cmake", "--build", str(build), "--target", "ui_missing_rich"],
                              capture_output=True, text=True, encoding="utf-8", errors="replace")
    output = negative.stdout + negative.stderr
    (build / "ui-missing-rich.log").write_text(output, encoding="utf-8")
    assert negative.returncode != 0, "missing rich module unexpectedly linked"
    assert re.search(r"(?:undefined symbol:|undefined reference to|unresolved external symbol)[^\r\n]*\bnt_ui_rich_text\b", output), output
    contract = (build / "ui/font-contract.txt").read_text(encoding="utf-8").strip()
    command = [args.node, contract] if contract.endswith(".js") else [contract]
    assert "font-contract-accepted" in run(command)
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    assert "NT_FONT_EMBOLDEN_ENABLED:BOOL=OFF" in cache, "composition check requires default embolden OFF"
    assert "NT_ASSERT_MODE:STRING=0\n" not in cache, "rejection requires supported assertions"
    for mode in ("positive", "negative", "tiny", "outline"):
        rejected = subprocess.run(command + [mode], capture_output=True, text=True, encoding="utf-8", errors="replace")
        assert "font-contract-ready" in rejected.stdout, (mode, rejected.stdout, rejected.stderr)
        assert rejected.returncode != 0 and "font-contract-accepted" not in rejected.stdout, mode
    print("PASS: font zero/reset accepted; nonzero weight and transparent outline rejected in subprocesses")
    print("PASS: selected modules, effect execution, archive references and missing-rich rejection")


if __name__ == "__main__":
    main()
