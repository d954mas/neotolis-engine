#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_ROOT"

PASS=0
FAIL=0
SKIP=0

pass() { echo "  [PASS] $1"; PASS=$((PASS + 1)); }
fail() { echo "  [FAIL] $1"; FAIL=$((FAIL + 1)); }
skip() { echo "  [SKIP] $1"; SKIP=$((SKIP + 1)); }

echo "=== Phase 1 Verification ==="
echo ""

# --- Directory structure ---
echo "1. Directory structure"
for dir in engine/runtime/core engine/runtime/web engine/graphics engine/systems \
           tools/builder shared/include examples/hello tests deps/cglm; do
    if [ -d "$dir" ]; then
        pass "$dir/ exists"
    else
        fail "$dir/ missing"
    fi
done

echo ""

# --- Public header naming ---
echo "2. Public header naming (nt_ prefix)"
HEADER_FAIL=0
while IFS= read -r f; do
    basename_f=$(basename "$f")
    if [[ "$basename_f" != nt_* ]]; then
        fail "Header $f does not have nt_ prefix"
        HEADER_FAIL=1
    fi
done < <(find engine -name '*.h' -not -path '*/web/*')
if [ "$HEADER_FAIL" -eq 0 ]; then
    pass "All engine headers use nt_ prefix"
fi

echo ""

# --- Header guards ---
echo "3. Header guards"
GUARD_FAIL=0
while IFS= read -r f; do
    if ! head -3 "$f" | grep -q '#ifndef'; then
        fail "Header $f missing #ifndef guard"
        GUARD_FAIL=1
    fi
done < <(find engine shared -name '*.h')
if [ "$GUARD_FAIL" -eq 0 ]; then
    pass "All headers have #ifndef guards"
fi

echo ""

# --- cglm vendored ---
echo "4. Vendored dependencies"
if [ -f "deps/cglm/CMakeLists.txt" ]; then
    pass "cglm vendored with CMakeLists.txt"
else
    fail "cglm not vendored"
fi

echo ""

# --- CMake files ---
echo "5. CMake build files"
for f in CMakeLists.txt CMakePresets.json engine/CMakeLists.txt shared/CMakeLists.txt \
         tools/builder/CMakeLists.txt examples/hello/CMakeLists.txt tests/CMakeLists.txt; do
    if [ -f "$f" ]; then
        pass "$f exists"
    else
        fail "$f missing"
    fi
done

echo ""

# --- Native build ---
echo "6. Native build"
if command -v cmake > /dev/null 2>&1 && command -v clang > /dev/null 2>&1; then
    echo "  Configuring native-debug..."
    if cmake --preset native-debug > /dev/null 2>&1; then
        pass "native-debug configured"
        echo "  Building native-debug..."
        if cmake --build --preset native-debug > /dev/null 2>&1; then
            pass "native-debug built"

            # Check builder output
            if [ -f "build/tools/builder/native-debug/builder" ] || \
               [ -f "build/tools/builder/native-debug/builder.exe" ]; then
                pass "builder executable at build/tools/builder/native-debug/"
                # Run builder
                BUILDER_OUT=$(./build/tools/builder/native-debug/builder* 2>&1 || true)
                if echo "$BUILDER_OUT" | grep -q "Neotolis Builder"; then
                    pass "builder prints version string"
                else
                    fail "builder did not print expected version string"
                fi
            else
                fail "builder executable not found at build/tools/builder/native-debug/"
            fi

            # Check engine library output
            if [ -f "build/engine/native-debug/libnt_engine.a" ] || \
               [ -f "build/engine/native-debug/nt_engine.lib" ]; then
                pass "engine library at build/engine/native-debug/"
            else
                fail "engine library not found at build/engine/native-debug/"
            fi
        else
            fail "native-debug build failed"
        fi
    else
        fail "native-debug configure failed"
    fi
else
    skip "cmake or clang not available -- cannot verify native build"
fi

echo ""

# --- WASM build ---
echo "7. WASM build"
if command -v emcc > /dev/null 2>&1; then
    echo "  Configuring wasm-debug..."
    if emcmake cmake --preset wasm-debug > /dev/null 2>&1; then
        pass "wasm-debug configured"
        echo "  Building wasm-debug..."
        if cmake --build --preset wasm-debug > /dev/null 2>&1; then
            pass "wasm-debug built"
            if [ -f "build/examples/hello/wasm-debug/index.html" ]; then
                pass "index.html at build/examples/hello/wasm-debug/"
            else
                fail "index.html not found at build/examples/hello/wasm-debug/"
            fi
        else
            fail "wasm-debug build failed"
        fi
    else
        fail "wasm-debug configure failed"
    fi
else
    skip "emcc not available -- cannot verify WASM build"
fi

echo ""

# --- CI workflow ---
echo "8. CI workflow"
if [ -f ".github/workflows/ci.yml" ]; then
    pass "ci.yml exists"
    if grep -q "setup-emsdk" .github/workflows/ci.yml; then
        pass "CI uses setup-emsdk action"
    else
        fail "CI missing setup-emsdk"
    fi
    if grep -q "native-build" .github/workflows/ci.yml; then
        pass "CI has native-build job"
    else
        fail "CI missing native-build job"
    fi
    if grep -q "format-check" .github/workflows/ci.yml; then
        pass "CI has format-check job"
    else
        fail "CI missing format-check job"
    fi
else
    fail "ci.yml missing"
fi

echo ""
echo "=== Results ==="
echo "  Passed: $PASS"
echo "  Failed: $FAIL"
echo "  Skipped: $SKIP"
echo ""

if [ "$FAIL" -gt 0 ]; then
    echo "PHASE 1 VERIFICATION: FAILED"
    exit 1
else
    echo "PHASE 1 VERIFICATION: PASSED"
    exit 0
fi
