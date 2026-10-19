# Lists the files matching every glob pattern the builder walked (GLOBS, written
# by nt_builder_write_depfile), so adding or removing a matching file reruns the
# builder even with an old timestamp. Directory mtimes cannot do this: on Windows
# ninja reads them from the parent's listing, which NTFS updates lazily.
#
# With CHANGED (an always-run step before the pack): touch CHANGED when the
# listing differs from LISTING. Without it (after the builder): write LISTING.

if(NOT DEFINED CHANGED AND NOT EXISTS "${GLOBS}")
    message(FATAL_ERROR "${GLOBS} is missing: the pack builder must call nt_builder_write_depfile last")
endif()

set(_listing "")
if(EXISTS "${GLOBS}")
    file(STRINGS "${GLOBS}" _patterns)
    foreach(_pattern IN LISTS _patterns)
        file(GLOB _files LIST_DIRECTORIES false "${_pattern}")
        list(SORT _files)
        string(JOIN "\n" _files_text ${_files})
        string(APPEND _listing "${_pattern}:\n${_files_text}\n")
    endforeach()
endif()

if(NOT DEFINED CHANGED)
    file(WRITE "${LISTING}" "${_listing}")
    return()
endif()

set(_recorded "")
if(EXISTS "${LISTING}")
    file(READ "${LISTING}" _recorded)
endif()
if(NOT EXISTS "${CHANGED}" OR NOT _recorded STREQUAL _listing)
    get_filename_component(_changed_dir "${CHANGED}" DIRECTORY)
    file(MAKE_DIRECTORY "${_changed_dir}")
    file(TOUCH "${CHANGED}")
endif()
