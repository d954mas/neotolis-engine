#pragma once

#include <cstdint>
#include <filesystem>

namespace methane_reference {

void export_scene_data(const std::filesystem::path &output_dir, uint32_t complexity);
void export_texture_data(const std::filesystem::path &output_dir, uint32_t random_seed = 1123U, uint32_t textures_count = 50U);

} // namespace methane_reference
