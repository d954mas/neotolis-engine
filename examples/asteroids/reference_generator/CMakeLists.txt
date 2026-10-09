cmake_minimum_required(VERSION 3.20)
project(AsteroidsReferenceGenerator LANGUAGES CXX)

set(AST_REFERENCE_DEPENDENCIES "${CMAKE_CURRENT_SOURCE_DIR}/deps" CACHE PATH "Pinned sources populated by fetch_dependencies.py")
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(FASTSIMD_STANDALONE_PROJECT OFF)
set(FASTSIMD_TESTS OFF)
set(FASTSIMD_EXAMPLES OFF)
add_subdirectory("${AST_REFERENCE_DEPENDENCIES}/FastSIMD" fastsimd)

# Use upstream's relaxed math with one fixed official dispatch target.
file(RELATIVE_PATH noise_dispatch "${CMAKE_CURRENT_SOURCE_DIR}" "${AST_REFERENCE_DEPENDENCIES}/FastNoise2/src/FastNoise/FastSIMD_Build.inl")
fastsimd_create_dispatch_library(FastSIMD_FastNoise RELAXED SOURCES "${noise_dispatch}" FEATURE_SETS SSE2)
target_include_directories(FastSIMD_FastNoise PRIVATE "${AST_REFERENCE_DEPENDENCIES}/FastNoise2/include")
add_library(FastNoise STATIC
    "${AST_REFERENCE_DEPENDENCIES}/FastNoise2/src/FastNoise/Metadata.cpp"
    "${AST_REFERENCE_DEPENDENCIES}/FastNoise2/src/FastNoise/SmartNode.cpp"
)
target_include_directories(FastNoise PUBLIC "${AST_REFERENCE_DEPENDENCIES}/FastNoise2/include")
target_compile_definitions(FastNoise PRIVATE FASTNOISE_EXPORT PUBLIC FASTNOISE_STATIC_LIB FASTNOISE2_VERSION="1.1.1")
target_link_libraries(FastNoise PUBLIC FastSIMD FastSIMD_FastNoise)
if(MSVC)
    target_compile_options(FastSIMD_FastNoise PRIVATE /fp:fast)
else()
    target_compile_options(FastSIMD_FastNoise PRIVATE -ffast-math -Wno-ignored-attributes)
endif()

add_executable(asteroids_reference_generator main.cpp)
target_include_directories(asteroids_reference_generator PRIVATE "${AST_REFERENCE_DEPENDENCIES}/HLSLpp/include")
target_compile_definitions(asteroids_reference_generator PRIVATE HLSLPP_FEATURE_TRANSFORM HLSLPP_LOGICAL_LAYOUT=0 HLSLPP_COORDINATES=0)
target_link_libraries(asteroids_reference_generator PRIVATE FastNoise)

add_executable(asteroids_reference_scene_exporter
    ../reference_scene_data/scene_export.cpp
    ../reference_scene_data/scene_export_main.cpp
)
target_include_directories(asteroids_reference_scene_exporter PRIVATE "${AST_REFERENCE_DEPENDENCIES}/HLSLpp/include")
target_compile_definitions(asteroids_reference_scene_exporter PRIVATE HLSLPP_FEATURE_TRANSFORM HLSLPP_LOGICAL_LAYOUT=0 HLSLPP_COORDINATES=0)
target_link_libraries(asteroids_reference_scene_exporter PRIVATE FastNoise)

add_executable(asteroids_reference_parity source_parity.cpp)
target_include_directories(asteroids_reference_parity PRIVATE oracle_adapters "${AST_REFERENCE_DEPENDENCIES}/HLSLpp/include")
target_compile_definitions(asteroids_reference_parity PRIVATE HLSLPP_FEATURE_TRANSFORM HLSLPP_LOGICAL_LAYOUT=0 HLSLPP_COORDINATES=0)
target_link_libraries(asteroids_reference_parity PRIVATE FastNoise)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR OR NT_BUILD_TESTS)
    enable_testing()
    add_test(NAME asteroids_reference_parity COMMAND asteroids_reference_parity)
endif()
