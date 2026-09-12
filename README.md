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
  rocks, scree, clouds, and play-area boundary. Pine, ash and oak
  forms have full summer crowns, overlapping thin leaf sprays, varied density
  and coherent wind sway. Oaks use separate lobed leaves and a deep branching
  crown; CPU tree builds run concurrently while the loading screen stays active.
  Foliage uses three detail levels per bough, with direct switches and no
  blending. The far level merges foliage onto a coarse
  voxel grid and keeps only major bark branches; soft shadows retain their
  original geometry.
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
improvements. Links to `docs/*.md` throughout this README point to local
working notes (measurements, previews, raw data) that are gitignored and not
committed; regenerate them with `tools/` and the test suite. PLAN.md's
"Reference material" section keeps the conclusions and research references
that would otherwise only live there — including the
[tree-rendering guide](docs/TREE_RENDERING.md), which covers crown geometry,
threaded loading, progressive LOD, wind, shadows, measured costs and
validation.

## Tree LOD measurements

The default `--tree-lod reduced` uses three prebuilt mesh levels. Near foliage
retains full detail, middle foliage uses a 0.18-model-unit voxel grid, and far
foliage uses a 0.5-model-unit grid. Selection projects the coarse cell width
onto the screen: middle targets 1.5 pixels and far 2 pixels. Boughs have small
deterministic threshold offsets and 10% hysteresis to spread hard switches and
avoid flicker. Exactly one mesh is drawn per bough; there is no blending or
per-frame geometry generation. This is a cell-footprint heuristic, not a
measured silhouette-error bound or Nanite's specialized voxel renderer.

Bark retains whole-tree radius thresholds of 90/45 pixels. CPU tree instance
lists retain their allocated storage between frames. Coarse foliage stays
farther away when it would otherwise appear as large blocks; performance
improvement from this quality policy alone is not assumed.

Press `F8` to toggle tree LOD off/on. Off forces full-detail visible bark and
foliage and restores the original near reflection proxies. On restores the
selected LOD mode. The F3 diagnostics HUD shows the state. Use `--tree-lod full` to start
with tree LOD disabled.

Reflection trees now use the same footprint policy, conservatively measured
from the nearest point of the whole crown; off-camera trees remain eligible.
Middle/far ray meshes merge boughs into one whole-tree mesh per reusable variant.
Only meshes with fewer triangles than the original near ray proxy are selected.
Separate ray masks keep the original shadow/AO proxies unchanged, including
legacy shadow rays; reflection-only geometry cannot affect them. Coarse
reflections remain static, as the original proxies were, and need not match
individual visible bough switches exactly. All meshes and their BLASes are
built once during loading.

F9 independently disables reflected-geometry rays, retaining sky reflections.
The ray-tracing scene reuses each frame slot's TLAS when addresses, transforms,
and masks are unchanged. Moving tanks, projectiles, destruction and reflection
LOD changes trigger rebuilds. When F5 shadows/AO and F9 reflection rays are
both off, scene gathering and TLAS rebuilding are skipped entirely.

F3 reports `tree reflection triangles` (instanced geometry eligible for those
rays, not the number of triangle intersections), `TLAS rebuilds/frame`, and
visible `tree triangles/pass`. These expose actual workload changes. The extra
reflection BLASes use additional memory; when shadows/AO are active, coarse
reflection instances can add to the TLAS, so a frame-time improvement still
needs measurement. No new FPS gain is claimed.

`--tree-lod previous` retains the earlier 26/13 bough-radius thresholds,
original middle/far visual meshes, and fixed near ray proxies for comparison.
`--tree-lod far` forces lowest visible and reflection detail, while
`--tree-lod hidden` omits visible trees but retains the default reflection
policy. Shadows/AO and tree placement retain their behavior. These modes now
compare both selection policies and geometry; historical measurements below
used a different configuration.

For a controlled Release comparison:

```bash
./out/runtime-release/tanks --seed 7331 --view trees --tree-shadows soft --tree-lod-benchmark
```

The benchmark freezes wind and camera, runs previous/reduced/far/hidden and
then the reverse order, prints `TREE_LOD_RESULT` CSV rows, and exits after
`TREE_LOD_COMPLETE`. Each phase runs 360 frames, discards 60 warmup frames,
and reports the last 240 CPU samples plus asynchronous GPU exponential moving averages.
Triangle counts count foliage once,
although foliage is submitted to both depth and lighting passes. Run in an
unobstructed window without other GPU workloads. Repeat with `--view landscape`
to check a second camera. Debug/validation builds are unsuitable for timing.

