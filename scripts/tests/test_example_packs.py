from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[2]


# Stands in for a pack builder that leaves a current pack untouched.
FIXTURE_BUILDER = r"""from pathlib import Path

asset = (Path(SOURCE) / "asset.txt").read_text()
pack = Path(__file__).parent / "fixture.ntpack"
if not pack.exists() or pack.read_text() != asset:
    pack.write_text(asset)
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
        self.asset = self.source / "asset.txt"
        self.asset.write_text("original", encoding="utf-8")
        # Python accepts a directory containing __main__.py as its program, so
        # the real CMake build/copy graph runs without codecs.
        (self.packs / "__main__.py").write_text(
            FIXTURE_BUILDER.replace("SOURCE", repr(str(self.source))), encoding="utf-8")
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
        return result.stdout

    def configure(self, *args):
        self.run_cmake("-S", self.source, "-B", self.build, "-G", "Ninja", *args)

    def mtimes(self):
        return [p.stat().st_mtime_ns for p in (self.packs / "fixture.ntpack", self.build / "assets/fixture.ntpack")]

    def test_builder_runs_every_build_and_copies_only_changed_packs(self):
        self.configure()
        self.run_cmake("--build", self.build)
        self.assertEqual((self.build / "assets/fixture.ntpack").read_text(encoding="utf-8"), "original")
        before = self.mtimes()
        time.sleep(1.1)
        # The builder decides: an untouched pack is neither rewritten nor copied
        # again (restat skips the copy edge, not just copy_if_different).
        self.assertNotIn("Copying", self.run_cmake("--build", self.build))
        self.assertEqual(self.mtimes(), before)

        self.asset.write_text("edited", encoding="utf-8")
        self.assertIn("Copying", self.run_cmake("--build", self.build))
        self.assertEqual((self.build / "assets/fixture.ntpack").read_text(encoding="utf-8"), "edited")
        self.assertTrue(all(a > b for a, b in zip(self.mtimes(), before)))

    def test_wasm_and_skipped_native_only_copy_prebuilt_packs(self):
        self.configure()
        self.run_cmake("--build", self.build)
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
