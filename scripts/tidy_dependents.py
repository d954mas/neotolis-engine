#!/usr/bin/env python3
"""Print the .c files to clang-tidy after the given headers changed, or FULL."""

import json
from pathlib import Path
import subprocess
import sys

from check_logic import tidy_dependents


def main():
    deps_dir, db_dir, headers = sys.argv[1], sys.argv[2], sys.argv[3:]
    root = Path(__file__).resolve().parent.parent
    deps = subprocess.run(["ninja", "-C", deps_dir, "-t", "deps"], check=True, text=True,
                          stdout=subprocess.PIPE, encoding="utf-8", errors="replace").stdout
    db = json.loads((Path(db_dir) / "compile_commands.json").read_text(encoding="utf-8"))
    sources = {entry["file"] for entry in db if entry["file"].endswith(".c")}
    result = tidy_dependents(deps.splitlines(), sources, [str(root / h) for h in headers])
    if result is None:
        print("FULL")
        return
    prefix = str(root).replace("\\", "/").lower() + "/"
    for path in sorted(result):
        normalized = path.replace("\\", "/")
        if normalized.lower().startswith(prefix):
            print(normalized[len(prefix):])


if __name__ == "__main__":
    main()
