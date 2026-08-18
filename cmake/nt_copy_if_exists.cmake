# cmake -P helper: copy SRC to DST when SRC exists — for optional pack outputs
# the builder may legitimately not produce (so a plain copy_if_different would fail).
if(EXISTS "${SRC}")
    get_filename_component(_dst_dir "${DST}" DIRECTORY)
    file(MAKE_DIRECTORY "${_dst_dir}")
    file(COPY_FILE "${SRC}" "${DST}" ONLY_IF_DIFFERENT)
endif()
