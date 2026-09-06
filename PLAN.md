# Project Plan

This document records the project's delivered foundations and possible
follow-up work. The progress index is the source of truth for task status;
tick an item only after its implementation has been completed and verified.
The current goal defines the selected work. Other unchecked candidates are
options rather than a committed roadmap.

## Current goal

**Hydraulic erosion terrain overhaul — planned, not yet implemented.** Replace
terrain generation and rebuild the surrounding environment as needed, preserving
the delivered tree system. The user has explicitly allowed the current terrain,
water, ground materials and non-tree scenery approach to be discarded. Tree
placement may adapt to the new world; tree geometry, density, LOD, wind and soft
shadows remain the established baseline.

The [terrain erosion plan](docs/TERRAIN_EROSION_PLAN.md) defines the architecture,
research references, replacement boundaries, budgets and acceptance criteria.
The default direction remains a temperate British landscape, with erosion,
drainage, soil/rock exposure and placement derived from shared data. Work is
planned in this order:

- [ ] Establish CPU generation contracts, triangle-consistent ground sampling,
  baseline timings and diagnostic views.
- [ ] Generate broad landforms, material resistance and soil with explicit
  drainage boundaries.
- [ ] Prototype hydraulic erosion, deposition and limited slope relaxation;
  select backend and resolution from measured quality and startup cost.
- [ ] Build final drainage, persistent water and viable spawn/routes.
- [ ] Rebuild ground materials and non-tree scenery; place existing trees.
- [ ] Integrate and validate loading, gameplay, dynamic lighting, performance
  and the final visual references.

The accepted roughly 5.3-second cold-start baseline remains the target; cache
hits must not hide a slower new-seed path. Keep generation out of normal frames
and retain the current dynamically lit procedural renderer. These tasks take
priority over the optional candidates below. Completed features remain recorded
as history, including terrain components this goal may replace.

## Progress index

### Completed foundations

