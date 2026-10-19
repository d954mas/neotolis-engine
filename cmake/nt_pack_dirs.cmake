# Lists the files of every directory named in a builder depfile, so adding or
# removing a file in a globbed directory rebuilds the pack. A directory entry in
# the depfile alone is not enough: on Windows ninja reads a directory's mtime from
# its parent's listing, which NTFS updates lazily.
#
# With CHANGED (an always-run step before the pack): touch CHANGED when the
# listing differs from LISTING. Without it (after the builder): write LISTING.

set(_listing "")
if(EXISTS "${DEPFILE}")
    # One path per line, as nt_builder_write_depfile writes it. Drop the line
    # continuations first: a trailing backslash would escape CMake's list separator.
    file(READ "${DEPFILE}" _text)
    string(REPLACE " \\\n" "\n" _text "${_text}")
    string(REPLACE "\\ " " " _text "${_text}")
    string(REPLACE "\\#" "#" _text "${_text}")
    string(REPLACE "$$" "$" _text "${_text}")
    string(REPLACE "\n" ";" _lines "${_text}")
    foreach(_line IN LISTS _lines)
        string(STRIP "${_line}" _path)
        if(IS_DIRECTORY "${_path}")
            file(GLOB _files LIST_DIRECTORIES false "${_path}/*")
            list(SORT _files)
            string(JOIN "\n" _files_text ${_files})
            string(APPEND _listing "${_path}:\n${_files_text}\n")
        endif()
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
    file(TOUCH "${CHANGED}")
endif()
