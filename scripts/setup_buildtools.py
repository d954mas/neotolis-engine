"""Install the CMake and Ninja pinned in .toolchain-versions into a venv under ~/.neotolis/toolchain/.

The official PyPI wheels give the same CMake and Ninja on Windows and Linux, locally and in
CI. One per-user venv serves every checkout and worktree; its versioned name lets branches with
different pins coexist. scripts/lib/toolchain.sh puts its bin directory first on PATH.
"""

import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# USERPROFILE on Windows: the same directory scripts/lib/toolchain.sh resolves.
TOOLCHAIN_DIR = Path(os.environ.get("USERPROFILE") or Path.home()) / ".neotolis" / "toolchain"


def read_versions():
    versions = {}
    for line in (ROOT / ".toolchain-versions").read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            key, value = line.split("=", 1)
            versions[key] = value
    return versions


def main():
    versions = read_versions()
    cmake, ninja = versions["CMAKE_VERSION"], versions["NINJA_VERSION"]
    dest = TOOLCHAIN_DIR / f"buildtools-cmake-{cmake}-ninja-{ninja}"
    marker = dest / ".installed"
    if marker.exists():
        print(f"CMake {cmake} and Ninja {ninja} already installed in {dest}")
        return
    subprocess.run([sys.executable, "-m", "venv", "--clear", str(dest)], check=True)
    python = dest / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    subprocess.run([str(python), "-m", "pip", "install", "--quiet", "--disable-pip-version-check",
                    "--only-binary=:all:", f"cmake=={cmake}", f"ninja=={ninja}"], check=True)
    marker.write_text(f"{cmake}\n{ninja}\n", encoding="utf-8")
    print(f"CMake {cmake} and Ninja {ninja} installed in {dest}")


if __name__ == "__main__":
    main()
