import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[2]


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
        self.shared = self.source / "shared shaders"
        self.shared.mkdir()
        self.asset = self.raw / "font.txt"
        self.asset.write_text("original", encoding="utf-8")
        (self.shared / "shader.txt").write_text("shader", encoding="utf-8")
        # Python accepts a directory containing __main__.py as its program.
        # This fixture exercises the real CMake build/copy graph without codecs.
        (self.packs / "__main__.py").write_text(
            "import json\nfrom pathlib import Path\n"
            f"source = Path({str(self.source)!r})\n"
            "files = {p.relative_to(source).as_posix(): p.read_text()\n"
            "         for name in ('raw', 'shared shaders')\n"
            "         for p in (source / name).rglob('*') if p.is_file()}\n"
            "Path(__file__).with_name('fixture.ntpack').write_text(json.dumps(files, sort_keys=True))\n",
            encoding="utf-8",
        )
        self.write_project()

    def write_project(self, input_dirs=True):
        dirs = 'INPUT_DIRS raw "${CMAKE_CURRENT_SOURCE_DIR}/shared shaders"' if input_dirs else ""
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
    {dirs}
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
        expected = {
            p.relative_to(self.source).as_posix(): p.read_text(encoding="utf-8")
            for directory in (self.raw, self.shared)
            for p in directory.rglob("*") if p.is_file()
        }
        for path in (self.packs / "fixture.ntpack", self.build / "assets/fixture.ntpack"):
            self.assertEqual(json.loads(path.read_text(encoding="utf-8")), expected)

    def test_native_tracks_edits_additions_removals_and_new_subdirectories(self):
        self.configure()
        self.build_and_check()
        pack = self.packs / "fixture.ntpack"
        original_mtime = pack.stat().st_mtime_ns
        self.build_and_check()
        self.configure()
        self.build_and_check()
        self.assertEqual(pack.stat().st_mtime_ns, original_mtime)

        time.sleep(1.1)
        self.asset.write_text("edited", encoding="utf-8")
        self.build_and_check()

        time.sleep(1.1)
        nested = self.raw / "new subdirectory" / "mesh.txt"
        nested.parent.mkdir()
        nested.write_text("added", encoding="utf-8")
        # A copied asset can predate the pack; membership alone must invalidate it.
        os.utime(nested, (1, 1))
        self.build_and_check()

        time.sleep(1.1)
        nested.unlink()
        self.build_and_check()

        time.sleep(1.1)
        (self.shared / "shader.txt").write_text("edited include", encoding="utf-8")
        self.build_and_check()

        time.sleep(1.1)
        self.asset.unlink()
        self.build_and_check()

        time.sleep(1.1)
        self.asset.write_text("restored into empty directory", encoding="utf-8")
        self.build_and_check()

    def test_procedural_pack_needs_no_input_directories(self):
        self.write_project(input_dirs=False)
        self.configure()
        self.build_and_check()

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
