# Warning and sanitizer flag functions for Neotolis Engine targets.
# Apply to engine/tools/examples/tests targets only -- never to vendored deps.

function(nt_set_warning_flags target)
    target_compile_options(${target} PRIVATE
        -Wall
        -Wextra
        -Werror
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wdouble-promotion
        -Wformat=2
        -Wundef
        # Documented suppressions:
        -Wno-unused-parameter    # Callback stubs often have unused params
    )
endfunction()

function(nt_set_sanitizer_flags target)
    # Sanitizer flags for Debug builds only.
    # MUST appear in both compile and link options (Pitfall 1 from research).
    target_compile_options(${target} PRIVATE
        $<$<CONFIG:Debug>:-fsanitize=address,undefined>
        $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
        $<$<CONFIG:Debug>:-fno-sanitize-recover=all>
    )
    target_link_options(${target} PRIVATE
        $<$<CONFIG:Debug>:-fsanitize=address,undefined>
    )
    if(EMSCRIPTEN)
        # All WASM builds need growable memory (static data can exceed 16 MB default)
        target_link_options(${target} PRIVATE
            "-sALLOW_MEMORY_GROWTH=1"
            $<$<CONFIG:Release>:-sINITIAL_MEMORY=64MB>
            $<$<CONFIG:Debug>:-sINITIAL_MEMORY=512MB>
        )
    endif()
endfunction()