The previous visible tree GPU-pass cost estimates the optimistic GPU headroom
if those passes became free while all other work stayed fixed. Hidden mode is
a practical endpoint, but changes occlusion and cannot establish a universal
FPS ceiling. These comparisons retain shadow quality. The current default also changes
reflection detail, so the historical visible-only ceiling does not establish
its total performance headroom.
With the earlier aggressive 240–200 / 180–140 bands (not the current defaults),
on the development Arc A370M at 1280×720 (seed 7331, soft shadows), paired
forward/reverse Release runs reduced tree-view frame time from 25.08 to 20.90 ms
and landscape from 22.37 to 18.22 ms: about 20–23% higher loop FPS. The default
removed 75–79% of visible tree GPU cost, leaving only 1.2–1.5 ms of those passes
to eliminate. Hidden-tree runs measured 19.01/16.65 ms respectively; shadow
work remained around 7.1 ms. These are scene-specific measurements, not a
guaranteed gain for every camera. See [local results and raw-log links](docs/TREE_LOD_LIMITS.md).

## Rendering direction

The broader goal is a more realistic-looking game using modern techniques
suited to **Intel(R) Arc(tm) A370M Graphics (DG2)** and native Linux/Mesa Vulkan
(the confirmed target GPU for shadow development). The
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

The advanced generator is now the game default: seeded British landforms,
hydraulic erosion, final drainage with substantial lakes (including
equilibrium partial lakes), a refined 513-sample final surface, generated
rock/moisture/sediment
material fields shared by shading and placement, and footprint-aware
spawn/route selection. The original quick heightmap remains available as
`--terrain legacy`. Remaining work: wider startup/driving measurements, a
memory cap, and continued material/texture art direction.
See the [terrain plan](docs/TERRAIN_EROSION_PLAN.md) for the full status.

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
| `H` | Show/hide the in-game controls panel |
| Mouse | Look around in free-camera mode |
| Arrow keys | Move horizontally in free-camera mode |
| `Space` / Left `Ctrl` | Move up / down in free-camera mode |
| `F3` | Toggle HUD diagnostics, performance summary in the window title, and detailed terminal reports |
| `F4` | Reset performance samples for a new measurement |
| `F5` | Toggle shadows and ambient occlusion together |
| `F6` | Cycle tree shadows: filtered maps → soft maps → legacy rays |
| `F7` | Toggle ambient occlusion independently while shadows are enabled |
| `F8` | Toggle tree LOD; off forces full-detail bark and foliage |
| `F9` | Toggle reflected-geometry rays; retain sky reflections |
| `F12` | Save a PNG under `screenshots/` |
| `Esc` | Quit |

The mouse cursor is captured while the application is running. In free-camera
mode, `Space` both raises the camera and fires because firing remains active.

## Combat HUD

The battle overlay uses translucent panels with live vehicle speed, travel
direction, gun elevation, and a hull-relative turret diagram. Speed uses the
simulation's world units per second. The split reticle follows the existing
unjittered gun-ray projection; it indicates direction, not a predicted hit or
penetration chance. An out-of-view notice appears when that point leaves the
screen. The camera label follows C and identifies fixed inspection views.

Shells leave the barrel at 25 world units per second and follow a gentle
ballistic arc: a level shot drops 0.15 units over 25 units of forward travel,
and 0.6 units over 50. The reticle marks the barrel direction at 25 units;
raise the gun to compensate for drop on longer shots.

The top counter tracks destroyed crates, including crates crushed by driving.
The tactical map shows the full playable boundary, remaining crate objectives,
the player's hull heading, and the gun direction. North is world +Z and east
is +X. It is an objective map, without terrain, enemy spotting or line-of-sight
simulation. Crate markers disappear when those crates are destroyed.

The gun panel describes the existing single-shot controls. Health, ammunition
inventory, reload timers and shell selection are not implemented and have no
HUD indicators. H opens controls; F3 reveals FPS and F8/F9 rendering states.
The layout scales with the window, keeping the center clear for aiming.

For a GPU-independent layout preview using the same geometry as the game:

```bash
cmake --build build --target hud_preview combat_hud_test
./build/hud_preview /tmp/combat-hud.svg
./build/hud_preview /tmp/combat-hud-help.svg 1280 720 help
ctest --test-dir build -R '^combat_hud$' --output-on-failure
```

These SVG previews use illustrative state on a plain background. In-game PNG
capture remains the check for the actual scene and Vulkan alpha blending.

## Performance measurements

The F3 HUD FPS counter counts completed frames over half-second wall-clock windows.
F8 starts a fresh window. It is separate from the longer F3 report window.
GPU timestamp readbacks run only while F3/`--profile` reporting is enabled,
and continue during its CPU-sample warmup so changing LOD does not temporarily
remove and then restore profiling work.

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
rocks, scree, and shrubs), not their separate material passes
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
accepts `tank`, `tank-side`, `tank-front`, `tank-rear`, `tank-top`, `terrain`, and `water` and holds a reference camera until `C` is
pressed. Without an explicit seed, reference views use 7331; normal gameplay
still chooses and prints a random seed. Timing, effects, and temporal ray noise
are not deterministic replay. Capture screenshots outside measurement intervals.

