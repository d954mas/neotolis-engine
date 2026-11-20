"""Install the LLVM release pinned in .toolchain-versions into ~/.neotolis/toolchain/llvm-<version>/.

Local setup and every CI job run this script, so both build, format and lint with the
same binaries. One per-user install serves every checkout and worktree; the versioned
directory lets branches with different pins coexist. Only the tools the engine uses are
extracted from the release archive.
"""

import hashlib
import os
import platform
import shutil
import sys
import tarfile
import urllib.request
from compression import zstd
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# USERPROFILE on Windows: the same directory CMake (nt_llvm_toolchain.cmake) and bash resolve.
TOOLCHAIN_DIR = Path(os.environ.get("USERPROFILE") or Path.home()) / ".neotolis" / "toolchain"
RELEASES = "https://github.com/llvm/llvm-project/releases/download"

# Driver, format/lint/LSP tools, every binutil CMake looks up next to the compiler (a missing
# one falls back to PATH, mixing versions), the Windows linker CMake selects (-fuse-ld=lld-link)
# and the sanitizer symbolizer. lib/clang holds the builtin headers and runtime libraries.
TOOLS = (
    "clang", "clang++", "clang-format", "clang-tidy", "clangd", "clang-scan-deps",
    "llvm-ar", "llvm-ranlib", "llvm-nm", "llvm-objcopy", "llvm-objdump", "llvm-readelf",
    "llvm-strip", "llvm-dlltool", "llvm-addr2line", "llvm-symbolizer", "llvm-rc", "llvm-mt", "lld-link",
)


def read_versions():
    versions = {}
    for line in (ROOT / ".toolchain-versions").read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            key, value = line.split("=", 1)
            versions[key] = value
    return versions


def archive_spec(versions):
    version = versions["LLVM_VERSION"]
    system, machine = platform.system(), platform.machine().lower()
    if system == "Linux" and machine in ("x86_64", "amd64"):
        return f"LLVM-{version}-Linux-X64", versions["LLVM_SHA256_LINUX_X64"], ""
    if system == "Windows" and machine in ("x86_64", "amd64"):
        return f"clang+llvm-{version}-x86_64-pc-windows-msvc", versions["LLVM_SHA256_WINDOWS_X64"], ".exe"
    sys.exit(f"setup_llvm.py: no pinned LLVM archive for {system} {machine}")


def download(url, path):
    print(f"Downloading {url}")
    with urllib.request.urlopen(url) as response, open(path, "wb") as out:
        shutil.copyfileobj(response, out, 1 << 20)


def verify(path, sha256):
    digest = hashlib.sha256()
    with open(path, "rb") as file:
        while chunk := file.read(1 << 20):
            digest.update(chunk)
    if digest.hexdigest() != sha256:
        path.unlink()
        sys.exit(f"setup_llvm.py: {path.name} sha256 {digest.hexdigest()} does not match the pin {sha256}")


def wanted(name, top, exe):
    rel = name[len(top) + 1:] if name.startswith(top + "/") else None
    if rel is None:
        return False
    if rel.startswith("lib/clang/"):
        return True
    if rel.startswith("bin/"):
        tool = rel[4:]
        if exe and tool.endswith(exe):
            tool = tool[: -len(exe)]
        # clang++ and friends are symlinks to the versioned clang-<major> on Linux.
        return tool in TOOLS or (tool.startswith("clang-") and tool[6:].isdigit())
    return False


def main():
    versions = read_versions()
    version = versions["LLVM_VERSION"]
    top, sha256, exe = archive_spec(versions)
    dest = TOOLCHAIN_DIR / f"llvm-{version}"
    # The marker records what was extracted, so a changed tool list reinstalls.
    marker = dest / ".installed"
    contents = "\n".join((version, *TOOLS)) + "\n"
    if marker.exists() and marker.read_text(encoding="utf-8") == contents:
        print(f"LLVM {version} already installed in {dest}")
        return
    if dest.exists():
        shutil.rmtree(dest)
    archive = TOOLCHAIN_DIR / f"{top}.tar.zst"
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        download(f"{RELEASES}/llvmorg-{version}/{top}.tar.zst", archive)
    verify(archive, sha256)

    staging = TOOLCHAIN_DIR / f".staging-{version}"
    if staging.exists():
        shutil.rmtree(staging)
    print(f"Extracting {archive.name}")
    # The release archives use zstd long mode (128 MiB window), above the default decoder limit.
    options = {zstd.DecompressionParameter.window_log_max: 31}
    with zstd.ZstdFile(archive, options=options) as stream, tarfile.open(fileobj=stream, mode="r|") as tar:
        for member in tar:
            if wanted(member.name, top, exe):
                tar.extract(member, staging, filter="data")
    (staging / top).rename(dest)
    staging.rmdir()
    archive.unlink()
    marker.write_text(contents, encoding="utf-8")
    print(f"LLVM {version} installed in {dest}")


if __name__ == "__main__":
    main()
