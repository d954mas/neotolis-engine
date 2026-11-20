# Configure fails unless the tree is built by the toolchain pinned in .toolchain-versions. A tree
# configured before a version change, or by a cmake/ninja/compiler from PATH, stops here; the
# versions file is a configure dependency, so a bump reruns this on the next build.
include("${CMAKE_CURRENT_LIST_DIR}/nt_toolchain_pins.cmake")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../.toolchain-versions")

function(_nt_require what found pinned)
    if(NOT found STREQUAL pinned)
        message(FATAL_ERROR "${what} is '${found}', .toolchain-versions pins '${pinned}'. Install it with "
                            "python scripts/setup_toolchain.py, run 'source scripts/lib/toolchain.sh' so the "
                            "pinned cmake is first on PATH, then reconfigure the tree with --fresh")
    endif()
endfunction()

_nt_require("CMake" "${CMAKE_VERSION}" "${NT_PIN_CMAKE_VERSION}")
execute_process(COMMAND "${CMAKE_MAKE_PROGRAM}" --version OUTPUT_VARIABLE _nt_ninja OUTPUT_STRIP_TRAILING_WHITESPACE)
_nt_require("${CMAKE_GENERATOR} (${CMAKE_MAKE_PROGRAM})" "${_nt_ninja}" "${NT_PIN_NINJA_VERSION}")
if(EMSCRIPTEN)
    _nt_require("Emscripten" "${EMSCRIPTEN_VERSION}" "${NT_PIN_EMSDK_VERSION}")
else()
    # C and C++ come from the same toolchain file, so the C compiler stands for both.
    _nt_require("C compiler" "${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}" "Clang ${NT_PIN_LLVM_VERSION}")
endif()
