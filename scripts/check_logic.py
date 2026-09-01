import os
import re
from pathlib import Path


_CMAKE_TOKEN_RE = re.compile(r'"([^"]*)"|\[(=*)\[(.*?)\]\2\]')
_SANITIZER_PROOF = "tests/unit/test_sanitizer_proof.c"


def _cmake_tokens(line):
    return [
        match.group(1) if match.group(1) is not None else match.group(3)
        for match in _CMAKE_TOKEN_RE.finditer(line)
    ]


def ctest_command_stems(lines):
    stems = set()
    for line in lines:
        if "add_test(" not in line:
            continue
        tokens = _cmake_tokens(line)
        if len(tokens) < 2:
            continue
        # tokens[0] is the test NAME — never registration evidence. The command
        # (tokens[1]) always counts. Later args count only for python-wrapper
        # commands AND only when they point into the test output dir (wrapper-
        # launched exes: python run_x.py <exe>) — an exe passed as data to a
        # non-wrapper test can never satisfy the gate.
        stems.add(os.path.splitext(os.path.basename(tokens[1]))[0])
        command_is_wrapper = os.path.basename(tokens[1]).startswith("python")
        for token in tokens[2:] if command_is_wrapper else []:
            if "/build/tests/" in token.replace("\\", "/"):
                stems.add(os.path.splitext(os.path.basename(token))[0])
    return stems


def missing_registered_targets(targets, registered):
    return set(targets) - set(registered)


def allowed_missing_tests(system_name, compiler_id):
    if system_name == "Windows" and compiler_id == "Clang":
        return {_SANITIZER_PROOF}
    return set()


def cmake_generated_value(build_dir, filename, variable):
    pattern = re.compile(rf'set\({re.escape(variable)}\s+"([^"]+)"\)')
    for path in Path(build_dir).glob(f"CMakeFiles/**/{filename}"):
        match = pattern.search(path.read_text(encoding="utf-8", errors="replace"))
        if match:
            return match.group(1)
    return ""
