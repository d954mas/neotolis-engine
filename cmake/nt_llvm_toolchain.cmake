# Native engine builds compile C and C++ with the LLVM release pinned in .toolchain-versions,
# installed by scripts/setup_toolchain.py into the per-user toolchain directory that every
# checkout and worktree shares. Never the system or image compiler or linker.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../.toolchain-versions" _nt_llvm_line REGEX "^LLVM_VERSION=")
string(REPLACE "LLVM_VERSION=" "" NT_LLVM_VERSION "${_nt_llvm_line}")
if(CMAKE_HOST_WIN32)
    file(TO_CMAKE_PATH "$ENV{USERPROFILE}" _nt_home)
    set(_nt_exe ".exe")
else()
    set(_nt_home "$ENV{HOME}")
    set(_nt_exe "")
endif()
set(_nt_llvm_bin "${_nt_home}/.neotolis/toolchain/llvm-${NT_LLVM_VERSION}/bin")
if(NOT EXISTS "${_nt_llvm_bin}/clang${_nt_exe}")
    message(FATAL_ERROR "LLVM ${NT_LLVM_VERSION} is not installed in ${_nt_llvm_bin}: run python scripts/setup_toolchain.py")
endif()
set(CMAKE_C_COMPILER "${_nt_llvm_bin}/clang${_nt_exe}")
set(CMAKE_CXX_COMPILER "${_nt_llvm_bin}/clang++${_nt_exe}")
if(CMAKE_HOST_WIN32)
    # CMake finds the resource compiler on PATH, not next to the compiler.
    set(CMAKE_RC_COMPILER "${_nt_llvm_bin}/llvm-rc.exe")
else()
    # The pinned ld.lld instead of the image's GNU ld (Windows already links with lld-link).
    set(CMAKE_LINKER_TYPE LLD)
endif()
