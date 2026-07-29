import json
import tempfile
import unittest
from pathlib import Path

from scripts.check_logic import (
    allowed_missing_tests,
    ci_variant_files,
    ctest_command_stems,
    missing_registered_targets,
    read_compile_variants,
)


class CheckLogicTests(unittest.TestCase):
    def test_sanitizer_waiver_only_applies_to_windows_clang(self):
        sanitizer = "tests/unit/test_sanitizer_proof.c"

        self.assertEqual({sanitizer}, allowed_missing_tests("Windows", "Clang"))
        self.assertEqual(set(), allowed_missing_tests("Linux", "Clang"))
        self.assertEqual(set(), allowed_missing_tests("Windows", "MSVC"))

    def test_ctest_parser_uses_only_the_command_token(self):
        lines = ['add_test([=[test_foo]=] "python" "test_foo")\n']

        self.assertEqual({"python"}, ctest_command_stems(lines))

    def test_every_compiled_target_must_be_registered(self):
        targets = {"test_log_real", "test_log_stub"}

        self.assertEqual({"test_log_stub"}, missing_registered_targets(targets, {"test_log_real"}))

    def test_compile_database_preserves_all_variants(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "compile_commands.json"
            path.write_text(
                json.dumps(
                    [
                        {"file": "engine/core.c", "command": "clang -DA engine/core.c"},
                        {"file": "engine/core.c", "command": "clang -DB engine/core.c"},
                    ]
                ),
                encoding="utf-8",
            )

            self.assertEqual(
                {"clang -DA engine/core.c", "clang -DB engine/core.c"},
                read_compile_variants(path)["engine/core.c"],
            )

    def test_ci_only_variant_is_not_hidden_by_equal_last_command(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            ci = root / "ci.json"
            native = root / "native.json"
            ci.write_text(
                json.dumps(
                    [
                        {"file": "engine/core.c", "command": "clang -DDEVAPI engine/core.c"},
                        {"file": "engine/core.c", "command": "clang engine/core.c"},
                    ]
                ),
                encoding="utf-8",
            )
            native.write_text(
                json.dumps([{"file": "engine/core.c", "command": "clang engine/core.c"}]),
                encoding="utf-8",
            )

            self.assertEqual({"engine/core.c"}, ci_variant_files(ci, native))


if __name__ == "__main__":
    unittest.main()
