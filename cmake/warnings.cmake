# Warning, sanitizer, and CRT flag functions for Neotolis Engine targets.
# Warnings/sanitizers apply to engine/tools/examples/tests targets only -- never to vendored deps.

function(nt_set_warning_flags target)
    target_compile_options(${target} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wdouble-promotion
        -Wformat=2
        -Wundef
        # Documented suppressions:
        -Wno-unused-parameter    # Callback stubs often have unused params
    )
    if(NOT NT_NO_WERROR)
        target_compile_options(${target} PRIVATE -Werror)
    endif()
endfunction()

function(nt_set_sanitizer_flags target)
    # Sanitizer flags for Debug builds only.
    # MUST appear in both compile and link options (Pitfall 1 from research).
    # Sanitizers disabled on Windows + clang: interception_win can't patch
    # current UCRT short-prologue functions (memcpy / operator delete / etc.);
    # both static and dynamic ASan runtimes trip at startup, and UBSan on
    # Windows links against ASan-side runtime helpers that fail without ASan.
    if(WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "Clang")
        target_compile_options(${target} PRIVATE
            $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
        )
    else()
        target_compile_options(${target} PRIVATE
            $<$<CONFIG:Debug>:-fsanitize=address,undefined>
            $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
            $<$<CONFIG:Debug>:-fno-sanitize-recover=all>
        )
        target_link_options(${target} PRIVATE
            $<$<CONFIG:Debug>:-fsanitize=address,undefined>
        )
    endif()
    if(EMSCRIPTEN)
        # All WASM builds need growable memory (static data can exceed 16 MB default)
        target_link_options(${target} PRIVATE
            "-sALLOW_MEMORY_GROWTH=1"
            $<$<CONFIG:Release>:-sINITIAL_MEMORY=64MB>
            $<$<CONFIG:Debug>:-sINITIAL_MEMORY=512MB>
        )
    endif()
endfunction()

# NT_STATIC_CRT=ON pins the release static CRT (Windows); OFF skips every pin so an
# embedding exe controls the CRT via CMAKE_MSVC_RUNTIME_LIBRARY. All-or-nothing on
# purpose: a partial pin re-creates the /failifmismatch conflicts inside one exe.
function(nt_set_static_crt target)
    if(NT_STATIC_CRT)
        # Remove _DLL so CRT calls resolve to static symbols
        target_compile_options(${target} PRIVATE -U_DLL)
    endif()
endfunction()

# C++ variant: MSVC C++ objects embed /failifmismatch(RuntimeLibrary, _ITERATOR_DEBUG_LEVEL),
# so the runtime property and iterator debug level must be pinned alongside -U_DLL.
function(nt_set_static_crt_cxx target)
    if(NT_STATIC_CRT)
        nt_set_static_crt(${target})
        target_compile_definitions(${target} PRIVATE $<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=0>)
        set_target_properties(${target} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:>")
    endif()
endfunction()
