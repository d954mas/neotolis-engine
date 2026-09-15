# Wires an example's asset-pack build + copy into the build graph and writes
# the .expected_packs manifest the CI guard verifies. Native presets run the
# builder at build time; wasm copies a prior native build's packs or fails loudly.
function(nt_example_packs)
    cmake_parse_arguments(PACKS "" "NAME;TARGET;BUILDER;PACK_DIR;ASSETS_DIR" "PACKS;OPTIONAL_PACKS" ${ARGN})
    if(PACKS_UNPARSED_ARGUMENTS OR NOT PACKS_NAME OR NOT PACKS_TARGET OR NOT PACKS_BUILDER
       OR NOT PACKS_PACK_DIR OR NOT PACKS_ASSETS_DIR OR NOT PACKS_PACKS)
        message(FATAL_ERROR "nt_example_packs(${PACKS_NAME}): bad or missing arguments"
            " (unparsed: '${PACKS_UNPARSED_ARGUMENTS}')")
    endif()
    # Registry lets the root CMakeLists reject unknown NT_SKIP_EXAMPLE_PACKS entries.
    set_property(GLOBAL APPEND PROPERTY NT_EXAMPLE_PACK_NAMES "${PACKS_NAME}")

    set(_skipped FALSE)
    if("${PACKS_NAME}" IN_LIST NT_SKIP_EXAMPLE_PACKS)
        set(_skipped TRUE)
        message(STATUS "${PACKS_NAME}: pack generation skipped (NT_SKIP_EXAMPLE_PACKS)")
    endif()

    set(_copy_list "")
    set(_stamp "")
    if(NOT EMSCRIPTEN AND NOT _skipped)
        foreach(PACK ${PACKS_PACKS})
            list(APPEND _pack_files "${PACKS_PACK_DIR}/${PACK}")
        endforeach()
        # The stamp is the only OUTPUT: it appears strictly after the builder
        # succeeded, so a builder that published packs and then died (e.g. at
        # header generation) cannot look up-to-date to ninja's mtime check.
        # The packs target must DEPENDS on it — CMake materializes a custom
        # command only when some target consumes an OUTPUT (byproducts don't count).
        set(_stamp "${PACKS_PACK_DIR}/.${PACKS_NAME}.stamp")
        add_custom_command(
            OUTPUT "${_stamp}"
            BYPRODUCTS ${_pack_files}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKS_PACK_DIR}"
            COMMAND "$<TARGET_FILE:${PACKS_BUILDER}>" "${PACKS_PACK_DIR}"
            COMMAND ${CMAKE_COMMAND} -E touch "${_stamp}"
            DEPENDS ${PACKS_BUILDER}
            WORKING_DIRECTORY "${NT_ENGINE_ROOT}"
            COMMENT "Building ${PACKS_NAME} pack(s)"
        )
        set(_copy_list ${PACKS_PACKS})
        # Manifest existence tells the CI guard this example is wired; the pack
        # list is informational (missing packs already fail the build itself).
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
        # Marker (not absence) so the CI guard reports "skipped", not "not wired".
        file(WRITE "${PACKS_PACK_DIR}/.expected_packs" "# skipped\n")
    else()
        # WASM: wire every copy. A missing pack fails the build with a clear
        # ninja error ("no known rule to make <pack>") until a native build
        # creates it — then the same edge copies it, no reconfigure needed.
        set(_copy_list ${PACKS_PACKS})
    endif()

    foreach(PACK ${_copy_list})
        add_custom_command(
            OUTPUT "${PACKS_ASSETS_DIR}/${PACK}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKS_ASSETS_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PACKS_PACK_DIR}/${PACK}" "${PACKS_ASSETS_DIR}/${PACK}"
            DEPENDS "${PACKS_PACK_DIR}/${PACK}"
            COMMENT "Copying ${PACK} -> assets/"
        )
        list(APPEND _outputs "${PACKS_ASSETS_DIR}/${PACK}")
    endforeach()
    # Optional packs (content-dependent builder output, may be absent) mirror
    # on every build: copied when present, stale copies removed when not —
    # correct in all modes with no configure-time EXISTS snapshot to go stale.
    set(_optional_cmds "")
    foreach(PACK ${PACKS_OPTIONAL_PACKS})
        list(APPEND _optional_cmds COMMAND ${CMAKE_COMMAND}
            -D "SRC=${PACKS_PACK_DIR}/${PACK}" -D "DST=${PACKS_ASSETS_DIR}/${PACK}"
            -P "${NT_ENGINE_ROOT}/cmake/nt_copy_if_exists.cmake")
    endforeach()
    if(_outputs OR _optional_cmds)
        add_custom_target(${PACKS_TARGET}_packs ${_optional_cmds} DEPENDS ${_stamp} ${_outputs})
        add_dependencies(${PACKS_TARGET} ${PACKS_TARGET}_packs)
    endif()
endfunction()
