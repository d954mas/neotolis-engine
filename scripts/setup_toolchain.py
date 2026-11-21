"""Install the toolchain pinned in .toolchain-versions into ~/.neotolis/toolchain/<tool>-<version>/.

    python scripts/setup_toolchain.py [llvm] [build] [emsdk]    (default: all)

llvm is clang, clang-tidy, clang-format, clangd, binutils and lld; build is CMake and Ninja; emsdk
is the Emscripten SDK. Local setup and every CI job run this script, so both use the same
binaries; one per-user install serves every checkout and worktree, and the versioned
directories let branches with different pins coexist. Inside GitHub Actions it also adds the
tools to GITHUB_PATH and GITHUB_ENV, as scripts/lib/toolchain.sh does for a local shell.
"""

import sys

if sys.version_info < (3, 14):
    sys.exit(f"setup_toolchain.py: Python 3.14+ required (compression.zstd), found {sys.version.split()[0]}")

import hashlib
import os
import platform
import shutil
import stat
import subprocess
import tarfile
import urllib.request
import zipfile
from compression import zstd
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLCHAIN_DIR = Path.home() / ".neotolis" / "toolchain"
WINDOWS = platform.system() == "Windows"
if platform.system() not in ("Windows", "Linux") or platform.machine().lower() not in ("x86_64", "amd64"):
    sys.exit(f"setup_toolchain.py: only Windows and Linux x86-64 are pinned, not {platform.system()} {platform.machine()}")
EXE = ".exe" if WINDOWS else ""
OS = "WINDOWS_X64" if WINDOWS else "LINUX_X64"

# Driver, format/lint/LSP tools, every binutil CMake looks up next to the compiler (a missing one
# falls back to PATH, mixing versions), the linker (lld-link on Windows, ld.lld on Linux), the
# targets the Linux archive symlinks to (llvm-readobj, lld) and the sanitizer symbolizer.
LLVM_TOOLS = (
    "clang", "clang++", "clang-format", "clang-tidy", "clangd", "clang-scan-deps",
    "llvm-ar", "llvm-ranlib", "llvm-nm", "llvm-objcopy", "llvm-objdump", "llvm-readelf", "llvm-readobj",
    "llvm-strip", "llvm-dlltool", "llvm-addr2line", "llvm-symbolizer", "llvm-rc", "llvm-mt",
) + (("lld-link",) if WINDOWS else ("lld", "ld.lld"))


def read_versions():
    versions = {}
    for line in (ROOT / ".toolchain-versions").read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            key, value = line.split("=", 1)
            versions[key] = value
    return versions


def fetch(url, sha256):
    """Download into a .part file and verify it; a partial .part is overwritten by the next run."""
    path = TOOLCHAIN_DIR / (url.rsplit("/", 1)[1] + ".part")
    print(f"Downloading {url}")
    digest = hashlib.sha256()
    with urllib.request.urlopen(url, timeout=60) as response, open(path, "wb") as out:
        while chunk := response.read(1 << 20):
            digest.update(chunk)
            out.write(chunk)
    if digest.hexdigest() != sha256:
        path.unlink()
        sys.exit(f"setup_toolchain.py: {url} sha256 {digest.hexdigest()} does not match the pin {sha256}")
    return path


def strip(name, top, keep):
    """The path of an archive member under top/ when keep() accepts it, else None."""
    rel = name[len(top) + 1:] if name.startswith(top + "/") else ""
    return rel if rel and keep(rel) else None


def install(dest, contents, unpack):
    """Run unpack(dest) unless the marker already records these contents. The marker is written
    last, so an interrupted install is redone from scratch."""
    marker = dest / ".installed"
    if marker.exists() and marker.read_text(encoding="utf-8") == contents:
        print(f"{dest.name} already installed")
        return
    if dest.exists():
        # git marks its object files read-only, which Windows refuses to delete.
        shutil.rmtree(dest, onexc=lambda func, path, _: (os.chmod(path, stat.S_IWRITE), func(path)))
    dest.mkdir(parents=True)
    unpack(dest)
    marker.write_text(contents, encoding="utf-8")
    print(f"{dest.name} installed in {dest}")


def extract_tar(archive, dest, top, keep, options=None):
    stream = zstd.ZstdFile(archive, options=options) if archive.name.endswith(".zst.part") else open(archive, "rb")
    with stream, tarfile.open(fileobj=stream, mode="r|*") as tar:
        for member in tar:
            if rel := strip(member.name, top, keep):
                member.name = rel
                tar.extract(member, dest, filter="data")
    archive.unlink()


