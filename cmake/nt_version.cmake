# cmake -DGIT=<git> -DROOT=<engine root> -DOUT=<header> -P nt_version.cmake
#
# Build-time step of nt_version: writes NT_VERSION_COMMIT, the engine's git commit
# (first 8 hex, "-dirty" when tracked files differ from HEAD) or "unknown". The
# header is replaced only when the value changes, so an unchanged tree rebuilds nothing.

set(_commit "unknown")
if(GIT)
    execute_process(COMMAND "${GIT}" -C "${ROOT}" rev-parse --show-toplevel
        OUTPUT_VARIABLE _top RESULT_VARIABLE _rc OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_rc EQUAL 0)
        file(REAL_PATH "${_top}" _top)
        file(REAL_PATH "${ROOT}" _root)
        if(CMAKE_HOST_WIN32)
            string(TOLOWER "${_top}" _top)
            string(TOLOWER "${_root}" _root)
        endif()
        # An engine copied into another repository without its own .git would report the host's commit.
        if(_top STREQUAL _root)
            execute_process(COMMAND "${GIT}" -C "${ROOT}" rev-parse HEAD
                OUTPUT_VARIABLE _head RESULT_VARIABLE _rc OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(_rc EQUAL 0)
                # --short=8 may print more than 8 hex digits when the prefix is ambiguous.
                string(SUBSTRING "${_head}" 0 8 _commit)
                set(ENV{GIT_OPTIONAL_LOCKS} 0)
                execute_process(COMMAND "${GIT}" -C "${ROOT}" diff --quiet HEAD
                    RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_QUIET)
                if(_rc EQUAL 1)
                    string(APPEND _commit "-dirty")
                elseif(NOT _rc EQUAL 0)
                    set(_commit "unknown") # a failed diff cannot vouch for a clean tree
                endif()
            endif()
        endif()
    endif()
endif()

file(WRITE "${OUT}.tmp" "#define NT_VERSION_COMMIT \"${_commit}\"\n")
file(COPY_FILE "${OUT}.tmp" "${OUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUT}.tmp")
