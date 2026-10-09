#include "scene_export.hpp"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        std::fprintf(stderr, "usage: asteroids_reference_scene_exporter OUTPUT_DIR [--scenes-only|--textures-only]\n");
        return 2;
    }
    const std::string mode = argc == 3 ? argv[2] : "";
    if (!mode.empty() && mode != "--scenes-only" && mode != "--textures-only") {
        std::fprintf(stderr, "unknown mode: %s\n", mode.c_str());
        return 2;
    }
    try {
        const std::filesystem::path output_dir(argv[1]);
        if (mode != "--textures-only") {
            for (uint32_t complexity = 0; complexity < 10; ++complexity) {
                methane_reference::export_scene_data(output_dir, complexity);
                std::printf("Exported scene complexity %u\n", complexity);
            }
        }
        if (mode != "--scenes-only") {
            methane_reference::export_texture_data(output_dir / "textures");
            std::printf("Exported 50 textures, each with 3 original face layers\n");
        }
    } catch (const std::exception &error) {
        std::fprintf(stderr, "reference export failed: %s\n", error.what());
        return 1;
    }
    return 0;
}
