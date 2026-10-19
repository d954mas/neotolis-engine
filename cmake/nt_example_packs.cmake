# Wires an example's asset-pack build + copy into the build graph and writes
# the .expected_packs manifest the CI guard verifies. Native presets run the
# builder at build time; wasm copies a prior native build's packs or fails loudly.
function(nt_example_packs)
    cmake_parse_arguments(PACKS "" "NAME;TARGET;BUILDER;PACK_DIR;ASSETS_DIR" "PACKS" ${ARGN})
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
        # The builder writes build_packs.d (every file it read) and
        # build_packs.d.globs (every glob pattern with the files it matched) via
        # nt_builder_write_depfile. Before 3.29 ninja wants the depfile's target
        # to name the stamp and reruns the builder on every build.
        if(CMAKE_VERSION VERSION_LESS 3.29)
            message(FATAL_ERROR "nt_example_packs needs CMake 3.29+ (DEPFILE in ninja's deps log)")
        endif()
        set(_depfile "${PACKS_PACK_DIR}/build_packs.d")
        set(_globs "${_depfile}.globs")
        set(_globs_changed "${PACKS_PACK_DIR}/.${PACKS_NAME}.globs_changed")
        set(_globs_script "${NT_ENGINE_ROOT}/cmake/nt_pack_globs.cmake")
        add_custom_target(${PACKS_NAME}_pack_globs
            COMMAND ${CMAKE_COMMAND} -D "GLOBS=${_globs}" -D "CHANGED=${_globs_changed}" -P "${_globs_script}"
            BYPRODUCTS "${_globs_changed}"
        )
        add_custom_command(
            OUTPUT "${_stamp}"
            BYPRODUCTS ${_pack_files}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKS_PACK_DIR}"
            # A builder that stops writing them must fail, not reuse stale lists.
            COMMAND ${CMAKE_COMMAND} -E rm -f "${_depfile}" "${_globs}"
            COMMAND "$<TARGET_FILE:${PACKS_BUILDER}>" "${PACKS_PACK_DIR}"
            COMMAND ${CMAKE_COMMAND} -D "GLOBS=${_globs}" -P "${_globs_script}"
            COMMAND ${CMAKE_COMMAND} -E touch "${_stamp}"
            DEPENDS ${PACKS_BUILDER} "${_globs_changed}"
            DEPFILE "${_depfile}"
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
    if(_outputs)
        add_custom_target(${PACKS_TARGET}_packs DEPENDS ${_stamp} ${_outputs})
        add_dependencies(${PACKS_TARGET} ${PACKS_TARGET}_packs)
    endif()
endfunction()
