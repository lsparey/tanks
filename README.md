# Tanks

Tanks is a from-scratch 3D tank prototype built directly on Vulkan, without a
game engine. Drive a multipart tank across a procedurally generated landscape,
traverse the turret, and fire at destructible targets while hardware ray
queries provide shadows, ambient occlusion, and selected reflections.

The project currently targets Linux and is developed against the Mesa Vulkan
drivers. The renderer, simulation, terrain, materials, effects, and lightweight
HUD are implemented in C++20; GLFW, GLM, Assimp, and stb are the only third-party
runtime/build libraries.

## Highlights

- Momentum-based tank movement with differential steering, slope response,
  obstacle sliding, and four-point visual suspension.
- Independent tread marks and contact-, speed-, and slip-driven track dust.
- Procedural terrain, river water, English temperate grass, trees, shrubs,
  rocks, cliffs, scree, clouds, and play-area boundary. Pine, ash and oak
  forms have full summer crowns, overlapping thin leaf sprays, varied density
  and coherent wind sway. Oaks use separate lobed leaves and a deep branching
  crown; CPU tree builds run concurrently while the loading screen stays active.
  Foliage detail changes progressively per bough, with complementary coverage
  masks blending adjacent levels while retaining dynamic lighting and soft shadows.
- Multipart tank model with independent turret and barrel transforms.
- Editable Challenger-2-inspired mesh with sloped armour, road wheels, hubs,
  track belts and tread shoes; the original model remains selectable.
- Distinct armour, track, and barrel finishes with subtle edge wear, lower-hull
  dust, and muzzle soot attached to the model through movement and aiming.
- Projectiles, destructible crates, impacts, debris, smoke, shell trails, and
  short-lived explosion lighting.
- Vulkan dynamic rendering, MSAA, instanced scenery, mesh LODs, GPU timings,
  and temporally stabilized ray-query effects.
- In-engine PNG screenshot capture for interactive and automated use.

See [PLAN.md](PLAN.md) for the completed-feature index and possible future
improvements. The [tree-rendering guide](docs/TREE_RENDERING.md) covers crown
geometry, threaded loading, progressive LOD, wind, shadows, measured costs and
validation.

## Rendering direction

The broader goal is a more realistic-looking game using modern techniques
suited to Arc A370M and native Linux/Mesa Vulkan. The
[rendering roadmap](docs/RENDERING_ROADMAP.md) prioritises physically based
materials, linear HDR, temporal image stability and measured ray/geometry
budgets, followed by selective vegetation, lighting and reconstruction work.
These are planned upgrades; the feature list above describes what runs today.

Terrain erosion remains the immediate goal. Establish representative Release
frame times and memory use before choosing quality defaults. Retain the accepted
tree fidelity and roughly 5.3-second startup target; hardware support alone
never establishes that an advanced effect fits the frame budget.

## Next development goal

The next planned overhaul is [hydraulic erosion terrain generation](docs/TERRAIN_EROSION_PLAN.md):
connected landforms shaped by water and sediment, followed by drainage, rivers
and lakes, ground materials and environment placement derived from the same
data. The current terrain, water and non-tree scenery can be replaced. Existing
tree geometry, density, progressive LOD, wind and soft shadows are preserved;
tree placement will adapt to the new ground.

The first foundation is implemented: CPU generation is separate from Vulkan
upload, contact heights follow the rendered triangles, and a standalone tool
provides repeatable timing and neutral diagnostic exports. The legacy landform
remains active. A selectable CPU rolling-valley prototype now adds broad
landforms, bedrock/soil, erodibility and an apron with open boundary faces;
see the [macro terrain guide](docs/TERRAIN_MACRO.md) for previews and commands.
A separate [eroded-valley CPU prototype](docs/TERRAIN_EROSION_PROTOTYPE.md) now
simulates hydraulic transport, deposition and limited soil relaxation, with
conservation budgets and deterministic parallel workers. Persistent hydrology,
spawn/routes, the environment rebuild and game integration are still planned.
See the [terrain baseline](docs/TERRAIN_BASELINE.md) for commands, measurements
and outstanding hardware checks.

## Requirements

### Hardware

A Vulkan-capable GPU and driver with all of the following are required. There
is currently no raster-only fallback:

- Vulkan 1.3, including dynamic rendering and synchronization2.
- Graphics and presentation queues with swapchain support.
- Buffer device address.
- `VK_KHR_acceleration_structure`.
- `VK_KHR_deferred_host_operations`.
- `VK_KHR_ray_query`.
- `VK_KHR_ray_tracing_position_fetch`.
- Sampler anisotropy and independent blending.

The renderer automatically prefers a discrete GPU, then an integrated GPU,
when more than one compatible device is available. It uses 4x MSAA where
supported, falls back to 2x or 1x, and opens at 1280x720. The project has been
developed on Intel Raptor Lake integrated graphics and Intel Arc A370M with
Mesa ANV. Other sufficiently capable Vulkan drivers should work but are not
yet a regularly tested target. Tree batches use optional multi-draw indirect
features when supported, with a direct indexed fallback otherwise.

