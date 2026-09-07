# Project Plan

This document records the project's delivered foundations and possible
follow-up work. The progress index is the source of truth for task status;
tick an item only after its implementation has been completed and verified.
The current goal defines the selected work. Other unchecked candidates are
options rather than a committed roadmap.

## Overall direction and priorities

Build a more realistic-looking game with modern, cost-effective techniques on
Intel Arc A370M, Linux/Mesa Vulkan at 1280×720. The
[rendering roadmap](docs/RENDERING_ROADMAP.md) defines technique choices,
dependencies, hardware checks and evidence required before enabling them.
Hydraulic erosion is the immediate goal; it is part of a broader move toward
coherent materials, lighting, geometry and motion rather than isolated polish.

- **P0 — active:** terrain generation, hydrology, ground sampling and rebuilding
  the non-tree environment around shared physical fields.
- **P1 — foundations:** consistent physically based materials; linear HDR and
  native temporal AA; measured ray budgets; resource ownership and selected
  batching; grounded, surface-aware effects. Material foundations join the
  terrain material stage; the erosion prototype does not wait for all of them.
- **P2 — measured follow-ups:** temporal upscaling, terrain LOD, near grass,
  GPU visibility, selective indirect lighting/reflection filtering, modest
  atmosphere upgrades and visual suspension articulation.
- **P3 — deferred:** simulation-heavy gameplay, general rigid bodies, new HUD
  features and expensive rendering research without a demonstrated use case.

New rendering work is planned, not delivered. The existing tree system remains
protected. Establish a representative Release baseline before setting default
quality; 60 Hz is a working aspiration, not a result inferred from the old
Debug timings. Keep the roughly 5.3-second startup target and budget memory,
CPU work, GPU work and temporal quality together. Completed entries describe
initial delivered passes, not a ceiling on future visual fidelity.

## Current goal

**Hydraulic erosion terrain overhaul — CPU erosion, channel carving and combined
stream/lake geometry and queries implemented; water quality, playability and
runtime integration pending.** Replace
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

Current scope is **British summertime**. Broader landforms are implemented;
lower new-map generation time and terrain/water quality remain the priorities.
Biome distribution and climate fields are deferred until the user returns to
them; they are not prerequisites for this work.

- [x] Replace the mandatory valley composition with seeded hills, ridges,
  plains and basins, including configurable domain warping and a mixed default
  ([implementation, previews and checks](docs/BRITISH_LANDFORMS.md)).
- [x] Reuse donor calculations in the erosion solver without changing generated
  fields or scratch-memory size; median paired CPU generation time fell 12.4%
  across 26 comparisons ([evidence](docs/EROSION_PERFORMANCE.md)).
- [ ] Compare shorter/coarser erosion against the current visual reference;
  measure quality and startup cost before choosing further CPU/GPU work.
