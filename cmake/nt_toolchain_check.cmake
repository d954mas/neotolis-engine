# Configure fails unless the tree is built by the toolchain pinned in .toolchain-versions. A tree
# configured before a version change, or by a cmake/ninja/compiler from PATH, stops here; the
# versions file is a configure dependency, so a bump reruns this on the next build.
set(_nt_versions "${CMAKE_CURRENT_LIST_DIR}/../.toolchain-versions")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_nt_versions}")
file(STRINGS "${_nt_versions}" _nt_pins REGEX "^[A-Z0-9_]+=")
foreach(_nt_pin IN LISTS _nt_pins)
    string(REGEX MATCH "^([A-Z0-9_]+)=(.*)$" _nt_match "${_nt_pin}")
    set(_NT_PIN_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
endforeach()

function(_nt_require what found pinned)
    if(NOT found STREQUAL pinned)
        message(FATAL_ERROR "${what} is '${found}', .toolchain-versions pins '${pinned}'. Install it with "
                            "python scripts/setup_toolchain.py, run 'source scripts/lib/toolchain.sh' so the "
                            "pinned cmake is first on PATH, then reconfigure: cmake --preset <name> --fresh")
    endif()
endfunction()

_nt_require("CMake" "${CMAKE_VERSION}" "${_NT_PIN_CMAKE_VERSION}")
execute_process(COMMAND "${CMAKE_MAKE_PROGRAM}" --version OUTPUT_VARIABLE _nt_ninja OUTPUT_STRIP_TRAILING_WHITESPACE)
_nt_require("Ninja (${CMAKE_MAKE_PROGRAM})" "${_nt_ninja}" "${_NT_PIN_NINJA_VERSION}")
if(EMSCRIPTEN)
    _nt_require("Emscripten" "${EMSCRIPTEN_VERSION}" "${_NT_PIN_EMSDK_VERSION}")
else()
    # C and C++ come from the same toolchain file, so the C compiler stands for both.
    _nt_require("C compiler" "${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}" "Clang ${_NT_PIN_LLVM_VERSION}")
endif()
