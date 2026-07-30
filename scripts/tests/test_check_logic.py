import unittest

from scripts.check_logic import (
    allowed_missing_tests,
    ctest_command_stems,
    missing_registered_targets,
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

if __name__ == "__main__":
    unittest.main()