- [ ] Deferred: add regional temperature/precipitation fields and blended
  Whittaker-style biome classification, shared by materials and vegetation
  ([design and rollout](docs/TERRAIN_EROSION_PLAN.md#climate-and-biome-distribution)).

- [x] Separate CPU terrain generation/meshing from Vulkan upload and match
  ground sampling to mesh triangles; add fixtures and 16 fixed regression seeds.
- [x] Add a standalone terrain probe, neutral exports and a measured CPU
  baseline ([results and commands](docs/TERRAIN_BASELINE.md)).
- [ ] Finish Stage 0: expand target-hardware Release measurements to driving
  and sustained effects, establish/enforce a memory cap and complete in-engine
  gameplay checks. The initial stationary Release baseline is recorded below.
- [x] Generate broad landforms, material resistance and soil with explicit
  drainage boundaries in the selectable rolling-valley CPU prototype
  ([preview, tests and timings](docs/TERRAIN_MACRO.md)). No game default change;
  full visual/playability acceptance remains pending.
- [x] Prototype hydraulic erosion, conservative sediment transport/deposition,
  limited soil relaxation and deterministic 1–4-worker CPU passes
  ([results and limitations](docs/TERRAIN_EROSION_PROTOTYPE.md)).
- [ ] Finish Stage 2 quality/performance gates: broaden startup coverage and
  complete grid-bias/convergence checks. Initial integrated Release runs fit the
  startup target with the current four-worker CPU backend.
- [x] Settle remaining sediment, account for temporary water removal, and
  analyse final drainage, basin outlets and potential runoff over the full apron
  ([CPU diagnostics and checks](docs/TERRAIN_DRAINAGE.md)).
- [x] Add supplied spill-level lakes, conservative basin losses/overflow,
  triangle-clipped meshes and shared lake/shoreline queries
  ([prototype and limits](docs/TERRAIN_LAKES.md)).
- [x] Select streams from resolved runoff, split reaches at confluences, anchor
  downstream profiles to lakes and report channel-depth shortfalls
  ([stream design guides](docs/TERRAIN_STREAMS.md)).
- [x] Survey exact channel cross-sections over the full apron, distinguish real
  banks from search/domain limits, and record zero-depth spill controls
  ([bank surveys](docs/TERRAIN_STREAM_SECTIONS.md)).
- [x] Add bounded soil-first channel excavation with material export accounting,
  protected lake rims, and fresh drainage/water/contact/render results after edits
  ([channel carving](docs/TERRAIN_CHANNEL_CARVING.md)).
- [x] Build a combined terrain-clipped stream/lake mesh and shared height,
  depth, flow and shoreline queries using downstream catchments
  ([combined water prototype](docs/TERRAIN_WATER.md)).
- [x] Accelerate full-apron shoreline queries with an immutable spatial index,
  preserving exact distances and exported geometry
  ([parity checks and query benchmark](docs/TERRAIN_SHORELINE.md)).
- [x] Select dry spawn candidates and connected routes from final water/ground,
  conservatively accounting for the complete tank footprint, face slopes,
  boundary and supplied obstacle circles
  ([static playability checks](docs/TERRAIN_PLAYABILITY.md)).
- [x] Add bounded deterministic terrain selection with explicit acceptance,
  attempt diagnostics, cancellation boundaries and accounting for rejected work
  ([selection policy and tests](docs/TERRAIN_SELECTION.md)).
- [ ] Finish channel/bank/confluence shape, positive-depth spill connections
  and partial-lake policy; accept the combined water geometry and its cost.
- [x] Connect selectable valley generation to responsive loading, retain final
  water/navigation, use the selected spawn and reserve/recheck routes through
  scenery placement while preserving 100 trees
  ([runtime integration](docs/TERRAIN_RUNTIME.md)).
- [x] Make valley terrain the in-game default after the landscape review, with
  `--terrain legacy` retained for comparison during further refinement.
- [x] Measure Release startup/frame/memory costs against legacy across three
  seeds, add a repeatable runtime benchmark and isolate scenery GPU phases
  ([results and remaining validation](docs/TERRAIN_RUNTIME_PERFORMANCE.md)).
- [x] Specialize the foliage lighting shader without reducing tree fidelity;
  validate matched captures and record the initial modest GPU improvement
  ([measurements and comparison limits](docs/FOLIAGE_LIGHTING.md)).
- [ ] Validate actual driving and complete Stage 3 water/playability acceptance.
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
- [ ] [Visual suspension articulation](#visual-suspension-articulation)
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
- [ ] [Terrain chunk culling and LOD](#terrain-chunk-culling-and-lod)
- [ ] [GPU-driven visibility and indirect drawing](#gpu-driven-visibility-and-indirect-drawing)

### Visual candidates

- [x] [Establish visual targets](#establish-visual-targets)
- [x] [Weapon firing presentation](#weapon-firing-presentation)
- [x] [Tank material detail and wear](#tank-material-detail-and-wear)
- [x] [Tank model silhouette and running gear](#tank-model-silhouette-and-running-gear)
- [x] [Foliage and environmental motion](#foliage-and-environmental-motion)
- [x] [Unified sky, sun, and atmosphere](#unified-sky-sun-and-atmosphere)
- [ ] [Physically based material foundation](#physically-based-material-foundation)
- [ ] [Linear HDR and temporal image stability](#linear-hdr-and-temporal-image-stability)
- [ ] [Selective advanced lighting and atmosphere](#selective-advanced-lighting-and-atmosphere)
- [ ] [Near-field ground vegetation](#near-field-ground-vegetation)
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
  and decorative scree. The old sedimentary cliff props have been removed.
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
rocks use each procedural variant's silhouette.

- Value: high if current obstacle contacts feel awkward; otherwise modest.
- Complexity: medium.
- Status: completed.

### Surface-dependent traction

Use final terrain surface/moisture fields for dust, tread marks and ground
contact presentation during the environment rebuild. Grip, acceleration and
braking changes are a separate handling follow-up after the new landscape is
playtested; retain the settled handling while evaluating visual realism.

- Priority: P1 for surface-driven presentation; P2 for measured handling needs.
- Dependency: authoritative terrain/material classifications and viable routes.
- Acceptance: visible surface and effects agree; no accidental handling changes.

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

Keep terrain diagnostics and generation controls in the active terrain work.
A broader handling/suspension tuning UI remains optional. Add it when repeated
playtesting exposes a specific need, rather than reopening physics for realism.

- Priority: P3; minimal diagnostic controls may accompany a concrete fix.
- Acceptance: tuning is reproducible and does not alter the default preset.

### Dynamic props

Pushable crates and general rigid-body objects remain gameplay work. Prefer
bounded, terrain-colliding cosmetic debris for the effects pass before adding
mass, sleep, stacking and broad-phase physics to every prop.

- Priority: P3; promote only for an interaction requirement.
- Cost gate: simulation, collision and TLAS update load during sustained play.

### Visual suspension articulation

Evaluate per-wheel visual contact and limited track-path adaptation on the new
terrain while preserving the existing hull simulation and named rig. Bound
wheel travel, avoid belt/shoe intersections and keep raster/ray transforms
consistent. Do not simulate individual tread shoes as rigid bodies.

- Priority: P2 after terrain contact is correct and a visible gap is documented.
- Dependency: terrain triangle sampling and the current running-gear contract.
- Acceptance: side views and driving over banks improve without altering handling.

### Full suspension and airborne simulation

Vertical dynamics, force-based track contacts, jumping, landing and rollover
remain deferred. They are not required to show plausible wheel contact on the
eroded ground; visual articulation is a separate, smaller candidate.

- Priority: P3; promote for gameplay, not as a graphics prerequisite.
- Acceptance: established driving stability, slope behaviour and collisions
  survive a dedicated physics test programme.

## Tank physics recommendation

Keep established tank handling while integrating the eroded surface and viable
spawn/routes. Recoil and animated tracks are already delivered. Prioritise
correct contact and surface-driven dust/marks, then consider visual wheel
articulation if the new terrain exposes a visible weakness. Broader handling
and rigid-body work remains secondary to environment and rendering realism.

## Performance improvements

### Existing performance foundation

Performance work should begin with measurements from representative gameplay,
then target the largest observed cost. The renderer already has a useful
foundation:

- GPU timestamps split each frame into TLAS, terrain, foreground, scenery,
  effects, and HUD regions without stalling the active submission.
- Trees, rocks, shrubs, and scree are CPU-frustum-culled and submitted
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

Batch shared meshes with per-instance opacity and effect parameters where
recording/draw overhead is measurable. Track marks can currently contribute up
to 256 draws. Preserve depth/history rules and transparent ordering; a single
unsorted batch is not a valid replacement for sorted smoke.

- Priority: P1 when sustained driving/effects profiles identify the cost.
- Acceptance: appearance and ordering match, with lower CPU time at realistic
  effect counts. Measure GPU overdraw independently of API draw counts.

### Remove the cross-frame CPU history wait

Move temporal dependencies onto correctly ordered GPU work only after giving
all mutable UBO, instance and indirect buffers explicit per-frame ownership.
The current shared buffers make deleting a fence wait unsafe. Define history
read/write lifetimes and barriers together with the HDR/temporal pass design.

- Priority: P1 if profiling shows recoverable CPU overlap; also a design constraint
  for new frame resources.
- Acceptance: clean validation, stable history through resize/cuts and improved
  measured overlap. Dependent GPU temporal frames still require ordering.

### Refit the ray-tracing TLAS

Benchmark legal fixed-slot TLAS update against rebuild, including subsequent
ray traversal, inactive/masked instance semantics, scratch and allocation cost.
Use update-capable initial builds only where they improve total frame cost.
Preserve static terrain/tree BLAS unless their geometry actually changes.

- Priority: P2 if acceleration-structure work is significant after the terrain rebuild.
- Acceptance: correct dynamic visibility and lower combined build/traversal cost;
  a shorter update timestamp alone does not establish a win.

### Reuse per-frame CPU scratch storage

Reuse visibility, bough-grouping and ray-instance storage with bounded capacity.
Cache genuinely static data and keep per-frame ownership explicit. This can
recover CPU cost for richer scenery without changing its appearance.

- Priority: P1 when the Release profile confirms allocation/grouping cost.
- Acceptance: fewer allocations and better frame-time tails without stale entries
  or races; do not change culling/placement to make the timing look better.

### Add scalable ray-tracing quality presets

Give shadows, AO and reflections explicit distance, sample, projected-area and
roughness budgets on Arc A370M, with lower-cost compatibility settings for the
integrated GPU. Tune stable budgets first. Preserve close solid contact and
soft foliage shadows; avoid quality changes that visibly pump with camera speed.

- Priority: P1 alongside the new material/lighting foundation.
- P2 extension: reduced-resolution ray terms with depth/normal-aware temporal
  reconstruction after the required buffers and histories exist.
- Acceptance: measured GPU savings, stable moving edges and an explicit visual
  cost for each setting. Do not assume more rays are necessary for realism.

### Dynamic resolution or upscaling

Evaluate temporal reconstruction after linear HDR, correct motion/depth,
disocclusion and responsive masks are available. Begin with native temporal AA;
retain native MSAA as the reference/fallback. At 720p, aggressive downscaling
can lose leaves and aiming detail, so require a demonstrated net quality/cost win.

- Priority: P2; dependent on the P1 temporal foundation.
- Platform gate: verify native Linux/Vulkan SDK support, features and licences.
  Arc hardware alone does not imply XeSS availability on this platform.
- Acceptance: reconstruction cost, memory, thin detail, latency and motion quality
  are included. Frame generation does not substitute for real frame performance.
- Details: [rendering roadmap](docs/RENDERING_ROADMAP.md#hdr-temporal-stability-and-reconstruction).

### Terrain chunk culling and LOD

If erosion resolution makes the single terrain mesh expensive, prototype chunk
culling and screen-error LOD with shared boundaries and smooth transitions.
Retain exact near-field tank contact and define raster/ray/query error limits
before using simplified geometry. Ordinary indexed meshes are the first path.

- Priority: P2, triggered by terrain raster/traversal or mesh-memory cost.
- Acceptance: no cracks, visible bank popping or contact/shadow disagreement.
- Full virtualised geometry and streaming remain P3 research, not required work.

### GPU-driven visibility and indirect drawing

Foliage already uses CPU-generated multi-draw commands. Add compute culling,
optional conservative HZB occlusion and indirect command generation only when
denser terrain/ground vegetation makes CPU selection or hidden geometry a
measured limit. Query optional features and retain a working indexed fallback.

- Priority: P2 after terrain/vegetation density is defined.
- Acceptance: moving-camera/disocclusion correctness and net CPU/GPU savings,
  including compute/Hi-Z construction cost. Preserve the current tree LOD output.
- Mesh shaders and Nanite-style hierarchy/streaming remain separate P3 research.

### Performance recommendation

Measure generation and runtime as separate budgets. Keep the accepted cold
startup target while coordinating erosion and tree work under one worker budget.
During runtime, benchmark representative Release driving, water, terrain and
tree scenes before adding passes or choosing quality defaults.

Prioritise measured allocation/batching/ownership fixes that make room for P1
materials and temporal stability. Terrain LOD, GPU visibility, reduced-resolution
ray passes and upscaling are targeted P2 experiments, not mandatory rewrites.
Use the [rendering roadmap](docs/RENDERING_ROADMAP.md) for hardware/memory gates
and the [terrain plan](docs/TERRAIN_EROSION_PLAN.md) for generation acceptance.

## Visual improvements

The current presentation already includes procedural terrain and materials,
clouds and distance fog, ACES-style tonemapping, ray-traced soft shadows/AO and
selected reflections, depth-aware reflective water, dynamic muzzle/explosion
lighting, debris, smoke, dust, and independent tread decals. Future work should
strengthen a chosen art direction and improve motion/readability rather than
add effects indiscriminately.

### Establish visual targets

The chosen direction is a realistic British countryside scene: coherent scale,
materials and motion under cool sky fill, warm sunlight, broken cloud and
restrained haze. Exact legacy terrain colours and shapes are replaceable.
[Visual target and in-game reference board](docs/VISUAL_TARGET.md) record the
pillars and repeatable tank, landscape, and water/rock views. `--seed` and
`--view` reproduce static scenes for future material and lighting comparisons.

- Value: keeps otherwise-good effects visually coherent and prevents endless
  local tweaking.
- Complexity: low.
- Status: initial reference board completed; use the evolving realism criteria
  and preserved tree views for future changes.

### Weapon firing presentation

The existing recoil, spring return and chassis kick now have a brief directional
muzzle flame with a small face-on core, followed by a soft expanding smoke burst.
Smoke slows from a forward jet into a rising cloud; back-to-front transparent
cards test against the scene without overwriting depth or shadow/AO history.
Camera movement is unchanged.

Dry terrain hits leave irregular scorch patches blended directly into the
terrain material. They follow slopes, stay for 45 seconds and fade over the last
12 seconds. At most 16 patches, 4 flashes and 48 muzzle-smoke cards are retained.
Terrain marks add no draws or RT instances. Depth-softened intersections are
P1 effect work; bounded impact marks on non-tree props are P2. Runtime craters
and tree damage remain P3 gameplay work outside the terrain-generation goal.

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
This is a static weathering pass. Shared PBR response is P1; bounded
surface-driven dirt/mud accumulation is P2 after terrain fields and filtered
material masks exist. See the updated [tank reference](docs/VISUAL_TARGET.md#tank-material-checkpoint).

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
- Running-gear animation is covered by [animated tracks](#animated-tracks).
  [Visual articulation](#visual-suspension-articulation) is a P2 contact improvement;
  finer fittings need a screen-size benefit rather than blanket subdivision.

### Foliage and environmental motion

Bark, foliage and shrubs now share coherent low-frequency wind sway. The
remaining options are smaller independent leaf motion and aligning dust and
cloud motion with the same weather direction. Terrain has no grass-blade
geometry to animate.

- Value: makes an otherwise-static landscape feel alive and improves motion
  cues when the tank is stationary.
- Complexity: medium.
- Priority: delivered tree motion is protected during the terrain overhaul.
  Shared wind for new grass/effects is P2; independent tree flutter or animated
  ray geometry remains deferred until separately justified.
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
This is a fixed daylight preset. Modest cloud motion/shadows and improved
sky illumination are P2 under the advanced-lighting item; a full time-of-day
or weather cycle remains P3 until broader gameplay/lighting needs justify it.

- Value: improves scene-wide cohesion, particularly in water reflections and
  at the horizon.
- Complexity: medium; dynamic time of day and cloud shadows increase it.
- Status: completed for the fixed British daylight preset.

### Physically based material foundation

Replace the current mixed highlight/reflection approximations with a coherent
opaque material model: energy-conserving GGX response, base colour, roughness,
metal/dielectric behaviour and filtered normal detail. Coordinate this with the
terrain material rebuild. Painted armour remains dielectric; exposed metal,
stone, rubber and wet deposits need distinct responses. Preserve tree leaf
transmission and verify its appearance under any shared lighting changes.

- Priority: P1, beginning at the terrain's material stage.
- Acceptance: convincing response in neutral/matched lighting without compensating
  albedo hacks, specular shimmer or unexplained scene-tone shifts.

### Linear HDR and temporal image stability

Add a linear HDR colour target and one output exposure/tone-map stage. Define
motion vectors for all moving/deforming surfaces, depth/velocity resolve,
responsive masks, history rejection and reset rules. Then prototype native
colour TAA with the existing MSAA path as a comparison/fallback. Current
shadow/AO accumulation is not full-scene temporal AA.

- Priority: P1; prerequisite for temporal upscaling and advanced reconstruction.
- Acceptance: less foliage/specular shimmer without blurred leaves, tank trails
  or aim-point softness, with measured bandwidth and memory cost.
- Details: [temporal design](docs/RENDERING_ROADMAP.md#hdr-temporal-stability-and-reconstruction).

### Selective advanced lighting and atmosphere

Tie diffuse sky illumination and roughness-filtered reflections to the same
procedural sky. Retain dynamic lights and selective ray-query visibility. If
indirect lighting remains visibly deficient, test sparse dynamic irradiance
probes with fixed update/ray/memory caps and a sky-fill fallback. This avoids
committing the whole scene to expensive multi-bounce tracing.

Prefer analytic aerial perspective and a shared cloud-motion/shadow field
before a volumetric weather system. Limited low-resolution scattering is an
experiment only where it materially improves depth and target readability.

- Priority: P2 after materials, temporal stability and quality budgets.
- Acceptance: better reference-matched illumination, no probe leaks/history trails,
  bounded update latency and a net acceptable GPU/memory cost.
- Full-scene path tracing, large volumetric weather and complex GI frameworks
  remain P3 research on this hardware.

### Near-field ground vegetation

Add bounded grass/clump instances driven by final soil/moisture and route
fields. Fade density/detail into the ground material with distance and test
geometry versus coverage cards by overdraw and visual stability. Most blades
should not become individual ray-tracing instances. Existing tree assets stay intact.

- Priority: P2 after terrain/material integration and temporal stability.
- Acceptance: believable ground scale/contact without a carpet of shimmer,
  visible density rings or excessive raster/TLAS cost.

### Shorelines and terrain transitions

Deliver this within the P0 terrain overhaul: persistent water and final ground
must share shoreline intersections, water depth and wetness. Derive bank
materials, deposits and exposed stone from the generated fields. Rework the
old absolute-height rules. The old cliff strips have been removed.

Use flow-directed surface detail and restrained shoreline foam only where
flow/depth supports it. Small normal detail is preferable to displacement
that disagrees with the contact surface. PBR inputs join the P1 material work.

- Priority: P0 for geometry/classification; P1 for material response.
- Acceptance: no floating water, hard artificial banks or material/prop mismatch.

### Particle and smoke presentation

The muzzle already uses soft sorted cards. Extend that approach to remaining
effects with depth-softened intersections, bounded lighting/extinction and
surface-driven dust. Batch compatible draws while preserving transparent order.
Define sampled depth and responsive-mask handling with the HDR/temporal work.
Use simple terrain collision for cosmetic debris where contact is visible.

- Priority: P1 for batching, surface response and depth integration; P2 for
  broader smoke lighting after profiling overdraw.
- Acceptance: grounded effects during motion without colour/history trails,
  occluded targets or unbounded particle/lighting cost.
- Full fluid/volumetric explosion simulation remains P3 research.

### Camera presentation

Preserve the settled follow/aim views and stable firing camera. Address actual
terrain/scenery clipping or contact-view problems exposed by the new landforms.
Keep cinematic framing in optional capture tools rather than normal play.

- Priority: P2 only for a reproduced visibility/clipping problem.
- Recoil zoom, automatic speed FOV, additional shake and blanket spring lag
  are not default upgrades; prior recoil-camera experiments were rejected.
- Acceptance: stable aiming, readable terrain and no new motion discomfort.

### Post-processing and exposure

Linear HDR and temporal stability are P1 foundations, not late polish. After
they work, add restrained bloom for actual bright sources and optional grading
with fixed exposure as the reference. Auto-exposure only earns a place when
lighting range demands it and adaptation does not fight aiming/readability.

- Priority: P2 for bloom/grading after HDR and calibrated materials.
- Acceptance: highlight detail and scene consistency improve without hiding
  lighting/material faults. No default vignette, chromatic aberration, depth
  of field or motion blur as a substitute for realism.

### Environmental variety and composition

Replace rocks, scree, shrubs and their placement as needed during the
P0 terrain overhaul. Use erosion, resistance, slope and moisture fields for
coherent formations and distribution. Preserve trees, adapting placement only.

Add P2 near-field vegetation after its temporal/cost prerequisites. Additional
ruins, landmarks and prop families should serve composition or navigation,
with geometry and material detail chosen by actual screen size.

- Priority: P0 for the environment replacement; P2 for measured visual variety.
- Acceptance: coherent landscape scale and useful open routes, without hiding
  weak terrain under uniformly dense clutter.

### HUD and interaction feedback

Keep the HUD at native output resolution after reconstruction/tone mapping.
Preserve the current aiming feedback and performance controls through renderer
changes. Add renderer diagnostics when needed for development; new gameplay
hit/reload systems and broad UI redesign remain separate product work.

- Priority: P1 compatibility during HDR/upscaling; P3 new gameplay/UI features.
- Acceptance: crisp aim feedback and legible metrics across resize and quality modes.

### Visual recommendation

Deliver hydraulic terrain shape and shared environmental data first, then
coordinate the ground rebuild with P1 physically based materials and HDR/temporal
stability. These foundations address broad realism weaknesses before adding
more geometry, rays or cinematic effects. Preserve the tree baseline and tank
handling/camera while testing the new ground.

Use P2 techniques for specific remaining problems: near vegetation and terrain
LOD for scale, selective lighting for depth, temporal reconstruction for image
stability or GPU headroom, and modest visual suspension for contact. Enable each
only after target-hardware measurements and moving-image review. Expensive
rendering research and simulation-heavy gameplay remain P3.
