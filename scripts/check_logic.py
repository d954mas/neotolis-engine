import json
import os
import re
from collections import defaultdict
from pathlib import Path


_CMAKE_TOKEN_RE = re.compile(r'"([^"]*)"|\[(=*)\[(.*?)\]\2\]')
_SANITIZER_PROOF = "tests/unit/test_sanitizer_proof.c"


def _compile_command(entry):
    if "command" in entry:
        return entry["command"]
    return "\0".join(entry.get("arguments", []))


def read_compile_variants(path):
    variants = defaultdict(set)
    for entry in json.loads(Path(path).read_text(encoding="utf-8")):
        source = entry["file"].replace("\\", "/")
        variants[source].add(_compile_command(entry))
    return dict(variants)


def ci_variant_files(ci_path, native_path):
    ci = read_compile_variants(ci_path)
    native = read_compile_variants(native_path)
    return {source for source, commands in ci.items() if not commands <= native.get(source, set())}


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
        if len(tokens) >= 2:
            stems.add(os.path.splitext(os.path.basename(tokens[1]))[0])
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
