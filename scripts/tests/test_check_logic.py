import unittest

from scripts.check_logic import (
    allowed_missing_tests,
    ctest_command_stems,
    missing_registered_targets,
    tidy_dependents,
)


class CheckLogicTests(unittest.TestCase):
    def test_sanitizer_waiver_only_applies_to_windows_clang(self):
        sanitizer = "tests/unit/test_sanitizer_proof.c"

        self.assertEqual({sanitizer}, allowed_missing_tests("Windows", "Clang"))
        self.assertEqual(set(), allowed_missing_tests("Linux", "Clang"))
        self.assertEqual(set(), allowed_missing_tests("Windows", "MSVC"))

    def test_ctest_parser_uses_command_tokens_not_the_name(self):
        # The [=[name]=] token must not register a target; the command token always
        # counts; later args count only for python-wrapper commands AND only when
        # they live under build/tests, so an exe passed as data (out of tree, or to
        # a non-wrapper test) cannot satisfy the gate.
        lines = ['add_test([=[test_name_only]=] "python" "C:/r/build/tests/dbg/test_foo.exe" "data/test_bar.exe")\n']

        self.assertEqual({"python", "test_foo"}, ctest_command_stems(lines))

    def test_ctest_parser_ignores_args_of_non_wrapper_commands(self):
        lines = ['add_test([=[n]=] "C:/r/build/tests/dbg/test_cmp.exe" "C:/r/build/tests/dbg/test_ref.exe")\n']

        self.assertEqual({"test_cmp"}, ctest_command_stems(lines))

    def test_every_compiled_target_must_be_registered(self):
        targets = {"test_log_real", "test_log_stub"}

        self.assertEqual({"test_log_stub"}, missing_registered_targets(targets, {"test_log_real"}))

    def test_tidy_dependents_keeps_includers_and_uncompiled_tus(self):
        deps = ["a.obj: #deps 2, deps mtime 1 (VALID)", "    C:/r/a.c", "    C:/r/x.h", "",
                "b.obj: #deps 2, deps mtime 1 (VALID)", "    C:/r/b.c", "    C:/r/y.h"]
        db = {"C:/r/a.c", "C:/r/b.c", "C:/r/devapi_only.c"}

        self.assertEqual({"C:/r/a.c", "C:/r/devapi_only.c"}, tidy_dependents(deps, db, ["C:\\R\\x.h"]))

    def test_tidy_dependents_falls_back_to_full_for_unknown_header(self):
        deps = ["a.obj: #deps 2, deps mtime 1 (VALID)", "    C:/r/a.c", "    C:/r/x.h"]

        self.assertIsNone(tidy_dependents(deps, {"C:/r/a.c"}, ["C:/r/x.h", "C:/r/new.h"]))

if __name__ == "__main__":
    unittest.main()