- [x] [Core Vulkan renderer](#core-renderer)
- [x] [Procedural world and environment](#procedural-world-and-environment)
- [x] [Tank, weapons, and interaction](#tank-weapons-and-interaction)
- [x] [Main-gun elevation](#main-gun-elevation)
- [x] [Turret-locked aiming camera](#turret-locked-aiming-camera)
- [x] [Lighting, materials, and effects](#lighting-materials-and-effects)
- [x] [English temperate grass palette](#english-temperate-grass-palette)
- [x] [Tank movement and suspension physics](#tank-physics-checkpoint)
- [x] [Independent tread marks and track dust](#tank-physics-checkpoint)
- [x] [Existing performance foundation](#existing-performance-foundation)

### Tank and physics candidates

- [x] [Gun recoil](#gun-recoil)
- [x] [Hull-shaped collision](#hull-shaped-collision)
- [ ] [Surface-dependent traction](#surface-dependent-traction)
- [x] [Animated tracks](#animated-tracks)
- [ ] [Physics tuning tools](#physics-tuning-tools)
- [ ] [Dynamic props](#dynamic-props)
- [ ] [Full suspension and airborne simulation](#full-suspension-and-airborne-simulation)

### Performance candidates

- [x] [Extend performance instrumentation](#extend-performance-instrumentation)
- [x] [Progressive tree LOD and loading](#progressive-tree-lod-and-loading)
- [ ] [Instance decals and short-lived effects](#instance-decals-and-short-lived-effects)
- [ ] [Remove the cross-frame CPU history wait](#remove-the-cross-frame-cpu-history-wait)
- [ ] [Refit the ray-tracing TLAS](#refit-the-ray-tracing-tlas)
- [ ] [Reuse per-frame CPU scratch storage](#reuse-per-frame-cpu-scratch-storage)
- [ ] [Add scalable ray-tracing quality presets](#add-scalable-ray-tracing-quality-presets)
- [ ] [Dynamic resolution or upscaling](#dynamic-resolution-or-upscaling)
- [ ] [GPU-driven visibility and indirect drawing](#gpu-driven-visibility-and-indirect-drawing)

### Visual candidates

- [x] [Establish visual targets](#establish-visual-targets)
- [x] [Weapon firing presentation](#weapon-firing-presentation)
- [x] [Tank material detail and wear](#tank-material-detail-and-wear)
- [x] [Tank model silhouette and running gear](#tank-model-silhouette-and-running-gear)
- [x] [Foliage and environmental motion](#foliage-and-environmental-motion)
- [x] [Unified sky, sun, and atmosphere](#unified-sky-sun-and-atmosphere)
- [ ] [Shorelines and terrain transitions](#shorelines-and-terrain-transitions)
- [ ] [Particle and smoke presentation](#particle-and-smoke-presentation)
- [ ] [Camera presentation](#camera-presentation)
- [ ] [Post-processing and exposure](#post-processing-and-exposure)
- [ ] [Environmental variety and composition](#environmental-variety-and-composition)
- [ ] [HUD and interaction feedback](#hud-and-interaction-feedback)

## Developed features

### Core renderer

- Vulkan window, swapchain, resize handling, depth buffering, and MSAA.
- Dynamic rendering with shared mesh, texture, material, and push-constant
  infrastructure.
- Hardware ray-query support with BLAS/TLAS management.
- Two frames of command/synchronization resources.
- Temporal history buffers for shadow and AO accumulation.
- GPU screenshot capture and a lightweight HUD with FPS display.
- Per-stage GPU timestamp reporting.

### Procedural world and environment

- Generated heightmap terrain with hills, plateau features, valleys, and a
  traced river route.
- Connected low-basin water generation with varied water levels.
- A terrain-following play-area boundary and translucent energy wall.
- Procedurally generated and terrain-aware trees, shrubs, boulders,
  sedimentary cliffs, and decorative scree.
- Tree bark and pine/ash canopies use rounded voxel boundaries with smooth normals
  across texture seams. Surface vertices are shared without adding triangles;
  existing dynamic ray-query lighting and coarse occluder LODs remain in use.
  Generation skips unused materials and noise outside each boundary shell,
  with loading progress presented while tree variants build on up to three
  CPU workers. Geometry arrays remain private to each task; uploads and
  Vulkan command-pool/queue use stay on the main thread. Outstanding work
  is bounded to limit both CPU contention and waiting mesh memory.
- Pine, ash and oak each have two full summer crown variants. A shared generated
  skeleton drives bark, thin twigs, flattened ragged leaf sprays and every
  LOD. Crown density varies both between variants and across branches;
  thinning leaves the branch structure and surviving sprays in place. Leafy
  side shoots fill the boughs and interior crown, with overlapping small fans
  based on summer reference photographs rather than isolated terminal tufts.
  Sub-pixel petiole meshes are omitted and spatial hashing avoids repeated
  coordinate collisions as crown occupancy grows.
- Oak has a deep, rounded crown with large boughs staggered along the trunk,
  visible scaffold gaps and dense terminal shoots. Small, separately shaded
  lobed leaf surfaces preserve leaf edges instead of merging into a solid
  voxel mass. Coarser LODs simplify these leaves within each spray.
- Foliage uses progressive per-bough LOD, complementary coverage masks and
  a masked depth pass before lighting. Indexed ranges batch by variant,
  with direct draws available when optional multi-draw features are absent.
  Bark retains discrete LOD. See the [tree-rendering guide](docs/TREE_RENDERING.md)
  for thresholds, geometry, wind, shadow behaviour, validation and limits.
- Canopy ray proxies use inset triangles per leaf or voxel spray and are flattened
  to follow the visible foliage. Near-grid occupancy checks keep each proxy
  inside its voxel leaf group; oak proxies fit inside the thin leaf surfaces.
  Medium/far raster LODs engage at smaller screen sizes
  where individual leaves cannot be resolved.
  Foliage sun shadows retain a wide sampling cone and partial transmission,
  while solid occluders retain their narrow cone. AO and reflections query
  both geometry groups. Coarse raster LODs preserve the large crown gaps.
- Placement rules that account for water, spawn clearance, spacing, slope,
  scale, and reusable visual variants.

### Tank, weapons, and interaction

- Imported multipart tank model with separately rendered hull, tracks,
  turret, and barrel materials.
- Player driving, independent turret traverse, main-gun elevation, hull-follow
  camera, turret-locked aiming camera, and free camera.
- Gun recoil with a damped barrel return and chassis impulse.
- Hull-aligned capsule collision with stable obstacle sliding and
  orientation-aware play-area boundaries.
- Muzzle-accurate projectile spawning with swept collision tests against
  boxes, trees, and rocks, plus terrain impact detection.
- Destructible target boxes, impact flashes, dynamic explosion lights, debris,
  embers, muzzle smoke, and shell trails.
- World-space crosshair projection and basic target/FPS HUD feedback.

### Lighting, materials, and effects

- Procedural grass, gravel, rock, bark, foliage, camouflage, metal, crate,
  cloud, boundary, and tread textures.
- Terrain material patching, height/slope-dependent grass-to-rock transitions,
  domain-warped sampling, and luminance-derived bump detail.
- Ray-traced soft sun shadows, contact AO, and selected environment
  reflections with temporal stabilization.
- Material-specific foliage transmission, stone response, tank highlights,
  ACES-style tonemapping, and distance fog.
- Procedural cloud sky, related analytic reflection sky, and depth-aware
  water with Fresnel response and animated normal ripples.
- Independent tread decals and speed/slip/contact-driven dust effects.

### English temperate grass palette

- Four consistently green procedural variants: wet pasture, mixed meadow,
  shaded blue-green growth, and fresh spring grass.
- Random per-run pairing retains natural variation without selecting dry or
  straw-dominated terrain.

### Main-gun elevation

- `R` raises and `F` lowers the barrel around a geometry-derived trunnion.
- Depression is limited to −10° and elevation to +20°, matching the published
  Challenger 2 main-armament envelope.
- The barrel mesh, muzzle position, projectile direction, crosshair, recoil,
  and ray-tracing instance all share the elevated transform.
- The camera-mode toggle moved from `F` to `C` to avoid an input conflict.

### Turret-locked aiming camera

- `C` cycles through hull-follow, turret-aiming, and free-camera modes.
- The aiming view orbits with turret traverse and looks along the elevated
  main-gun aim line, keeping the projected crosshair centered.
- Its closer offset retains a view of the tank while prioritizing accurate
  target alignment over driving visibility.

## Tank physics checkpoint

The current tank movement is a solid foundation and is a reasonable place to
stop pending wider playtesting. It currently includes:

- Fixed-step movement simulation.
- Momentum, acceleration, braking, drag, and rolling resistance.
- Differential-track steering and stationary pivot turns.
- Forward/reverse speed differences and speed-dependent steering.
- Terrain slope forces, lateral traction, and a maximum climbing angle.
- Increased rolling resistance and a low-speed static-friction hold that
  prevents stationary creep on slopes up to 15 degrees.
- Velocity-aware obstacle sliding and stable boundary collisions.
- Four-contact visual suspension with pitch, roll, and heave damping.
- Acceleration squat, braking dive, and restrained cornering lean.
- Independent left/right tread marks and track dust.
- Mark and dust intensity driven by acceleration, slip, turning, and contact.

Further movement work should be driven by a specific issue found during
playtesting rather than added for completeness.

## Candidate improvements

### Gun recoil

The barrel now snaps backward when firing and returns to rest with a damped
spring. Each shot also gives the terrain-constrained chassis a small opposite
velocity impulse. The follow camera deliberately remains stable because a
camera displacement made the shot feel like a zoom rather than physical recoil.

- Value: high visual and tactile payoff.
- Complexity: low to medium.
- Status: completed.

### Hull-shaped collision

The tank now uses a rounded, hull-aligned capsule instead of a single circular
footprint. It resolves against existing tree and rock proxies while preserving
tangential velocity for smooth scraping and sliding. Boundary clearance also
accounts for the capsule's changing X/Z extent as the tank rotates. Static
obstacle proxies now stay inside visible geometry: trees use their trunk base,
rocks use each procedural variant's silhouette, and cliffs use the actual
terrain-exposed footprint of each broken stone plate.

- Value: high if current obstacle contacts feel awkward; otherwise modest.
- Complexity: medium.
- Status: completed.

### Surface-dependent traction

Expose the terrain's visible surface classification to gameplay so grass,
gravel, rock, mud, or wet ground can affect acceleration, braking, lateral
grip, tread darkness, and dust production.

- Value: adds handling variety and connects physics to the environment.
- Complexity: medium.
- Suggested priority: useful when the terrain types become meaningful to play.

### Animated tracks

Track shoes now circulate around paths extracted from the authored belts;
road wheels, hubs, idlers and sprockets rotate from each side's signed travel.
The post-collision longitudinal velocity and yaw rate drive animation at the
existing fixed simulation step, handling reverse, coasting and opposing pivot
motion without changing handling. Lateral sliding does not spin the wheels.
Five shared instanced draws render the moving parts; the same 160 transforms
drive ray-traced geometry. The original model remains rigid. `--static-tracks`
restores the static refined model for matched visual/performance comparisons.

- Value: visual polish most noticeable near the tank.
- Complexity: medium, depending on the model's UV and mesh layout.
- Status: completed for distance-driven rigid wheels and shoes. Individual
  suspension articulation, track sag and powered wheelspin remain separate work.

### Physics tuning tools

Move handling and suspension constants into a configuration structure or a
small development UI. Useful parameters include engine acceleration, braking,
drag, grip, yaw response, suspension frequency/damping, and dust intensity.

- Value: speeds up repeated feel-tuning and comparison of handling presets.
- Complexity: low to medium.
- Suggested priority: worthwhile if movement tuning continues frequently.

### Dynamic props

Allow the tank to push crates, debris, or other lightweight objects. This would
require object mass, velocity, collision response, sleep behavior, and likely a
broader rigid-body architecture or a physics library.

- Value: stronger environmental interaction.
- Complexity: high.
- Suggested priority: only when dynamic-object gameplay is planned.

### Full suspension and airborne simulation

Add vertical velocity, force-based individual track contacts, jumping, landing
impulses, loss of traction while airborne, and possible rollover behavior.

- Value: supports more extreme terrain and simulation-heavy handling.
- Complexity: very high, with significant stability and tuning risk.
- Suggested priority: defer unless jumping or rollover becomes a gameplay goal.

## Tank physics recommendation

Pause further tank movement work and evaluate it during normal gameplay. If no
specific handling or collision problem emerges, gun recoil is the strongest
next improvement because it adds impact without reopening the movement
architecture.

## Performance improvements

### Existing performance foundation

Performance work should begin with measurements from representative gameplay,
then target the largest observed cost. The renderer already has a useful
foundation:

- GPU timestamps split each frame into TLAS, terrain, foreground, scenery,
  effects, and HUD regions without stalling the active submission.
- Trees, rocks, shrubs, scree, and cliffs are CPU-frustum-culled and submitted
  in instanced mesh/material groups.
- Tree bark and rocks use three projected-size LODs with hysteresis;
  foliage uses progressive per-bough selection and batched indexed draws.
- Ray-tracing geometry uses simplified proxies where appropriate, and
  numerous minor effects are excluded from the TLAS.
- Shadow and AO ray counts decrease with distance and use temporal
  accumulation to recover quality.
- Two frame-in-flight resource slots are already available.

### Progressive tree LOD and loading

Tree generation and meshing now run on up to three CPU workers while loading
progress remains visible; Vulkan uploads and acceleration-structure work stay
on the main thread. Foliage detail is selected separately per bough and blends
adjacent levels through limited screen-size bands. A masked depth pass resolves
coverage before lighting, and variant batches use multi-draw indirect with a
direct indexed fallback. Current and previous wind vectors are evaluated once
per placement and shared by bark and foliage.

The matched Arc A370M Debug check at 1280×720 retained roughly 5.3-second
startup. GPU time fell from 25.83 to 23.96 ms, while mean frame time stayed
near 31 ms because CPU visibility/grouping work increased. All nine tests
passed, final GPU validation was clean, and 31 diagnostic scene captures
matched between direct and multi-draw paths. See the
[measurement conditions and results](docs/TREE_RENDERING.md#recorded-validation-and-performance).

- Status: completed for CPU-selected progressive foliage LOD and bounded
  parallel tree builds.
- Limits: all three foliage levels remain resident, bark still switches
  discretely, colour dithering can remain visible, and ray proxies stay static.
  GPU cluster selection, geometric-error LOD and streaming remain future work.

### Extend performance instrumentation

CPU timings now cover simulation, visibility/grouping and upload, TLAS instance
gathering, command recording, submission, both fence waits, acquisition, and
presentation. A 240-frame rolling window reports mean, p95, p99, worst frame,
actual draw calls, visible scenery instances, and TLAS instance counts alongside
the existing asynchronous GPU timings. F3 (or `--profile`) enables window-title
and terminal reports; F4 resets the sample window. Startup warmup, screenshot
capture, and resize handling keep special frames out of ordinary measurements.
The README describes stationary, scenery, and driving/effects checks. Fixed
seeds and reference cameras now support static cross-launch comparisons;
scripted gameplay replay remains future work.

- Value: identifies whether the next limit is CPU submission, ray queries,
  acceleration-structure work, or ordinary rasterization.
- Complexity: low.
- Status: completed. Use these measurements before selecting a larger optimization.

### Instance decals and short-lived effects

Track marks currently issue as many as 256 individual draw calls. Boxes,
shells, smoke/dust puffs, debris, and flashes also contain groups sharing the
same mesh and material. Extend instance data beyond transforms to include
opacity and effect-specific parameters, then draw each compatible group in a
small number of calls.

- Value: potentially large reduction in CPU command-recording and driver
  overhead, especially after sustained driving or explosions.
- Complexity: medium.
- Suggested priority: strongest general-purpose next optimization if CPU or
  effects timing becomes significant.

### Remove the cross-frame CPU history wait

The temporal-history ping-pong currently waits for both frame fences before
recording the next frame. Investigate expressing the history dependency with
correct GPU-side ordering and image barriers, or redesigning the history ring
so the CPU can remain ahead without racing a previous read.

- Value: may restore meaningful CPU/GPU overlap and make the existing two
  frames-in-flight effective.
- Complexity: medium to high; synchronization errors can cause subtle temporal
  noise or validation failures.
- Suggested priority: profile fence-wait time first, then change only with
  validation layers enabled and stable-image comparisons.

### Refit the ray-tracing TLAS

The scene TLAS is fully rebuilt every frame even though most instances and
their transforms are static. Use a stable fixed-slot instance layout for
terrain, scenery, tank parts, boxes, and shells, marking inactive dynamic slots
with masks. Build with update support and use TLAS refit/update when only
transforms or masks change.

- Value: reduces acceleration-structure cost when TLAS timing is material.
- Complexity: medium to high; requires stable instance counts and careful
  capacity management.
- Suggested priority: pursue only if the existing TLAS timestamp is a notable
  part of the frame budget.

### Reuse per-frame CPU scratch storage

Visibility grouping and ray-tracing instance gathering currently construct
several vectors each frame. Store these as reusable frame scratch buffers,
reserve known capacities once, and clear without releasing their allocations.
Cache the static portion of the TLAS instance list and update only dynamic
entries.

- Value: reduces allocation churn and improves CPU frame-time consistency.
- Complexity: low to medium.
- Suggested priority: a safe cleanup after CPU profiling confirms measurable
  command-preparation cost.

### Add scalable ray-tracing quality presets

Expose shadow, AO, and reflection distance/sample budgets as quality settings.
Possible extensions include reducing rays during fast camera movement,
disabling distant AO earlier, limiting water reflections by projected area,
or tracing expensive terms at a reduced resolution before temporal recovery.

- Value: provides a direct GPU performance/quality tradeoff across different
  hardware, especially the target integrated GPU.
- Complexity: low for presets; high for reduced-resolution ray-query passes.
- Suggested priority: use when fragment/ray-query time dominates the GPU
  measurements.

### Dynamic resolution or upscaling

Render the 3D scene below native resolution when GPU time exceeds a target,
then upscale before the HUD. Temporal upscaling would require motion vectors
and more robust history rejection; simple spatial upscaling is easier but
produces a softer image.

- Value: broad GPU relief when fill rate and per-pixel ray queries dominate.
- Complexity: medium for spatial scaling, very high for temporal upscaling.
- Suggested priority: defer until quality presets are insufficient.

### GPU-driven visibility and indirect drawing

Move large-scale visibility selection and draw generation to compute shaders
using indirect draw commands. This becomes useful if object counts grow far
beyond the current scene. Foliage already uses CPU-generated multi-draw
indirect batches; culling and LOD selection remain on the CPU. The completed
bough LOD work does not implement compute-driven visibility or a GPU cluster
hierarchy.

- Value: scales to much denser environments.
- Complexity: high.
- Suggested priority: defer unless profiling shows CPU visibility/submission
  becoming a bottleneck as scene density grows.

### Performance recommendation

For the active terrain overhaul, measure CPU generation stages, derived fields,
meshing, uploads and acceleration-structure builds before choosing erosion
resolution, iteration count or a GPU backend. Coordinate terrain and existing
tree jobs under one worker budget and report cold-cache startup independently.
Use the existing profiler for runtime terrain/scenery costs; erosion itself
should contribute no per-frame simulation work.

The [terrain plan's budgets](docs/TERRAIN_EROSION_PLAN.md#budgets-and-acceptance)
are the acceptance gate. Effect instancing, frame-history synchronisation and
TLAS changes remain separate candidates when measured cost justifies them.

## Visual improvements

The current presentation already includes procedural terrain and materials,
clouds and distance fog, ACES-style tonemapping, ray-traced soft shadows/AO and
selected reflections, depth-aware reflective water, dynamic muzzle/explosion
lighting, debris, smoke, dust, and independent tread decals. Future work should
strengthen a chosen art direction and improve motion/readability rather than
add effects indiscriminately.

### Establish visual targets

The chosen direction is grounded British countryside daylight: cool sky fill,
slightly warm sunlight, English greens, broken cloud, and pale distance haze.
[Visual target and in-game reference board](docs/VISUAL_TARGET.md) record the
pillars and repeatable tank, landscape, and water/rock views. `--seed` and
`--view` reproduce static scenes for future material and lighting comparisons.

- Value: keeps otherwise-good effects visually coherent and prevents endless
  local tweaking.
- Complexity: low.
- Status: completed; use the reference views for future visual changes.

### Weapon firing presentation

The existing recoil, spring return and chassis kick now have a brief directional
muzzle flame with a small face-on core, followed by a soft expanding smoke burst.
Smoke slows from a forward jet into a rising cloud; back-to-front transparent
cards test against the scene without overwriting depth or shadow/AO history.
Camera movement is unchanged.

Dry terrain hits leave irregular scorch patches blended directly into the
terrain material. They follow slopes, stay for 45 seconds and fade over the last
12 seconds. At most 16 patches, 4 flashes and 48 muzzle-smoke cards are retained.
Terrain marks add no draws or RT instances. Tree/rock surface marks, craters,
depth-softened particle intersections and broader explosion/trail improvements
remain separate work.

- Value: high; firing is a frequent focal action and currently offers the
  clearest opportunity for stronger visual feedback.
- Complexity: low to medium for recoil, medium for persistent impact decals.
- Status: completed. `--weapon-preview` triggers an app-local shot at frame 90
  and a ground-effect sample at frame 180, with a fixed 1/60 simulation step;
  it needs no synthetic keyboard/mouse input. See the visual target document
  for capture commands. Existing shell physics and firing controls are retained.

### Tank material detail and wear

Painted armour, tracks, and the barrel now have separate material responses.
Convex mesh edges receive restrained, broad scuffs that fade away when too
small to resolve on screen; flat triangulation seams and concave corners do
not. Model-space dust masks tint and dull the lower
hull/tracks, and the muzzle has a localized soot band. Roughness and highlight
strength vary with wear, dust, and soot while retaining readable camouflage.
This is a static weathering pass; driving-dependent dirt/mud buildup remains
an optional follow-up. See the updated [tank reference](docs/VISUAL_TARGET.md#tank-material-checkpoint).

- Value: improves the main object at every camera distance where detail is
  visible.
- Complexity: medium; procedural masks can avoid requiring a full new asset
  pipeline.
- Status: completed for the initial material and static-weathering pass.

### Tank model silhouette and running gear

The default is now an editable Challenger 2 OBJ, revised against the supplied
side profile: long low turret, sloped cheeks/glacis, deep segmented skirts,
six road wheels per side, hubs, raised end wheels, hollow track belts, tread
shoes, roof fittings, rear drums and a proportioned sleeved barrel
with an open muzzle. The original `tank.x` is preserved via `--model original`.
The asset is generated offline, not constructed every frame. Named objects
merge into five base rendering parts plus five instanced running-gear batches, retaining the current turret,
elevation, recoil, material and capsule-collision systems.

- Status: static geometry and side/front/rear/top reference passes completed.
  Broad cheek armour, launchers, rear grilles/drums, mud flaps, asymmetric
  roof fittings and split engine grilles are modelled. Small details and
  equipment-fit differences remain approximations, not an exact replica.
- Editing and limitations: [model asset notes](assets/models/README.md).
- Running-gear animation is now covered by [animated tracks](#animated-tracks).
  Articulated suspension remains optional future work.

### Foliage and environmental motion

Bark, foliage and shrubs now share coherent low-frequency wind sway. The
remaining options are smaller independent leaf motion and aligning dust and
cloud motion with the same weather direction. Terrain has no grass-blade
geometry to animate.

- Value: makes an otherwise-static landscape feel alive and improves motion
  cues when the tank is stationary.
- Complexity: medium.
- Suggested priority: refine only when remaining environmental stillness is
  noticeable without disrupting the accepted crown shape or load time.
- Status: bark, leaves, and shrubs bend coherently from anchored roots,
  using one low-frequency wind vector per instance and quadratic trunk
  bending. Current and previous wind vectors are now evaluated once per
  placement on the CPU and shared by bark and all foliage boughs, avoiding
  repeated trigonometry in each vertex invocation. Elapsed time drives the
  motion independently of frame rate;
  there is no travelling wave across the solid canopy. Bent normals and
  previous bent positions keep lighting and temporal reprojection aligned.
  Ray queries retain static coarse proxies to avoid rebuilding tree geometry.
  Bark has its own opaque material; only leaves use transmission. Terrain
  has no blade geometry to move and stays static. Individual leaf motion
  and dust/cloud wind alignment remain future work.

### Unified sky, sun, and atmosphere

The visible sky and reflection misses now use one directional sky function and
cloud-density texture, with a small sun disk aligned to the direct light.
Shared daylight colours control sky, horizon haze, ambient fill, direct light,
cloud shading, and reflection hits. Fog uses the cloud-free horizon gradient.
This is a fixed daylight preset; moving cloud shadows and time-of-day animation
remain optional follow-ups.

- Value: improves scene-wide cohesion, particularly in water reflections and
  at the horizon.
- Complexity: medium; dynamic time of day and cloud shadows increase it.
- Status: completed for the fixed British daylight preset.

### Shorelines and terrain transitions

This is now part of the active
[terrain erosion overhaul](docs/TERRAIN_EROSION_PLAN.md), particularly final
hydrology and the ground-material rebuild. Derive water/land contact and wetness
from the finished terrain and persistent water, replacing the current absolute
height rules. The following visual aims remain useful within that work.

Improve contact between water and land with a wet shoreline band, subtle foam
or ripple breakup, and terrain darkening near the water level. Further terrain
work could add slope-aware texture scale variation, local colour patches,
wheel-rut displacement cues, and softer blending between grass and exposed
stone.

- Value: removes visible material boundaries and makes generated terrain feel
  less synthetic.
- Complexity: medium.
- Suggested priority: address where screenshots reveal obvious transition
  lines or repetitive ground patterns.

### Particle and smoke presentation

Replace or supplement blob-cluster smoke with camera-facing soft particles,
depth-aware fading, colour evolution, and turbulence. Let debris collide with
the terrain, and vary dust by surface type and moisture. Keep effect lifetimes
and density bounded so added richness does not obscure targets or overwhelm
the renderer.

- Value: improves explosions, muzzle blasts, shell trails, and tank motion.
- Complexity: medium to high, depending on soft-particle and batching support.
- Suggested priority: pair with effect instancing from the performance list.

### Camera presentation

Add subtle spring lag during acceleration and turning, collision avoidance
against terrain/scenery, a small speed-dependent field-of-view change, and
carefully limited impulses for firing, impacts, and hard landings. Provide
strength controls to avoid motion discomfort.

- Value: makes existing physics feel more substantial without changing the
  simulation.
- Complexity: low to medium.
- Suggested priority: combine with weapon recoil, then tune conservatively.

### Post-processing and exposure

Add a modest bloom pass for the boundary wall, muzzle flash, sparks, and bright
water highlights. Consider configurable colour grading and exposure controls;
automatic exposure is only worthwhile if lighting ranges or time of day vary
substantially. Avoid heavy vignette, chromatic aberration, or motion blur unless
the chosen art direction explicitly calls for them.

- Value: can unify the final image and improve bright-effect readability.
- Complexity: medium.
- Suggested priority: late polish, after lighting and materials are stable.

### Environmental variety and composition

Reworking rocks, cliffs, scree, shrubs and their placement is in scope for the
active [terrain overhaul](docs/TERRAIN_EROSION_PLAN.md). Existing tree assets and
rendering remain intact, with placement driven by the new terrain. Additional
landmark families below remain optional beyond that replacement.

Add a small number of distinctive landmarks and prop families—fallen trees,
stumps, ruined structures, grass clumps, flowers, or track-side clutter—placed
according to terrain and water context. Prefer a few readable silhouettes and
intentional focal areas over uniformly increasing object density.

- Value: improves navigation, composition, and replay-to-replay identity.
- Complexity: medium to high because it includes asset creation and placement
  rules.
- Suggested priority: when expanding the space beyond a renderer/handling
  showcase into a fuller game environment.

### HUD and interaction feedback

Refine the crosshair, target/hit confirmation, reload or fire-state feedback,
and control prompts. Maintain a clean separation between development metrics
and player-facing UI, with scaling that remains legible at different window
sizes.

- Value: improves readability and makes existing interactions feel complete.
- Complexity: low to medium.
- Suggested priority: when gameplay rules and weapon timing become more
  defined.

### Visual recommendation

The next visual goal is the hydraulic erosion terrain overhaul. Establish
convincing landforms, drainage and deposition in neutral shading before
rebuilding ground materials and non-tree props. Treat the older landscape
captures as comparison history, while preserving the accepted tree fidelity.
Ground colours, water, cliffs and rocks may all change to support the new
terrain. Keep tank readability, viable driving routes and dynamic lighting as
cross-cutting checks. Broader particle, camera and post-processing work remains
secondary to this goal.