Use `vulkaninfo` to inspect the installed driver before building:

```bash
vulkaninfo --summary
vulkaninfo | grep -E 'VK_KHR_(acceleration_structure|deferred_host_operations|ray_query|ray_tracing_position_fetch)'
```

The application performs the authoritative feature check at startup and
reports the selected GPU in the terminal.

### Software

- 64-bit Linux with an X11 or Wayland desktop session.
- A C++20 compiler (GCC or Clang).
- CMake 3.21 or newer and Ninja.
- Git and a local vcpkg checkout.
- Vulkan 1.3 headers and loader.
- `glslc` for compiling GLSL shaders to SPIR-V.
- Khronos validation layers when using the default Debug build.

GLFW, GLM, Assimp, and stb are installed by vcpkg from [`vcpkg.json`](vcpkg.json).

## Installing prerequisites

On Ubuntu or Debian-based distributions:

```bash
sudo apt update
sudo apt install -y \
    build-essential cmake ninja-build git curl zip unzip pkg-config \
    libvulkan-dev vulkan-tools vulkan-validationlayers glslc spirv-tools \
    libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
    libwayland-dev wayland-protocols libxkbcommon-dev extra-cmake-modules
```

Install a current Vulkan driver for the GPU separately. On other Linux
distributions, install the equivalent compiler, Vulkan SDK/validation-layer,
shader-compiler, and GLFW platform-development packages.

## Dependency setup

vcpkg is expected at `./vcpkg`. It is intentionally gitignored rather than
stored as a submodule. From the repository root, create it if necessary:

```bash
git clone https://github.com/microsoft/vcpkg.git vcpkg
./vcpkg/bootstrap-vcpkg.sh -disableMetrics
```

No separate asset download is needed. The default tank model is included at
`assets/models/challenger2.obj`, with the original at `assets/models/tank.x`;
the remaining textures and environment geometry are
generated by the application.

## Build

Configure and build the default Debug preset from the repository root:

```bash
cmake --preset default
cmake --build --preset default
```

The first configure can take several minutes while vcpkg downloads and builds
the dependencies. Later builds are incremental. Shader changes are compiled
automatically by the build.

For an optimized build, configure the same preset with a Release override:

```bash
cmake --preset default -DCMAKE_BUILD_TYPE=Release
cmake --build --preset default
```

## Run

Start the interactive prototype with:

```bash
./build/tanks
```

Runtime assets are resolved from the source tree, so the executable may also
be launched from another working directory. Startup prints the selected GPU,
ray-query support, and chosen MSAA sample count. Resize the window normally;
the swapchain and dependent render targets are recreated automatically.

### Controls

| Input | Action |
| --- | --- |
| `W` / `S` | Drive forward / reverse |
| `A` / `D` | Steer left / right; pivot at low speed |
| `Q` / `E` | Traverse turret left / right |
| `R` / `F` | Raise / lower the main gun (+20° / −10° limits) |
| Left mouse button or `Space` | Fire |
| `C` | Cycle hull-follow, turret-aiming, and free-camera modes |
| Mouse | Look around in free-camera mode |
| Arrow keys | Move horizontally in free-camera mode |
| `Space` / Left `Ctrl` | Move up / down in free-camera mode |
| `F3` | Toggle performance summary in the window title and detailed terminal reports |
| `F4` | Reset performance samples for a new measurement |
| `F12` | Save a PNG under `screenshots/` |
| `Esc` | Quit |

The mouse cursor is captured while the application is running. In free-camera
mode, `Space` both raises the camera and fires because firing remains active.

## Performance measurements

Press `F3`, or start with `./build/tanks --profile`. The window title shows
average frame time, p99, combined wait/API time, GPU time, and draw calls.
The terminal prints a detailed report once per second and on exit. To save
reports, run `./build/tanks --profile | tee /tmp/tanks-performance.log`.

CPU statistics cover the last 240 measured frames: mean, p95, p99, and worst
frame time, plus simulation, visibility/grouping and instance upload, TLAS
instance gathering, command recording, and queue submission. The slot fence,
cross-frame history fence, swapchain acquisition, and presentation calls are
timed separately. `other` covers remaining loop work such as events and UBO
updates. These phases partition CPU wall time; they are not CPU utilization.
Frame timing covers the game loop through presentation, excluding report
printing and title updates, and does not measure when pixels reach the screen.

GPU stage timings remain asynchronous exponential moving averages, read only
after an existing fence wait. They describe older submissions and overlap CPU
work: do not add them to CPU timings. The renderer uses FIFO (vsync), so time
in acquire/present or a fence can reflect frame pacing as well as GPU work.
A long wait alone does not establish that ray tracing is the bottleneck.

The scenery breakdown separates tree bark, foliage depth, foliage lighting and
other props. These are subsets of the reported scenery GPU time, not extra work
to add to the total. For repeatable Release comparisons, use
[`tools/benchmark_runtime.py`](tools/benchmark_runtime.py); see the
[recorded terrain performance results](docs/TERRAIN_RUNTIME_PERFORMANCE.md).