def install_llvm(v):
    version = v["LLVM_VERSION"]
    dest = TOOLCHAIN_DIR / f"llvm-{version}"
    top = f"clang+llvm-{version}-x86_64-pc-windows-msvc" if WINDOWS else f"LLVM-{version}-Linux-X64"

    def keep(rel):
        if rel.startswith("lib/clang/"):
            return True  # builtin headers and runtime libraries
        tool = rel[4:].removesuffix(EXE) if rel.startswith("bin/") else ""
        # clang and clang++ are symlinks to the versioned clang-<major> on Linux.
        return tool in LLVM_TOOLS or (tool.startswith("clang-") and tool[6:].isdigit())

    def unpack(dest):
        archive = fetch(f"https://github.com/llvm/llvm-project/releases/download/llvmorg-{version}/{top}.tar.zst",
                        v[f"LLVM_SHA256_{OS}"])
        # The release archives use zstd long mode (128 MiB window), above the default decoder limit.
        extract_tar(archive, dest, top, keep, {zstd.DecompressionParameter.window_log_max: 31})
        # A tool missing from the archive would silently fall back to PATH, mixing versions.
        missing = [tool for tool in LLVM_TOOLS if not (dest / "bin" / (tool + EXE)).exists()]
        if missing:
            sys.exit(f"setup_toolchain.py: the LLVM {version} archive lacks {', '.join(missing)}")

    install(dest, "\n".join((version, *LLVM_TOOLS)) + "\n", unpack)
    return [dest / "bin"]


def install_build(v):
    cmake, ninja = v["CMAKE_VERSION"], v["NINJA_VERSION"]
    cmake_dest, ninja_dest = TOOLCHAIN_DIR / f"cmake-{cmake}", TOOLCHAIN_DIR / f"ninja-{ninja}"
    top = f"cmake-{cmake}-{'windows' if WINDOWS else 'linux'}-x86_64"

    def keep(rel):
        return rel.startswith(("bin/", "share/"))  # docs and man pages stay out

    def unpack_cmake(dest):
        archive = fetch(f"https://github.com/Kitware/CMake/releases/download/v{cmake}/{top}{'.zip' if WINDOWS else '.tar.gz'}",
                        v[f"CMAKE_SHA256_{OS}"])
        if not WINDOWS:
            extract_tar(archive, dest, top, keep)
            return
        with zipfile.ZipFile(archive) as z:
            for info in z.infolist():
                if rel := strip(info.filename, top, keep):
                    info.filename = rel
                    z.extract(info, dest)
        archive.unlink()

    def unpack_ninja(dest):
        archive = fetch(f"https://github.com/ninja-build/ninja/releases/download/v{ninja}/ninja-{'win' if WINDOWS else 'linux'}.zip",
                        v[f"NINJA_SHA256_{OS}"])
        with zipfile.ZipFile(archive) as z:
            z.extract(f"ninja{EXE}", dest)
        (dest / f"ninja{EXE}").chmod(0o755)  # zip keeps no exec bit
        archive.unlink()

    install(cmake_dest, cmake + "\n", unpack_cmake)
    install(ninja_dest, ninja + "\n", unpack_ninja)
    return [cmake_dest / "bin", ninja_dest]


def install_emsdk(v):
    version = v["EMSDK_VERSION"]
    dest = TOOLCHAIN_DIR / f"emsdk-{version}"

    def unpack(dest):
        # The emsdk tag of the same version pins the installer itself, not only the SDK.
        dest.rmdir()  # git clone wants to create it
        subprocess.run(["git", "clone", "--depth", "1", "--branch", version, "https://github.com/emscripten-core/emsdk.git",
                        str(dest)], check=True)
        for action in ("install", "activate"):
            subprocess.run([sys.executable, str(dest / "emsdk.py"), action, version], check=True, cwd=dest)

    install(dest, version + "\n", unpack)
    return [dest, dest / "upstream" / "emscripten"], {"EMSDK": dest.as_posix(), "EM_CONFIG": (dest / ".emscripten").as_posix()}


def main():
    wanted = sys.argv[1:] or ["llvm", "build", "emsdk"]
    unknown = set(wanted) - {"llvm", "build", "emsdk"}
    if unknown:
        sys.exit(f"setup_toolchain.py: unknown component(s) {', '.join(sorted(unknown))}")
    TOOLCHAIN_DIR.mkdir(parents=True, exist_ok=True)
    versions = read_versions()
    paths, env = [], {}
    if "build" in wanted:
        paths += install_build(versions)
    if "llvm" in wanted:
        paths += install_llvm(versions)
    if "emsdk" in wanted:
        emsdk_paths, env = install_emsdk(versions)
        paths += emsdk_paths
    if os.environ.get("GITHUB_PATH"):
        with open(os.environ["GITHUB_PATH"], "a", encoding="utf-8") as out:
            out.writelines(p.as_posix() + "\n" for p in paths)
        with open(os.environ["GITHUB_ENV"], "a", encoding="utf-8") as out:
            out.writelines(f"{key}={value}\n" for key, value in env.items())


if __name__ == "__main__":
    main()
