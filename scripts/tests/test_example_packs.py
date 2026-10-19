import fnmatch
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[2]


FIXTURE_BUILDER = r"""import json
from pathlib import Path

import fnmatch
import os

source = Path(SOURCE)
patterns = [source / "raw" / "*.txt", source / "later" / "*.txt"]
globs = ""
reads = []
for pattern in patterns:
    # Like nt_builder_glob_iterate: case-sensitive names, files only, sorted.
    names = sorted(n for n in (os.listdir(pattern.parent) if pattern.parent.is_dir() else [])
                   if fnmatch.fnmatchcase(n, pattern.name) and (pattern.parent / n).is_file())
    globs += pattern.resolve().as_posix() + "\n" + "".join("  " + n + "\n" for n in names)
    reads += [pattern.parent / n for n in names]
reads.append(source / "shared shaders" / "shader.txt")
race = source / "race"
if race.exists():
    # A file that lands after the glob, while the builder still works.
    race.unlink()
    (source / "raw" / "late.txt").write_text("late")
out = Path(__file__).parent
files = {p.relative_to(source).as_posix(): p.read_text() for p in reads}
(out / "fixture.ntpack").write_text(json.dumps(files, sort_keys=True))


def esc(path):
    text = path.resolve().as_posix()
    return text.replace(" ", "\\ ").replace("#", "\\#").replace("$", "$$")


depfile = out / "build_packs.d"
inputs = sorted({esc(p) for p in reads})
depfile.write_text(esc(depfile) + ":" + "".join(" \\\n  " + i for i in inputs) + "\n", newline="\n")
(out / "build_packs.d.globs").write_text(globs, newline="\n")
"""


class ExamplePacksTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="nt packs ")
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name) / "source"
        self.build = Path(self.temp.name) / "build"
        self.packs = Path(self.temp.name) / "packs"
        self.source.mkdir()
        self.packs.mkdir()
        self.raw = self.source / "raw"
        self.raw.mkdir()
        self.later = self.source / "later"
        self.shared = self.source / "shared shaders"
        self.shared.mkdir()
        self.asset = self.raw / "font.txt"
        self.asset.write_text("original", encoding="utf-8")
        (self.shared / "shader.txt").write_text("shader", encoding="utf-8")
        # Python accepts a directory containing __main__.py as its program.
        # This fixture exercises the real CMake build/copy graph without codecs:
        # it globs raw/*.txt and the missing later/*.txt, reads one shared shader
        # and writes the depfile and glob list the way nt_builder_write_depfile does.
        (self.packs / "__main__.py").write_text(
            FIXTURE_BUILDER.replace("SOURCE", repr(str(self.source))), encoding="utf-8")
        self.write_project()

    def write_project(self):
        (self.source / "CMakeLists.txt").write_text(
            f'''cmake_minimum_required(VERSION 3.25)
project(pack_inputs NONE)
set(NT_ENGINE_ROOT "{ROOT.as_posix()}")
include("${{NT_ENGINE_ROOT}}/cmake/nt_example_packs.cmake")
add_executable(builder IMPORTED)
set_target_properties(builder PROPERTIES IMPORTED_LOCATION "{Path(sys.executable).as_posix()}")
add_custom_target(demo ALL)
nt_example_packs(
    NAME fixture TARGET demo BUILDER builder
    PACK_DIR "{self.packs.as_posix()}"
    ASSETS_DIR "${{CMAKE_CURRENT_BINARY_DIR}}/assets"
    PACKS fixture.ntpack
)
''',
            encoding="utf-8",
        )

    def run_cmake(self, *args):
        result = subprocess.run(
            ["cmake", *map(str, args)], capture_output=True, text=True,
            encoding="utf-8", errors="replace",
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def configure(self, *args):
        self.run_cmake("-S", self.source, "-B", self.build, "-G", "Ninja", *args)

    def build_and_check(self):
        self.run_cmake("--build", self.build)
        # Case-sensitive like the builder; Path.glob ignores case on Windows.
        globbed = [d / n for d in (self.raw, self.later) if d.is_dir()
                   for n in os.listdir(d) if fnmatch.fnmatchcase(n, "*.txt")]
        expected = {
            p.relative_to(self.source).as_posix(): p.read_text(encoding="utf-8")
            for p in [*globbed, self.shared / "shader.txt"]
        }
        for path in (self.packs / "fixture.ntpack", self.build / "assets/fixture.ntpack"):
            self.assertEqual(json.loads(path.read_text(encoding="utf-8")), expected)

    def pack_mtime(self):
        return (self.packs / "fixture.ntpack").stat().st_mtime_ns

    def assert_rebuilds(self, change, rebuilds=True):
        time.sleep(1.1)
        before = self.pack_mtime()
        change()
        self.build_and_check()
        after = self.pack_mtime()
        self.assertEqual(after != before, rebuilds)
        # The next build must settle: no second rebuild from the same change.
        self.build_and_check()
        self.assertEqual(self.pack_mtime(), after)

    def test_native_rebuilds_exactly_when_read_inputs_change(self):
        self.configure()
        self.build_and_check()
        original_mtime = self.pack_mtime()
        self.build_and_check()
        self.configure()
        self.build_and_check()
        self.assertEqual(self.pack_mtime(), original_mtime)

        self.assert_rebuilds(lambda: self.asset.write_text("edited", encoding="utf-8"))
        added = self.raw / "mesh.txt"

        def add_old_file():
            added.write_text("added", encoding="utf-8")
            # A copied asset can predate the pack; membership alone must invalidate it.
            os.utime(added, (1, 1))

        self.assert_rebuilds(add_old_file)
        self.assert_rebuilds(added.unlink)
        self.assert_rebuilds(lambda: (self.shared / "shader.txt").write_text("edited include", encoding="utf-8"))
        # Not read by the builder: a neighbour of a read file, or a file the glob does not match.
        self.assert_rebuilds(lambda: (self.shared / "unused.txt").write_text("unused", encoding="utf-8"), rebuilds=False)
        self.assert_rebuilds(lambda: (self.raw / "notes.md").write_text("notes", encoding="utf-8"), rebuilds=False)
        self.assert_rebuilds(lambda: (self.raw / "EXTRA.TXT").write_text("upper", encoding="utf-8"), rebuilds=False)
        self.assert_rebuilds(self.asset.unlink)
        self.assert_rebuilds(lambda: self.asset.write_text("restored into empty directory", encoding="utf-8"))

        # A globbed directory that did not exist when the builder ran.
        def create_later():
            self.later.mkdir()
            (self.later / "art.txt").write_text("art", encoding="utf-8")

        self.assert_rebuilds(create_later)
        self.assert_rebuilds(lambda: shutil.rmtree(self.later))

    def test_file_added_while_the_builder_runs_rebuilds_next_time(self):
        self.configure()
        self.build_and_check()
        time.sleep(1.1)
        (self.source / "race").write_text("", encoding="utf-8")
        self.asset.write_text("edited", encoding="utf-8")
        self.run_cmake("--build", self.build)
        packed = json.loads((self.packs / "fixture.ntpack").read_text(encoding="utf-8"))
        self.assertNotIn("raw/late.txt", packed)
        # The glob list holds what the builder saw, so the next build catches up.
        self.build_and_check()

    def test_builder_without_depfile_fails_the_build(self):
        main = self.packs / "__main__.py"
        main.write_text(main.read_text(encoding="utf-8").split("\ndepfile = ")[0] + "\n", encoding="utf-8")
        self.configure()
        result = subprocess.run(
            ["cmake", "--build", str(self.build)], capture_output=True, text=True,
            encoding="utf-8", errors="replace",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("nt_builder_write_depfile", result.stdout + result.stderr)

    def test_wasm_and_skipped_native_only_copy_prebuilt_packs(self):
        self.configure()
        self.build_and_check()
        pack = self.packs / "fixture.ntpack"
        original_bytes = pack.read_bytes()
        original_mtime = pack.stat().st_mtime_ns
        time.sleep(1.1)
        self.asset.write_text("not packed yet", encoding="utf-8")
        for name, option in (("wasm", "-DEMSCRIPTEN=ON"),
                             ("skipped", "-DNT_SKIP_EXAMPLE_PACKS=fixture")):
            with self.subTest(mode=name):
                self.build = Path(self.temp.name) / name
                self.configure(option)
                self.run_cmake("--build", self.build)
                self.assertEqual((self.build / "assets/fixture.ntpack").read_bytes(), original_bytes)
                self.assertEqual(pack.stat().st_mtime_ns, original_mtime)


if __name__ == "__main__":
    unittest.main()
