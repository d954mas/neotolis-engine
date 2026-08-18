# Wires an example's asset packs into the build graph.
#
# Native builds (unless NAME is listed in NT_SKIP_EXAMPLE_PACKS): BUILDER runs
# at build time producing PACKS in PACK_DIR, then each pack copies into
# ASSETS_DIR; TARGET depends on the chain, so a plain build yields runnable
# assets with no manual builder step and no reconfigure. A pack manifest
# (.expected_packs in PACK_DIR) records the declared set for the CI guard.
#
# WASM builds: the builder can't run here, so packs come from a prior native
# build. Copies are wired unconditionally — a missing pack fails the build
# loudly (fail early), and the same wiring picks the pack up without a
# reconfigure once a native build produces it.
#
# Skipped examples (NT_SKIP_EXAMPLE_PACKS) are the only soft path: packs that
# exist still copy, missing ones just warn — the exe builds without assets.
#
# OPTIONAL_PACKS may legitimately be absent (content-dependent builder output,
# e.g. text_cjk when the local font lacks CJK). The generate step cannot
# declare them as OUTPUT, so they wire as copies only when present, with a
# configure-dependency to pick them up once they appear.
#
# PACK_DIR is shared by every preset; do not build two presets concurrently —
# their builder runs would race on the same pack files.
function(nt_example_packs)
    cmake_parse_arguments(PACKS "" "NAME;TARGET;BUILDER;PACK_DIR;ASSETS_DIR" "PACKS;OPTIONAL_PACKS" ${ARGN})
    if(PACKS_UNPARSED_ARGUMENTS OR NOT PACKS_NAME OR NOT PACKS_TARGET OR NOT PACKS_BUILDER
       OR NOT PACKS_PACK_DIR OR NOT PACKS_ASSETS_DIR OR NOT PACKS_PACKS)
        message(FATAL_ERROR "nt_example_packs(${PACKS_NAME}): bad or missing arguments"
            " (unparsed: '${PACKS_UNPARSED_ARGUMENTS}')")
    endif()

    set(_skipped FALSE)
    if("${PACKS_NAME}" IN_LIST NT_SKIP_EXAMPLE_PACKS)
        set(_skipped TRUE)
        message(STATUS "${PACKS_NAME}: pack generation skipped (NT_SKIP_EXAMPLE_PACKS)")
    endif()

    set(_copy_list "")
    set(_ordering_deps "")
    if(NOT EMSCRIPTEN AND NOT _skipped)
        foreach(PACK ${PACKS_PACKS})
            list(APPEND _pack_files "${PACKS_PACK_DIR}/${PACK}")
        endforeach()
        add_custom_command(
            OUTPUT ${_pack_files}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKS_PACK_DIR}"
            COMMAND "$<TARGET_FILE:${PACKS_BUILDER}>" "${PACKS_PACK_DIR}"
            DEPENDS ${PACKS_BUILDER}
            WORKING_DIRECTORY "${NT_ENGINE_ROOT}"
            COMMENT "Building ${PACKS_NAME} pack(s)"
        )
        set(_copy_list ${PACKS_PACKS})
        # The builder rewrites optional packs without declaring them as OUTPUT;
        # ordering optional copies after the required outputs prevents copying
        # a pack mid-rewrite.
        set(_ordering_deps ${_pack_files})
        # Manifest for the CI completeness guard: the declared set, one per line.
        string(REPLACE ";" "\n" _manifest_body "${PACKS_PACKS}")
        file(WRITE "${PACKS_PACK_DIR}/.expected_packs" "${_manifest_body}\n")
    elseif(_skipped)
        foreach(PACK ${PACKS_PACKS})
            if(EXISTS "${PACKS_PACK_DIR}/${PACK}")
                list(APPEND _copy_list ${PACK})
            else()
                list(APPEND _missing ${PACK})
            endif()
        endforeach()
        if(_missing)
            message(WARNING "${PACKS_NAME}: skipped pack(s) '${_missing}' not present at"
                " ${PACKS_PACK_DIR}; ${PACKS_TARGET} builds without them.")
        endif()
        file(REMOVE "${PACKS_PACK_DIR}/.expected_packs")
    else()
        # WASM: wire every copy. A missing pack fails the build with a clear
        # ninja error until a native build creates it — then the same edge
        # copies it, no reconfigure needed.
        set(_copy_list ${PACKS_PACKS})
        foreach(PACK ${PACKS_PACKS})
            if(NOT EXISTS "${PACKS_PACK_DIR}/${PACK}")
                list(APPEND _missing ${PACK})
            endif()
        endforeach()
        if(_missing)
            message(WARNING "${PACKS_NAME}: pack(s) '${_missing}' not found at"
                " ${PACKS_PACK_DIR}. Build a native preset first; building"
                " ${PACKS_TARGET} before that fails.")
        endif()
    endif()

    foreach(PACK ${PACKS_OPTIONAL_PACKS})
        if(EXISTS "${PACKS_PACK_DIR}/${PACK}")
            list(APPEND _copy_list ${PACK})
        else()
            set_property(DIRECTORY APPEND PROPERTY
                CMAKE_CONFIGURE_DEPENDS "${PACKS_PACK_DIR}/${PACK}")
        endif()
    endforeach()

    foreach(PACK ${_copy_list})
        add_custom_command(
            OUTPUT "${PACKS_ASSETS_DIR}/${PACK}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKS_ASSETS_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PACKS_PACK_DIR}/${PACK}" "${PACKS_ASSETS_DIR}/${PACK}"
            DEPENDS "${PACKS_PACK_DIR}/${PACK}" ${_ordering_deps}
            COMMENT "Copying ${PACK} -> assets/"
        )
        list(APPEND _outputs "${PACKS_ASSETS_DIR}/${PACK}")
    endforeach()
    if(_outputs)
        add_custom_target(${PACKS_TARGET}_packs DEPENDS ${_outputs})
        add_dependencies(${PACKS_TARGET} ${PACKS_TARGET}_packs)
    endif()
endfunction()
