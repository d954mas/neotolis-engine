# cmake -P helper: mirror an OPTIONAL pack into assets — copy when SRC exists,
# remove a stale DST when it does not (the builder legitimately may not produce
# it, and a leftover from an earlier build must not survive the change).
if(EXISTS "${SRC}")
    get_filename_component(_dst_dir "${DST}" DIRECTORY)
    file(MAKE_DIRECTORY "${_dst_dir}")
    file(COPY_FILE "${SRC}" "${DST}" ONLY_IF_DIFFERENT)
else()
    file(REMOVE "${DST}")
endif()
