# GLOBS (written by nt_builder_write_depfile) holds every glob pattern the
# builder walked with the file names it matched. Re-list the patterns the way
# nt_builder_glob_iterate does (case-sensitive '*' and '?', files only) and touch
# CHANGED when the result differs, so adding or removing a matching file reruns
# the builder even with an old timestamp. Directory mtimes cannot do this: on
# Windows ninja reads them from the parent's listing, which NTFS updates lazily.
#
# Without CHANGED (after the builder): only check that GLOBS was written.

if(NOT DEFINED CHANGED)
    if(NOT EXISTS "${GLOBS}")
        message(FATAL_ERROR "${GLOBS} is missing: the pack builder must call nt_builder_write_depfile last")
    endif()
    return()
endif()

set(_recorded "")
set(_listing "")
if(EXISTS "${GLOBS}")
    file(READ "${GLOBS}" _recorded)
    file(STRINGS "${GLOBS}" _lines)
    foreach(_line IN LISTS _lines)
        if(_line MATCHES "^  ")
            continue()
        endif()
        get_filename_component(_dir "${_line}" DIRECTORY)
        get_filename_component(_name_pattern "${_line}" NAME)
        string(REGEX REPLACE "([][.+^$(){}|\\\\])" "\\\\\\1" _regex "${_name_pattern}")
        string(REPLACE "*" ".*" _regex "${_regex}")
        string(REPLACE "?" "." _regex "${_regex}")
        file(GLOB _files LIST_DIRECTORIES false "${_dir}/*")
        set(_names "")
        foreach(_file IN LISTS _files)
            get_filename_component(_name "${_file}" NAME)
            if(_name MATCHES "^${_regex}$")
                list(APPEND _names "${_name}")
            endif()
        endforeach()
        list(SORT _names)
        string(APPEND _listing "${_line}\n")
        foreach(_name IN LISTS _names)
            string(APPEND _listing "  ${_name}\n")
        endforeach()
    endforeach()
endif()

if(NOT EXISTS "${CHANGED}" OR NOT _recorded STREQUAL _listing)
    get_filename_component(_changed_dir "${CHANGED}" DIRECTORY)
    file(MAKE_DIRECTORY "${_changed_dir}")
    file(TOUCH "${CHANGED}")
endif()
