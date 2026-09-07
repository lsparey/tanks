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
    src/scene/TerrainDrainage.cpp
    src/scene/LakeWater.cpp
    src/scene/ShorelineIndex.cpp
    src/scene/StreamNetwork.cpp
    src/scene/StreamSections.cpp
    src/scene/ChannelCarving.cpp
    src/scene/TerrainWater.cpp
    src/scene/TerrainPlayability.cpp
    src/scene/TerrainSurface.cpp
    src/scene/TerrainGenerator.cpp
    src/scene/TerrainSelection.cpp
    src/scene/TerrainRuntime.cpp
)
target_include_directories(terrain_generation PUBLIC src)
target_link_libraries(terrain_generation PUBLIC glm::glm Threads::Threads)

add_executable(terrain_probe tools/TerrainProbe.cpp)
target_link_libraries(terrain_probe PRIVATE terrain_generation)
add_executable(shoreline_probe tools/ShorelineProbe.cpp)
target_link_libraries(shoreline_probe PRIVATE terrain_generation)
add_executable(terrain_selection_probe tools/TerrainSelectionProbe.cpp)
target_link_libraries(terrain_selection_probe PRIVATE terrain_generation)

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
    add_executable(terrain_drainage_test tests/TerrainDrainageTest.cpp)
    target_link_libraries(terrain_drainage_test PRIVATE terrain_generation)
    add_test(NAME terrain_drainage COMMAND terrain_drainage_test)
    add_executable(lake_water_test tests/LakeWaterTest.cpp)
    target_link_libraries(lake_water_test PRIVATE terrain_generation)
    add_test(NAME lake_water COMMAND lake_water_test)
    add_executable(stream_network_test tests/StreamNetworkTest.cpp)
    target_link_libraries(stream_network_test PRIVATE terrain_generation)
    add_test(NAME stream_network COMMAND stream_network_test)
    add_executable(stream_sections_test tests/StreamSectionsTest.cpp)
    target_link_libraries(stream_sections_test PRIVATE terrain_generation)
    add_test(NAME stream_sections COMMAND stream_sections_test)
    add_executable(channel_carving_test tests/ChannelCarvingTest.cpp)
    target_link_libraries(channel_carving_test PRIVATE terrain_generation)
    add_test(NAME channel_carving COMMAND channel_carving_test)
    add_executable(terrain_water_test tests/TerrainWaterTest.cpp)
    target_link_libraries(terrain_water_test PRIVATE terrain_generation)
    add_test(NAME terrain_water COMMAND terrain_water_test)
    add_executable(shoreline_index_test tests/ShorelineIndexTest.cpp)
    target_link_libraries(shoreline_index_test PRIVATE terrain_generation)
    add_test(NAME shoreline_index COMMAND shoreline_index_test)
    add_executable(terrain_playability_test tests/TerrainPlayabilityTest.cpp)
    target_link_libraries(terrain_playability_test PRIVATE terrain_generation)
    add_test(NAME terrain_playability COMMAND terrain_playability_test)
    add_executable(terrain_selection_test tests/TerrainSelectionTest.cpp)
    target_link_libraries(terrain_selection_test PRIVATE terrain_generation)
    add_test(NAME terrain_selection COMMAND terrain_selection_test)
    add_executable(terrain_runtime_test tests/TerrainRuntimeTest.cpp)
    target_link_libraries(terrain_runtime_test PRIVATE terrain_generation)
    add_test(NAME terrain_runtime COMMAND terrain_runtime_test)
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
    add_executable(terrain_scenery_test tests/TerrainSceneryTest.cpp src/render/Mesh.cpp
        src/render/VoxelSurface.cpp src/render/Buffer.cpp src/render/SingleTimeCommands.cpp
        src/scene/TreeGenerator.cpp)
    target_link_libraries(terrain_scenery_test PRIVATE terrain_generation Vulkan::Vulkan glfw)
    add_test(NAME terrain_scenery COMMAND terrain_scenery_test)
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