For matched terrain comparisons, `--view terrain` uses a fixed world camera.
`--terrain-resolution 257|513` selects the upgraded pipeline's playable grid;
257 remains the default. The 513 option is experimental and can exceed the
unchanged erosion step limit. It cannot be combined with `--terrain legacy`.
See the [resolution comparison](docs/TERRAIN_RESOLUTION.md) for results.

`--terrain-refinement 2x` (alias `on`) builds a 513-sample final surface after
the usual 257-sample erosion pass, with smoother channel banks; it is the
advanced generator's default. `--terrain-refinement off` restores the raw
257-sample surface and `4x` produces experimental 1025 final samples with a
measured rendering cost. All retain 257-sample erosion. Compare the same seed
and `--view terrain`; see the [refinement results](docs/TERRAIN_REFINEMENT.md)
and the [513/1025 test procedure](docs/TERRAIN_REFINEMENT_4X.md).

`./build/tanks --menu` opens a terrain menu before the loading screen with two
generators: the original fast heightmap and the advanced eroded terrain
(selected by default). Choose a landform and seed and click **Start Game**.
Click the landform to cycle it; click the seed to replace it, or use
**Random**. Tab/arrow keys move focus, Enter activates a control, and Escape
quits. Invalid seeds disable Start Game.
See the [menu preview](docs/terrain-menu.png).

**The menu only appears with `--menu`.** Plain `./build/tanks` starts the
advanced generator directly; `./build/tanks --terrain legacy` starts the
original quick heightmap instead (`drained-valley` is still accepted as the
advanced generator's historical name).
For normal play, prefer the optimized build:
`./out/runtime-release/tanks --seed 7331`.
The Debug build remains much slower because erosion runs at generation time.
Within the advanced generator, `--landform mixed` (the default) chooses hills,
ridges, plains, basins or a valley from the seed. Add `--landform hills` (or
`ridges`, `plain`, `basin`, `valley`) to choose a family. Landform, resolution
and refinement options require the advanced generator.
Advanced maps retain substantial lakes: at least 100 square metres of water,
with room for a 6-metre-wide circle. Narrow streams and channel carving are
disabled, and smaller pond depressions are filled before rebuilding hydrology
so they leave no hidden holes in the driving surface. Deep lake beds are also
raised to keep water within half the loaded tank's height. Rendering and
gameplay queries use the same final ground and water.
The new path uses the loaded tank's dimensions to select a dry spawn and route,
then preserves that route during scenery placement. `--terrain-attempts 1..8`
explicitly bounds seed selection (default 1); logs record both requested and
selected seeds. See [terrain runtime notes](docs/TERRAIN_RUNTIME.md) for status,
replay instructions and remaining acceptance work.
See [British landforms](docs/BRITISH_LANDFORMS.md) for shape previews, measurements
and Release build instructions. Biomes are deferred; the current palette,
lighting and vegetation remain British summertime.

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

## Tree shadow prototype

Tree sun shadows default to soft PCSS filtering of three stable 2048² depth
maps rendered from animated bark and leaf meshes. The nested light-space maps follow camera
translation in whole texels, retain off-camera casters, and blend their edges.
They reuse the existing raster LODs and wind; their visibility is filtered in
the current frame, without foliage lighting-history accumulation. Other solid
objects retain ray-traced sun shadows. Tree proxies remain in AO/reflections.

Try the tree reference camera in the optimized build:

```bash
./out/runtime-release/tanks --seed 7331 --view trees --tree-shadows soft --profile
```

Press **F6** to compare stable PCF maps, contact-hardening PCSS maps and the
previous ray/proxy experiment. Alternatively, launch with `--tree-shadows maps`, `--tree-shadows soft`, or
`--tree-shadows rays`. F6 resets performance warmup and lighting history. **F7** helps
separate AO noise from sun shadows; **F5** remains the master shadows/AO toggle.
GPU reports include `tree shadows` for map generation; map sampling cost is
included in the terrain/foreground/scenery shading stages.

For matched captures, add `--shadow-preview` (fixed 1/60 simulation steps) and
optionally `--freeze-wind`. Neither flag fires weapons. For example:

```bash
./out/runtime-release/tanks --seed 7331 --view trees --tree-shadows soft --shadow-preview --freeze-wind --screenshot screenshots/tree-soft.png --screenshot-frame 180
```

Soft PCSS maps were selected after the user compared all three modes on the
Arc A370M: they looked best, with no noticeable performance difference between
modes. This is an interactive observation, not a measured timing result.
The shadow maps add one 48 MiB depth array, shared under the existing serialized
frame submission. PCSS has a bounded filter radius; nearest-depth tree maps
approximate opaque coverage, not transmission through multiple leaf layers.
The old ray path and its history attachments remain available for comparison.
The filter reuses shared texels through texture gathers. Matched Arc A370M
captures measured 11–14% less foliage-lighting GPU time and 5–6% less total
GPU time, with at most two 8-bit colour levels of image difference outside the
HUD. See [the filter measurements](docs/FOLIAGE_FILTER_PERFORMANCE.md).
See [the redesign and validation notes](docs/SHADOW_REDESIGN.md).

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
