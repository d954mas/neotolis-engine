# Native engine builds compile, link and archive with the LLVM release pinned in
# .toolchain-versions, never the system or image tools.
include("${CMAKE_CURRENT_LIST_DIR}/nt_toolchain_pins.cmake")
set(_nt_llvm_bin "${NT_TOOLCHAIN_DIR}/llvm-${NT_PIN_LLVM_VERSION}/bin")
if(CMAKE_HOST_WIN32)
    set(CMAKE_C_COMPILER "${_nt_llvm_bin}/clang.exe")
    set(CMAKE_CXX_COMPILER "${_nt_llvm_bin}/clang++.exe")
    # CMake finds the resource compiler on PATH, not next to the compiler.
    set(CMAKE_RC_COMPILER "${_nt_llvm_bin}/llvm-rc.exe")
else()
    set(CMAKE_C_COMPILER "${_nt_llvm_bin}/clang")
    set(CMAKE_CXX_COMPILER "${_nt_llvm_bin}/clang++")
    # The pinned ld.lld instead of the image's GNU ld; --warn-backrefs keeps rejecting the archive
    # orders GNU ld cannot resolve, so the documented consumer link order stays checked.
    set(CMAKE_LINKER_TYPE LLD)
    set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--warn-backrefs -Wl,--fatal-warnings")
endif()
