cmake_minimum_required(VERSION 3.21)
project(tanks LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TANKS_BUILD_GAME "Build the Vulkan game and its renderer/gameplay tests" ON)
find_package(glm CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_library(terrain_generation STATIC
    src/scene/HeightmapGenerator.cpp
    src/scene/MacroTerrain.cpp
    src/scene/HydraulicErosion.cpp
    src/scene/TerrainSurface.cpp
    src/scene/TerrainGenerator.cpp
)
target_include_directories(terrain_generation PUBLIC src)
target_link_libraries(terrain_generation PUBLIC glm::glm Threads::Threads)

add_executable(terrain_probe tools/TerrainProbe.cpp)
target_link_libraries(terrain_probe PRIVATE terrain_generation)

include(CTest)
if(BUILD_TESTING)
    add_executable(terrain_surface_test tests/TerrainSurfaceTest.cpp)
    target_link_libraries(terrain_surface_test PRIVATE terrain_generation)
    add_test(NAME terrain_surface COMMAND terrain_surface_test)
    add_executable(macro_terrain_test tests/MacroTerrainTest.cpp)
    target_link_libraries(macro_terrain_test PRIVATE terrain_generation)
    add_test(NAME macro_terrain COMMAND macro_terrain_test)
    add_executable(hydraulic_erosion_test tests/HydraulicErosionTest.cpp)
    target_link_libraries(hydraulic_erosion_test PRIVATE terrain_generation)
    add_test(NAME hydraulic_erosion COMMAND hydraulic_erosion_test)
endif()

if(NOT TANKS_BUILD_GAME)
    return()
endif()

find_package(Vulkan 1.3 REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(Stb REQUIRED)

add_executable(tanks
    src/main.cpp
    src/core/Application.cpp
    src/render/VulkanContext.cpp
    src/render/Swapchain.cpp
    src/render/CommandContext.cpp
    src/render/Buffer.cpp
    src/render/Mesh.cpp
    src/render/VoxelSurface.cpp
    src/render/Pipeline.cpp
    src/render/HudRenderer.cpp
    src/render/Texture.cpp
    src/render/SingleTimeCommands.cpp
    src/render/AccelerationStructure.cpp
    src/render/RayTracingFunctions.cpp
    src/render/SceneAccelerationStructure.cpp
    src/render/HistoryBuffer.cpp
    src/render/ScreenshotWriter.cpp
    src/scene/Camera.cpp
    src/scene/InputManager.cpp
    src/scene/GrassTextureGenerator.cpp
    src/scene/RockTextureGenerator.cpp
    src/scene/TrackTextureGenerator.cpp
    src/scene/WaterGenerator.cpp
    src/scene/BoundaryGenerator.cpp
    src/scene/BoundaryTextureGenerator.cpp
    src/scene/CloudTextureGenerator.cpp
    src/scene/BarkTextureGenerator.cpp
    src/scene/TreeGenerator.cpp
    src/scene/LeafTextureGenerator.cpp
    src/scene/CrateTextureGenerator.cpp
    src/scene/CamoTextureGenerator.cpp
    src/scene/MetalTextureGenerator.cpp
    src/scene/Terrain.cpp
    src/scene/Tank.cpp
    src/scene/RunningGear.cpp
    src/scene/CollisionSystem.cpp
    src/io/ModelLoader.cpp
)

target_compile_definitions(tanks PRIVATE
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_FORCE_RADIANS
    ASSET_ROOT="${CMAKE_SOURCE_DIR}"
)

target_include_directories(tanks PRIVATE ${Stb_INCLUDE_DIR})

target_link_libraries(tanks PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
    assimp::assimp
    Threads::Threads
    terrain_generation
)

include(cmake/CompileShaders.cmake)
compile_shaders(tanks basic.vert basic.frag foliage_depth.frag hud.vert hud.frag)

if(BUILD_TESTING)
    add_executable(foliage_lod_test tests/FoliageLodTest.cpp)
    target_include_directories(foliage_lod_test PRIVATE src)
    target_link_libraries(foliage_lod_test PRIVATE glm::glm)
    add_test(NAME foliage_lod COMMAND foliage_lod_test)
    add_executable(tree_mesh_test tests/TreeMeshTest.cpp src/render/Mesh.cpp
        src/render/VoxelSurface.cpp src/render/Buffer.cpp src/render/SingleTimeCommands.cpp
        src/scene/TreeGenerator.cpp)
    target_include_directories(tree_mesh_test PRIVATE src)
    target_link_libraries(tree_mesh_test PRIVATE glm::glm Vulkan::Vulkan glfw Threads::Threads)
    add_test(NAME tree_mesh COMMAND tree_mesh_test)
    add_executable(tree_generator_test tests/TreeGeneratorTest.cpp src/scene/TreeGenerator.cpp)
    target_include_directories(tree_generator_test PRIVATE src)
    target_link_libraries(tree_generator_test PRIVATE glm::glm)
    add_test(NAME tree_generator COMMAND tree_generator_test)
    add_executable(voxel_surface_test tests/VoxelSurfaceTest.cpp src/render/VoxelSurface.cpp)
    target_include_directories(voxel_surface_test PRIVATE src)
    target_link_libraries(voxel_surface_test PRIVATE glm::glm Vulkan::Vulkan)
    add_test(NAME voxel_surface COMMAND voxel_surface_test)
    add_executable(frame_profiler_test tests/FrameProfilerTest.cpp)
    target_include_directories(frame_profiler_test PRIVATE src)
    add_test(NAME frame_profiler COMMAND frame_profiler_test)
    add_executable(tank_surface_test tests/TankSurfaceTest.cpp)
    target_include_directories(tank_surface_test PRIVATE src)
    target_link_libraries(tank_surface_test PRIVATE glm::glm Vulkan::Vulkan)
    add_test(NAME tank_surface COMMAND tank_surface_test)
    add_executable(tank_model_test tests/TankModelTest.cpp src/io/ModelLoader.cpp)
    target_include_directories(tank_model_test PRIVATE src)
    target_compile_definitions(tank_model_test PRIVATE ASSET_ROOT="${CMAKE_SOURCE_DIR}")
    target_link_libraries(tank_model_test PRIVATE glm::glm Vulkan::Vulkan assimp::assimp)
    add_test(NAME tank_model COMMAND tank_model_test)
    add_executable(running_gear_test tests/RunningGearTest.cpp src/scene/RunningGear.cpp src/io/ModelLoader.cpp)
    target_include_directories(running_gear_test PRIVATE src)
    target_compile_definitions(running_gear_test PRIVATE ASSET_ROOT="${CMAKE_SOURCE_DIR}")
    target_link_libraries(running_gear_test PRIVATE glm::glm Vulkan::Vulkan assimp::assimp)
    add_test(NAME running_gear COMMAND running_gear_test)
    add_executable(weapon_effects_test tests/WeaponEffectsTest.cpp)
    target_include_directories(weapon_effects_test PRIVATE src)
    target_link_libraries(weapon_effects_test PRIVATE glm::glm)
    add_test(NAME weapon_effects COMMAND weapon_effects_test)
endif()
