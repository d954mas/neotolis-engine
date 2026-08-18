# Wires an example's asset packs into the build graph.
#
# Native builds (unless NAME is listed in NT_SKIP_EXAMPLE_PACKS): BUILDER runs
# at build time producing PACKS in PACK_DIR, then each pack copies into
# ASSETS_DIR; TARGET depends on the chain, so a plain build yields runnable
# assets with no manual builder step and no reconfigure.
#
# WASM builds and skipped examples: the builder can't (or shouldn't) run here,
# so packs a prior native build left in PACK_DIR copy as-is; missing packs
# warn and TARGET still builds.
#
# OPTIONAL_PACKS may legitimately be absent (content-dependent builder output,
# e.g. text_cjk when the local font lacks CJK). The generate step cannot
# declare them as OUTPUT, so they join the copy chain only when present at
# configure time.
function(nt_example_packs)
    cmake_parse_arguments(PACKS "" "NAME;TARGET;BUILDER;PACK_DIR;ASSETS_DIR" "PACKS;OPTIONAL_PACKS" ${ARGN})

    set(_generate FALSE)
    if(NOT EMSCRIPTEN AND NOT "${PACKS_NAME}" IN_LIST NT_SKIP_EXAMPLE_PACKS)
        set(_generate TRUE)
    endif()

    set(_copy_list "")
    if(_generate)
        foreach(PACK ${PACKS_PACKS})
            list(APPEND _pack_files "${PACKS_PACK_DIR}/${PACK}")
        endforeach()
        add_custom_command(
            OUTPUT ${_pack_files}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKS_PACK_DIR}"
            COMMAND "$<TARGET_FILE:${PACKS_BUILDER}>" "${PACKS_PACK_DIR}"
            DEPENDS ${PACKS_BUILDER}
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            COMMENT "Building ${PACKS_NAME} pack(s)"
        )
        set(_copy_list ${PACKS_PACKS})
    else()
        set(_missing "")
        foreach(PACK ${PACKS_PACKS})
            if(EXISTS "${PACKS_PACK_DIR}/${PACK}")
                list(APPEND _copy_list ${PACK})
            else()
                list(APPEND _missing ${PACK})
            endif()
        endforeach()
        if(_missing)
            message(WARNING "${PACKS_NAME}: pack(s) '${_missing}' not found at ${PACKS_PACK_DIR}. "
                "Build a native preset first (or drop ${PACKS_NAME} from NT_SKIP_EXAMPLE_PACKS).")
        endif()
    endif()
    foreach(PACK ${PACKS_OPTIONAL_PACKS})
        if(EXISTS "${PACKS_PACK_DIR}/${PACK}")
            list(APPEND _copy_list ${PACK})
        endif()
    endforeach()

    foreach(PACK ${_copy_list})
        add_custom_command(
            OUTPUT "${PACKS_ASSETS_DIR}/${PACK}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKS_ASSETS_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PACKS_PACK_DIR}/${PACK}" "${PACKS_ASSETS_DIR}/${PACK}"
            DEPENDS "${PACKS_PACK_DIR}/${PACK}"
            COMMENT "Copying ${PACK} -> ${NT_PRESET_NAME}/assets/"
        )
        list(APPEND _outputs "${PACKS_ASSETS_DIR}/${PACK}")
    endforeach()
    if(_outputs)
        add_custom_target(${PACKS_TARGET}_packs DEPENDS ${_outputs})
        add_dependencies(${PACKS_TARGET} ${PACKS_TARGET}_packs)
    endif()
endfunction()