Counts are averages over the same CPU window. Draw calls are counted at the
actual Vulkan draw sites, including the HUD; an instanced batch counts once.
A multi-draw foliage batch also counts once per pass, even though it contains
several indexed commands. Trees use a masked depth pass followed by lighting;
draw-call counts alone do not describe their geometry or shading cost.
Visible props count unique scenery instances accepted by culling (trees,
rocks, scree, shrubs, and cliff sections), not their separate material passes
or every object in the scene. TLAS instances count the ray-query scene.

The first 60 successfully rendered frames are warmup. Screenshot frames and
swapchain-recreation frames are excluded; resizing clears the window and
restarts warmup. `F4` clears CPU samples and pending GPU timing results. Use
the same optimized build, window size, camera, and power settings when
comparing results; Debug timings are not representative of release speed.

Use these short scenarios, allowing the sample window to fill after `F4`:

1. **Stationary:** leave the tank and hull camera at spawn for an idle baseline.
2. **Scenery:** park at a dense cluster of trees/rocks and measure a fixed view.
3. **Driving/effects:** drive a consistent loop to accumulate tread marks and
   dust, then fire at crates to compare recording, draw counts, and GPU effects.

For static comparisons across launches, use `--seed 7331 --view landscape`.
The seed fixes terrain, material variants, and prop placement; `--view` also
accepts `tank`, `tank-side`, `tank-front`, `tank-rear`, `tank-top`, `water`, and `cliffs` and holds a reference camera until `C` is
pressed. Without an explicit seed, reference views use 7331; normal gameplay
still chooses and prints a random seed. Timing, effects, and temporal ray noise
are not deterministic replay. Capture screenshots outside measurement intervals.

The upgraded valley terrain is now the default: run `./build/tanks` or use
`./build/tanks --seed 7331` for a repeatable map. Use `--terrain legacy` to compare
with the previous generator, or `--terrain drained-valley` to select the new path
explicitly. The new path uses the loaded tank's dimensions to select a dry spawn and route,
then preserves that route during scenery placement. `--terrain-attempts 1..8`
explicitly bounds seed selection (default 1); logs record both requested and
selected seeds. See [terrain runtime notes](docs/TERRAIN_RUNTIME.md) for status,
replay instructions and remaining acceptance work.

The [visual target and reference board](docs/VISUAL_TARGET.md) describe the
British countryside daylight palette and show the repeatable camera views.

The refined tank is the default. For a matched comparison, run:

```bash
./build/tanks --model refined --view tank-side
./build/tanks --model original --view tank-side
```

Wheels rotate and shoes circulate independently on each side, including
reverse and pivot turns. Use `--static-tracks` for a matched static comparison.
Handling is unchanged; individual wheel suspension is not implemented. See the
[model editing notes](assets/models/README.md) for editable assets, regeneration,
material groups, and current limitations.

Run the twelve regression tests after building (requires `BUILD_TESTING=ON`,
the default): terrain surfaces, macro terrain, hydraulic erosion, foliage
LOD/wind, tree generation, tree meshes, voxel surfaces,
CPU statistics, tank surfaces, model assets, running gear and weapon effects.

```bash
ctest --test-dir build --output-on-failure
```

## Automated screenshot capture

Firing now includes a directional muzzle flame, soft expanding smoke and
45-second dry-ground scorch marks. Camera behaviour is unchanged. To inspect
the effects without keyboard/mouse automation, use the app-local preview:

```bash
./build/tanks --view tank-side --weapon-preview --screenshot screenshots/muzzle.png --screenshot-frame 91
./build/tanks --view tank-side --weapon-preview --screenshot screenshots/smoke.png --screenshot-frame 105
./build/tanks --view tank-side --weapon-preview --screenshot screenshots/scorch.png --screenshot-frame 240
```

This diagnostic mode uses a fixed 1/60 simulation step, fires once at frame 90
and spawns a ground-effect sample at frame 180. Omit `--weapon-preview` for
normal play. Scorches currently affect terrain only, not trees or rocks.

To render a frame to a PNG and exit:

```bash
./build/tanks --screenshot screenshots/example.png
```

By default the application captures frame 60, allowing temporal effects to
settle. Select another frame by placing `--screenshot-frame` after the output
option:

```bash
./build/tanks --screenshot screenshots/example.png --screenshot-frame 120
```

The destination directory is created automatically. Capture uses direct GPU
readback, so it does not depend on desktop screenshot support.

## Troubleshooting

- **No suitable device found:** update the GPU driver and confirm every Vulkan
  feature and extension listed under Hardware. Ray-query support alone is not
  enough; ray-tracing position fetch is also mandatory.
- **Validation layer unavailable:** install `vulkan-validationlayers`, or use a
  Release build, which disables validation layers.
- **CMake cannot find `glslc`:** install the distribution's `glslc` package or
  Vulkan SDK and ensure the executable is on `PATH`.
- **CMake cannot find the vcpkg toolchain:** clone and bootstrap vcpkg at the
  exact `./vcpkg` path used above, then configure again.
- **Window creation or presentation fails:** confirm a working X11/Wayland
  session and that the installed Vulkan driver supports presentation to it.
