# NT_PIN_<KEY> for every line of .toolchain-versions, and NT_TOOLCHAIN_DIR, where
# scripts/setup_toolchain.py installs them (Python's Path.home(): USERPROFILE on Windows).
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../.toolchain-versions" _nt_pins REGEX "^[A-Z0-9_]+=")
foreach(_nt_pin IN LISTS _nt_pins)
    string(REGEX MATCH "^([A-Z0-9_]+)=(.*)$" _nt_match "${_nt_pin}")
    set(NT_PIN_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
endforeach()
if(CMAKE_HOST_WIN32)
    file(TO_CMAKE_PATH "$ENV{USERPROFILE}/.neotolis/toolchain" NT_TOOLCHAIN_DIR)
else()
    set(NT_TOOLCHAIN_DIR "$ENV{HOME}/.neotolis/toolchain")
endif()
