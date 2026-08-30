# tanks

A from-scratch 3D tank prototype: drive a tank over procedural terrain and
shoot procedural boxes. Built directly on Vulkan (no engine) with GLFW, GLM,
and Assimp as the only dependencies. Targets Linux with an Intel GPU (Mesa
ANV driver).

See `.claude`-adjacent plan notes for the full milestone breakdown. Current
status: **M1 — window + swapchain + clear-color loop.**

## Prerequisites (Ubuntu/Debian-style apt)

```bash
sudo apt install -y cmake ninja-build libvulkan-dev vulkan-validationlayers \
    vulkan-tools spirv-tools glslc git pkg-config \
    libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
    libwayland-dev libxkbcommon-dev extra-cmake-modules
```

vcpkg is vendored as a plain (gitignored) clone at `./vcpkg`, not a
submodule. If it's missing:

```bash
git clone https://github.com/microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh -disableMetrics
```

## Assets

Place your tank model at `assets/models/tank.x` (needed starting at
milestone M5; not required to build/run M1-M4).

## Build & run

```bash
cmake --preset default
cmake --build --preset default
./build/tanks
```

The first configure will take a while: vcpkg builds GLFW, GLM, and Assimp
from source.
