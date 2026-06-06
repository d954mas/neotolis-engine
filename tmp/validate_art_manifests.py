import re
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
MANIFESTS = [
    ROOT / "gamedesign/assets/concept/production_batch_a/batch_a_runtime_manifest.md",
    ROOT / "gamedesign/assets/concept/production_batch_b/batch_b_runtime_manifest.md",
    ROOT / "gamedesign/assets/concept/production_batch_c/batch_c_runtime_manifest.md",
]


LINE_RE = re.compile(r"- `([^`]+)` [—-] (\d+)x(\d+), ([A-Z]+)")


def main() -> None:
    total = 0
    problems: list[str] = []
    for manifest in MANIFESTS:
        text = manifest.read_text(encoding="utf-8")
        count = 0
        for line in text.splitlines():
            match = LINE_RE.match(line)
            if not match:
                continue
            rel, w_text, h_text, mode = match.groups()
            path = ROOT / rel
            count += 1
            total += 1
            if not path.exists():
                problems.append(f"missing {rel}")
                continue
            with Image.open(path) as img:
                expected_size = (int(w_text), int(h_text))
                if img.size != expected_size:
                    problems.append(f"bad size {rel}: {img.size} != {expected_size}")
                if img.mode != mode:
                    problems.append(f"bad mode {rel}: {img.mode} != {mode}")
        if count == 0:
            problems.append(f"no manifest file entries parsed in {manifest}")
        else:
            print(f"OK {manifest.name}: {count} entries")
    if problems:
        print("FAILED")
        for problem in problems:
            print(problem)
        raise SystemExit(1)
    print(f"OK total manifest entries: {total}")


if __name__ == "__main__":
    main()
