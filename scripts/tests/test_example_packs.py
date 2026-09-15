from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class ExamplePackTests(unittest.TestCase):
    def test_codec_record_describes_the_successful_producer(self):
        build_root = ROOT / "build" / "tests"
        build_root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="pack-provenance-", dir=build_root) as tmp:
            work = Path(tmp).resolve()
            self.assertTrue(work.is_relative_to(build_root.resolve()))
            source = work / "source"
            source.mkdir()
            (source / "builder.c").write_text(
                '#include <stdio.h>\n'
                'int main(int argc, char **argv) {\n'
                '    if (argc != 2 || FAIL_PRODUCER) return 1;\n'
                '    char path[4096];\n'
                '    snprintf(path, sizeof(path), "%s/fixture.ntpack", argv[1]);\n'
                '    FILE *file = fopen(path, "wb");\n'
                '    if (!file) return 2;\n'
                '    fputs("fixture", file);\n'
                '    return fclose(file);\n'
                '}\n', encoding="utf-8")
            (source / "CMakeLists.txt").write_text(
                'cmake_minimum_required(VERSION 3.25)\n'
                'project(pack_provenance C)\n'
                'set(FAIL_PRODUCER 0 CACHE STRING "Fail pack generation")\n'
                'add_executable(producer builder.c)\n'
                'target_compile_definitions(producer PRIVATE FAIL_PRODUCER=${FAIL_PRODUCER})\n'
                'add_custom_target(consumer)\n'
                'include("${NT_ENGINE_ROOT}/cmake/nt_example_packs.cmake")\n'
                'nt_example_packs(NAME fixture TARGET consumer BUILDER producer\n'
                '    PACK_DIR "${PACK_ROOT}" ASSETS_DIR "${CMAKE_BINARY_DIR}/assets"\n'
                '    PACKS fixture.ntpack)\n', encoding="utf-8")
            packs = work / "packs"
            record = packs / ".basisu_codecs"
            stamp = packs / ".fixture.stamp"

            def run(*args, succeeds=True):
                result = subprocess.run(
                    ["cmake", *map(str, args)], capture_output=True, text=True,
                    cwd=work, timeout=60)
                if succeeds:
                    self.assertEqual(0, result.returncode, result.stdout + result.stderr)
                else:
                    self.assertNotEqual(0, result.returncode, result.stdout + result.stderr)
                return result.stdout + result.stderr

            def configure(name, codecs, *options, succeeds=True):
                return run(
                    "-S", source, "-B", work / name, "-G", "Ninja",
                    "-DCMAKE_C_COMPILER=clang", f"-DNT_ENGINE_ROOT={ROOT.as_posix()}",
                    f"-DPACK_ROOT={packs.as_posix()}", f"-DNT_BASISU_CODECS={codecs}",
                    *options, succeeds=succeeds)

            def build(name, succeeds=True):
                return run("--build", work / name, "--target", "consumer_packs",
                           succeeds=succeeds)

            original = "ETC1S;UASTC_LDR"
            configure("native-a", original)
            build("native-a")
            self.assertEqual(original, record.read_text().strip())
            stamp_before = stamp.stat().st_mtime_ns

            configure("native-b", "ETC1S", "-DFAIL_PRODUCER=1")
            self.assertEqual(original, record.read_text().strip())
            configure("wasm-a", original, "-DEMSCRIPTEN=ON")
            build("wasm-a")
            self.assertEqual(b"fixture", (work / "wasm-a/assets/fixture.ntpack").read_bytes())

            build("native-b", succeeds=False)
            self.assertEqual(original, record.read_text().strip())
            self.assertEqual(stamp_before, stamp.stat().st_mtime_ns)

            configure("native-b", "ETC1S", "-DFAIL_PRODUCER=0")
            build("native-b")
            self.assertEqual("ETC1S", record.read_text().strip())
            mismatch = configure("wasm-a", original, "-DEMSCRIPTEN=ON", succeeds=False)
            self.assertIn("this configure decodes", mismatch)

            record.unlink()
            build("native-b")
            self.assertEqual("ETC1S", record.read_text().strip())
            stamp_after = stamp.stat().st_mtime_ns
            configure("native-b", "ETC1S")
            build("native-b")
            self.assertEqual(stamp_after, stamp.stat().st_mtime_ns)


if __name__ == "__main__":
    unittest.main()
