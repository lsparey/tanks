cmake_minimum_required(VERSION 3.21)
project(luke-game LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Vulkan 1.3 REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)

add_executable(luke-game
    src/main.cpp
    src/core/Application.cpp
    src/render/VulkanContext.cpp
    src/render/Swapchain.cpp
    src/render/CommandContext.cpp
    src/render/Buffer.cpp
    src/render/Mesh.cpp
    src/render/Pipeline.cpp
    src/render/HudRenderer.cpp
    src/render/Texture.cpp
    src/scene/Camera.cpp
    src/scene/InputManager.cpp
    src/scene/HeightmapGenerator.cpp
    src/scene/GrassTextureGenerator.cpp
    src/scene/Terrain.cpp
    src/scene/Tank.cpp
    src/scene/CollisionSystem.cpp
    src/io/ModelLoader.cpp
)

target_compile_definitions(luke-game PRIVATE
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_FORCE_RADIANS
    ASSET_ROOT="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(luke-game PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
    assimp::assimp
)

include(cmake/CompileShaders.cmake)
compile_shaders(luke-game basic.vert basic.frag hud.vert hud.frag)
