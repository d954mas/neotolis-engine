# Single source of truth for the hull-evidence staleness hash.
# Any file that shapes serialized atlas geometry belongs in this list —
# including the Clipper2 bridge and the vendored Clipper2 sources (CDT + inflate).
# Callers must run from the repository root.

hull_geometry_source_sha256() {
    local sources=(
        tools/builder/nt_builder.h
        tools/builder/nt_builder_atlas.c
        tools/builder/nt_builder_atlas_geometry.c
        tools/builder/nt_builder_atlas_geometry.h
        tools/builder/nt_builder_atlas_vpack.c
        tools/builder/nt_builder_atlas_vpack.h
        tools/builder/nt_clipper2_bridge.cpp
        tools/builder/nt_clipper2_bridge.h
    )
    local clipper
    while IFS= read -r clipper; do
        sources+=("$clipper")
    done < <(printf '%s\n' deps/clipper2/CPP/Clipper2Lib/include/clipper2/*.h deps/clipper2/CPP/Clipper2Lib/src/*.cpp | LC_ALL=C sort)
    sha256sum "${sources[@]}" | sha256sum | awk '{print $1}'
}
