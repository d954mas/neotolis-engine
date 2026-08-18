# Wires an example's asset packs into the build graph; writes the .expected_packs
# manifest the CI guard verifies. Native: BUILDER runs at build time, packs copy
# into ASSETS_DIR — no manual step, no reconfigure. WASM: copies from a prior
# native build's PACK_DIR; a missing pack fails the build loudly (fail early).
# NT_SKIP_EXAMPLE_PACKS is the only soft path (exe builds without assets).
# OPTIONAL_PACKS may legitimately be absent (content-dependent builder output).
# PACK_DIR is shared by every preset — never build two presets concurrently.
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
    set(_ordering_deps "")
    if(NOT EMSCRIPTEN AND NOT _skipped)
        foreach(PACK ${PACKS_PACKS})
            list(APPEND _pack_files "${PACKS_PACK_DIR}/${PACK}")
        endforeach()
        # Optional packs copy right after the builder run (same command, so the
        # first clean build already lands them in assets/); tolerant of absence.
        set(_optional_cmds "")
        foreach(PACK ${PACKS_OPTIONAL_PACKS})
            list(APPEND _optional_cmds COMMAND ${CMAKE_COMMAND}
                -D "SRC=${PACKS_PACK_DIR}/${PACK}" -D "DST=${PACKS_ASSETS_DIR}/${PACK}"
                -P "${NT_ENGINE_ROOT}/cmake/nt_copy_if_exists.cmake")
        endforeach()
        add_custom_command(
            OUTPUT ${_pack_files}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKS_PACK_DIR}"
            COMMAND "$<TARGET_FILE:${PACKS_BUILDER}>" "${PACKS_PACK_DIR}"
            ${_optional_cmds}
            DEPENDS ${PACKS_BUILDER}
            WORKING_DIRECTORY "${NT_ENGINE_ROOT}"
            COMMENT "Building ${PACKS_NAME} pack(s)"
        )
        set(_copy_list ${PACKS_PACKS})
        # The builder rewrites optional packs without declaring them as OUTPUT;
        # ordering their declared copies after the required outputs prevents
        # copying a pack mid-rewrite.
        set(_ordering_deps ${_pack_files})
        # Manifest: required packs plain, optional prefixed '?' (may be absent,
        # but nothing outside this union may appear).
        set(_manifest ${PACKS_PACKS})
        foreach(PACK ${PACKS_OPTIONAL_PACKS})
            list(APPEND _manifest "?${PACK}")
        endforeach()
        string(REPLACE ";" "\n" _manifest_body "${_manifest}")
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
