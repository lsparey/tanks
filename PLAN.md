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
- **Gameplay — delivered:** a turn-based, Pocket Tanks-style duel in
  3D against an opponent tank: fuel-limited movement, manual aiming with shot
  power, accuracy-scaled damage and collectable power-up crates
  ([objectives](#gameplay), all 9 items complete). Pursued ahead of the
  outstanding terrain measurement gates below rather than after them; did
  not regress the terrain, tree or frame budgets. The last item's own
  Release frame-budget measurement against the terrain baseline remains an
  open follow-up.
- **P3 — deferred:** general rigid bodies, real-time multiplayer, and
  expensive rendering research without a demonstrated use case.

New rendering work is planned, not delivered. The existing tree system remains
protected. Establish a representative Release baseline before setting default
quality; 60 Hz is a working aspiration, not a result inferred from the old
Debug timings. Keep the roughly 5.3-second startup target and budget memory,
CPU work, GPU work and temporal quality together. Completed entries describe
initial delivered passes, not a ceiling on future visual fidelity.

## Current goal

**Hydraulic erosion terrain overhaul — the advanced generator is the game
default: erosion, continuous spill-connected water, partial lakes, refined
513-sample surface and generated material fields are implemented; wider
startup/driving measurements and a memory cap remain.** Replace
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

`--menu` shows a two-option terrain menu (original fast heightmap, advanced
eroded terrain) with the advanced generator selected. Without it, launches load
the advanced generator directly with 2x surface refinement; `--terrain legacy`
keeps the original quick generator and `drained-valley` remains an accepted
alias for `advanced`.

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
- [x] Compare 257/513 grids with unchanged erosion and matched views. Keep 257:
  finer geometry alone leaves channel artifacts, costs substantially more and
  hits the step limit on the ridge example ([comparison](docs/TERRAIN_RESOLUTION.md)).
- [x] Prototype an opt-in finer final surface/channel pass while retaining the
  257 erosion grid; keep water, contact and rendering consistent
  ([implementation, measurements and limits](docs/TERRAIN_REFINEMENT.md)).
- [x] Extend final refinement to 1025 samples with unchanged 257 erosion;
  capture matched 513/1025 views and measure stationary GPU/loading/memory costs
  ([comparison and test procedure](docs/TERRAIN_REFINEMENT_4X.md)).
- [x] Fix the remaining tributary gap: lake outlets now stand a bounded spill
  head above their crest, so escaping sheets keep positive depth. All 16 seeds
  report zero dry sections and zero zero-depth spill controls; 2x refinement is
  now the advanced default. Global 1025 refinement remains experimental because
  of its measured rendering cost.
- [x] Compare shorter erosion against the current visual reference: an
  18-second storm keeps near-identical heights and an equivalent clean water
  network on all 16 seeds (channel readability comes mostly from the
  duration-independent carving pass) while median generation drops from 3.3 s
  to 2.3 s. The game recipe now uses 18 s / 13.5 s rain; the solver and probe
  keep the 24-second default for recorded-baseline continuity. Release loading
  fell to 2.7-4.1 s across the three measured seeds. No GPU-compute port is
  justified at this cost.
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
- [x] Finish Stage 2 quality/performance gates: the erosion test suite covers
  the required flat/slope/bowl-saddle/dividing-ridge/confluence fixtures with
  drying, outlets, axis-swap grid-bias symmetry and timestep/spatial
  convergence tolerances; startup coverage now spans repeated multi-seed
  Release loads and the measured 18-second game recipe on the four-worker CPU
  backend.
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
- [x] Add positive-depth spill connections (bounded outlet spill head) and an
  explicit partial-lake policy (under-supplied basins stand at their sampled
  equilibrium level, consume their inflow and never spill). Combined water is
  continuous across every junction and spill on the 16 fixed seeds; narrow
  steep tributaries may still render sub-pixel thin, which stays accepted as
  wet ground. LakeWater/StreamNetwork/TerrainWater are now v2.
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
- [x] Smoke-validate actual driving: deterministic Release `--drive-preview`
  runs drive, track and fire on advanced terrain across seeds without errors.
  Extended interactive driving/effects coverage remains part of the measurement
  work below.
- [x] Rebuild ground materials and scenery placement from generated fields: the
  `TerrainMaterials` stage classifies rock (thin/scarred soil on steep ground),
  moisture (bank distance, storm exposure, gullies) and sediment (deposition),
  shared verbatim between `basic.frag` (new terrain field texture) and CPU
  placement (pebbles, grass tufts, rock clusters, shrubs, tree appeal), with
  flow-animated stream ripple, geology-aware rounded/angular rock forms per
  cluster and moisture-thinned shoreline reed fringes on the same data. Ground
  texture art direction stays open to iteration.
- [x] Consolidate to one final advanced generator: `--terrain legacy|advanced`,
  advanced default with 2x refinement and material fields; the menu offers the
  two generators plus landform/seed. Release loading is 4.30 s median / 4.80 s
  maximum across three seeds and three repeats on the Arc A370M machine, within
  the roughly 5.3-second target.
- [ ] Complete the outstanding measurement gates: unlocked-desktop GPU frame
  benchmark, extended driving/effects coverage, grid-bias/convergence checks
  and an enforced memory cap.

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

### Gameplay objectives

- [x] [Match rules and turn state](#match-rules-and-turn-state)
- [x] [Opponent tank](#opponent-tank)
- [x] [Fuel-limited movement](#fuel-limited-movement)
- [x] [Manual aiming with shot power](#manual-aiming-with-shot-power)
- [x] [Hits, accuracy-scaled damage and destruction](#hits-accuracy-scaled-damage-and-destruction)
- [x] [Power-up crates](#power-up-crates)
- [x] [Opponent AI](#opponent-ai)
- [x] [Turn camera and match HUD](#turn-camera-and-match-hud)
- [x] [Match flow and deterministic replay](#match-flow-and-deterministic-replay)

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
- [x] [Physically based material foundation](#physically-based-material-foundation)
- [x] [Linear HDR and temporal image stability](#linear-hdr-and-temporal-image-stability)
- [x] [Selective advanced lighting and atmosphere](#selective-advanced-lighting-and-atmosphere)
- [x] [Near-field ground vegetation](#near-field-ground-vegetation)
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

## Gameplay

The target is a turn-based artillery duel in the spirit of the 2D Pocket
Tanks, played in this 3D landscape. The player and one opponent tank
alternate turns; each turn a tank may move within a fuel budget, aim
manually and fire. Shells damage the other tank in proportion to how
accurately they land, and three solid hits destroy it. Crates scattered on
the map are collected by driving over them and grant power-ups. Manual
aiming is meant to be difficult: the player controls direction (turret yaw
and gun elevation) and shot power (muzzle velocity), and ranging has to be
learned from watching shells fall.

Existing building blocks this reuses rather than replaces: the single
`Tank` (handling, recoil, turret/elevation, capsule collision, animated
running gear), `Projectile` (ballistic drop, swept `CollisionSystem` tests
against terrain, trees, rocks and boxes), the destructible `Box` crates and
`spawnBoxes`, `TerrainPlayability` spawn candidates and connected routes,
the `--weapon-preview` deterministic capture path, `CombatHud` (map, heading,
aim projection) and the existing impact/explosion/debris/smoke effects and
audio layers. Tank handling, the tree system and the terrain generator stay
as they are; gameplay code lives in its own state layer so rendering and
simulation do not learn match rules.

Design decisions to settle early, recorded here so they are not re-argued:

- Range comes from shot power, not a wider elevation envelope. The
  Challenger 2's −10°/+20° gun limits are kept; the velocity range and
  `Projectile::kGravity` are tuned together so the full playable area is
  reachable from any spawn without lobbing shells vertically.
- Damage is per hit, scaled by accuracy: a direct hull hit scores full
  damage, a near miss scores splash damage falling off with distance from
  the hull, and nothing beyond the splash radius. "Three hits" means three
  full-damage hits; several splash hits can add up to the same total.
- Crates are pickups, not physics props. Collection is driving the hull
  footprint over the crate; shells still destroy crates as today, which
  wastes them.
- Turns resolve fully before control changes hands: a turn ends when the
  last shell has landed and its effects have settled, never mid-flight.
- The opponent uses the same inputs the player has (drive, turret, elevation,
  power, fire) and sees only what a player could observe. It does not read
  hidden state or receive a perfect ballistic solution by default.

Work is planned in this order; later items depend on earlier ones.

- [x] [Match rules and turn state](#match-rules-and-turn-state)
- [x] [Opponent tank](#opponent-tank)
- [x] [Fuel-limited movement](#fuel-limited-movement)
- [x] [Manual aiming with shot power](#manual-aiming-with-shot-power)
- [x] [Hits, accuracy-scaled damage and destruction](#hits-accuracy-scaled-damage-and-destruction)
- [x] [Power-up crates](#power-up-crates)
- [x] [Opponent AI](#opponent-ai)
- [x] [Turn camera and match HUD](#turn-camera-and-match-hud)
- [x] [Match flow and deterministic replay](#match-flow-and-deterministic-replay)

### Match rules and turn state

Add a match state layer that owns the two combatants, whose turn it is, the
phase within the turn (move, aim/fire, resolve) and the win condition. The
fixed-step simulation, input handling and rendering read from it; they do
not decide rules. Input routing follows the active tank: the player's
controls act on the player tank only during the player's turn and are
ignored otherwise. Keep the current free-play behaviour reachable behind a
flag for rendering and terrain work.

- Priority: first gameplay item; everything below depends on it.
- Acceptance: a headless test drives a complete match with scripted inputs
  to a win, phases transition only on the defined events, and no rule logic
  lives in `Application` draw or simulation code.

Status: completed for the rules engine itself. `MatchState`
(`src/scene/MatchState.h/.cpp`) owns two `CombatantState` records
(alive, health in "full hits" units so fractional splash damage can
accumulate toward the three-hit kill, fuel, shells) plus whose turn it is
and a `Phase` (`Move`, `AimFire`, `Resolving`, `GameOver`). Every mutator
(`spendFuel`, `endMovePhase`, `recordShotFired`, `applyDamage`,
`notifyProjectilesSettled`) is a no-op outside the phase it applies to or
once the match is over, which is what makes "phases transition only on the
defined events" a checkable property rather than a convention. `tests/MatchStateTest.cpp`
(headless, no engine/Vulkan dependency, following the existing
`ProjectileTest.cpp` idiom) proves that no-op guarantee for every mutator,
fuel exhaustion auto-ending the move phase, fuel/shell refill happening only
for the incoming combatant at a turn boundary, fractional splash damage
accumulating to a kill across turns, and a full five-turn scripted match
reaching `GameOver` with the correct `winner()`.

Deliberately not done here, pending later roadmap items: no `Application.cpp`
wiring, no CLI flag and no second `Tank` instance -- `CombatantId::Opponent`
is a bookkeeping label with nothing behind it until [opponent tank](#opponent-tank)
exists, and there is nothing to gate input on until then either. `applyDamage`
is exercised only with literal test values; computing it from an actual
shell's impact distance is [hits, accuracy-scaled damage and destruction](#hits-accuracy-scaled-damage-and-destruction).

### Opponent tank

Spawn a second `Tank` at a valid `TerrainPlayability` spawn candidate with a
minimum separation from the player and a connected route between them, using
the existing route reservation so scenery placement keeps both spawns open.
Render and ray-trace it like the player tank (raster instances, TLAS
instances, running gear) and give it a distinct camouflage or marking so the
enemy is readable at range. Driving collides with its hull the same way it
does with static scenery.

- Dependency: match state; `CamoTextureGenerator` for a second scheme.
- Cost gate: measure the Release frame cost of a second full tank (draws,
  TLAS instances, running-gear transforms) on the Arc A370M before accepting
  it as the default scene.
- Acceptance: two tanks spawn on every one of the 16 fixed seeds with a
  connected route, neither inside water, scenery or the boundary; matched
  screenshots show the same lighting and shadow response on both.

Status: completed for spawn, rendering and driving collision, with two
corrections to this item's original text. `TerrainPlayability::secondarySpawn`
reuses the accepted route's far end (already guaranteed connected, dry,
flat-enough and >= `minimumRouteSpan` from the player, and already protected
from scenery by the existing `Reservation`, which seeds every route cell, not
just the spawn) rather than an independent search, walking back along the
route for a fully clear cell if the far end itself carries `SpawnSteep`
(`kRouteBlocked` excludes it). Verified empirically on the three baseline
seeds (7331, 42, 65535) under real game settings: the far end qualifies
directly every time, so the walk-back is a defensive path, not a load-bearing
one in practice. `Application` constructs a second `Tank` (`opponentTank_`)
unconditionally but only places/draws/collides it when the advanced generator
produced a navigation result (`--terrain legacy` has neither a route system
to derive a second spawn from nor an opponent). Its armor parts bind a second
material set from a desert/OPFOR-leaning `CamoTextureGenerator::Palette`
(reddish-brown/sand/khaki/near-black) instead of another green scheme, so it
reads as distinctly "other" against this game's green countryside -- visually
confirmed via a new `--view opponent` reference camera. Raster draw and TLAS
gathering for both tanks share one code path (`drawTankParts`/
`appendTankRayInstances`) so the two can't silently drift apart. Driving
collision reuses the existing tank-vs-circle-obstacle system: the opponent's
footprint (its own already-computed `footprintRadius`) is added once to
`obstacles_` alongside trees/rocks, added after (not into) the obstacle list
`verifyObstacles` re-analyzes with, since including it there would make the
opponent's own parked position look like scenery blocking its own route cell.

Two corrections to this item's original text: it does not participate in
tree shadow cascades (nothing here does -- the tank has always been shadowed
entirely by ray tracing) so that phrase is removed above; and shell-vs-hull
collision is deliberately not built here. No shell-vs-tank collision exists
anywhere in this codebase yet, not even for the player, so building it now
with no damage system to call would just be throwaway code that [hits,
accuracy-scaled damage and destruction](#hits-accuracy-scaled-damage-and-destruction)
immediately replaces; that item now owns the whole "shells collide with a
hull capsule" behavior, for both tanks at once.

The opponent does not move yet (no AI/turn-driven input exists until later
roadmap items) -- it is placed once via `placeAt` and never calls `update()`.
This is why no `Pipeline::FrameUBO`/shader changes were needed for motion
vectors: `basic.vert`'s existing static-object fallback
(`previousModel = pc.model`) is already exactly correct at zero velocity, and
its parts leave `PushConstants::isDynamicObject` at `0` rather than the
player's `1+poseGroup` encoding, since that flag exists specifically to skip
`basic.frag`'s temporal shadow/AO smoothing for a genuinely moving rigid body
-- a static tank gets *better* shadow quality through the normal smoothed
path, not worse. Also not needed: any change to `TerrainRuntime::verifyObstacles`
-- `secondarySpawn`'s result is always a member of `navigation->route`, so
the existing per-route-cell check already covers it.

Not done here, pending later roadmap items: opponent movement/AI ([opponent
AI](#opponent-ai)); audio (`AudioEngine` has one global engine-sound voice
keyed to the player) and a HUD marker (`CombatHud::State::targets` is
`Box`-typed) -- neither is mentioned in this item's text; HUD is [turn camera
and match HUD](#turn-camera-and-match-hud). The frame-cost gate above is
unmeasured in the fully trustworthy sense: like the terrain benchmark work,
a real number needs an unlocked desktop.

### Fuel-limited movement

Give each tank a per-turn fuel budget spent on distance travelled and pivot
turns. Movement inputs stop acting when fuel is exhausted and the tank coasts
to rest with the existing braking; the move phase can also be ended early
by the player. Refilled at the start of each turn to the tank's current
capacity, which power-ups can raise. Handling, slope behaviour and collision
are unchanged; fuel only gates input.

- Dependency: match state.
- Acceptance: fuel drains deterministically for a scripted drive, the tank
  cannot move past zero, and a fixed-seed drive-preview shows identical
  handling with fuel disabled.

Status: completed, behind a new opt-in `--match` flag (off by default).
`MatchState` gained a pure `movementFuelCost(forwardSpeed, angularSpeed,
deltaTime)` (fuel/meter plus fuel/radian, tuned by feel), and
`Tank::update` gained a trailing `driveEnabled` parameter that wraps only
the four throttle/turn key reads (W/S/A/D) -- turret yaw, gun elevation and
firing are untouched, so a fuel-exhausted tank still aims and fires, it just
coasts to a stop under the existing braking/rolling resistance rather than
accelerating further. `Application` spends fuel every frame from the
player's actual post-update `signedSpeed()`/`angularSpeed()` and gates
`driveEnabled` on `matchState_.phase() == Phase::Move`; a new Tab key ends
the move phase early, matching "the move phase can also be ended early by
the player." `tests/MatchStateTest.cpp` proves the fuel-cost formula and
pins the exact frame a scripted constant-speed (and constant-pivot) drive
exhausts the default 20-fuel tank, satisfying "drains deterministically" and
"cannot move past zero" without needing `Tank` itself (which has no headless
test harness -- it needs live Vulkan/GPU resources). Verified live: a
`--match --drive-preview` run reaches "Fuel exhausted -- move phase ended"
and the tank visibly coasts to rest under momentum; the same run without
`--match` produces zero fuel-related output or behavior change, satisfying
"identical handling with fuel disabled."

The opt-in flag exists because turn-passing isn't wired at all yet -- this
item deliberately does not touch `activeCombatant()`, `recordShotFired()` or
`notifyProjectilesSettled()`, so nothing this session can ever hand a turn
back to the player once it leaves Move/AimFire. Shipping fuel-gating as
*default* behavior before an opponent AI (or any other way to end a "turn")
exists would eventually strand a live player with no way to drive again
short of restarting; `--match` keeps every existing free-play workflow
(terrain review, prior `--drive-preview` baselines) byte-identical until
enough of the roadmap exists to make match mode a complete default. Not
done here: any HUD/console feedback beyond two `std::cout` lines (a fuel/
phase readout is item 8's job), and no restriction on firing by phase (item
4, once aiming/shot power exist to restrict).

### Manual aiming with shot power

Add muzzle-velocity control alongside the existing turret yaw and gun
elevation: a bounded power value adjusted by held keys or a charge-and-release
fire, shown on the HUD. Tune the velocity range with `Projectile::kGravity`
so the whole boundary is reachable at full power from a level shot, and
minimum power lands a shell within a few hull lengths. Increase projectile
lifetime to cover the longest flight. No trajectory preview by default;
ranging is by observation, and the shell trail and impact effect must stay
visible from the firing tank's camera.

- Dependency: match state; the existing `fireProjectile` and recoil path.
- Acceptance: a table of (elevation, power) → landing distance on flat
  ground is documented and monotonic; the extreme cases are covered by a
  `ProjectileTest` fixture; recoil scales visibly with power.

Status: completed. `Tank` gained `shotPower_` (normalized 0..1, held T
up/G down keys, same pattern as R/F gun elevation), `shotSpeed()` (`glm::mix`
between `kMinShotSpeed=4.5` and `kMaxShotSpeed=45.0`), and
`applyGunRecoil(float powerFraction)` scales both the barrel kick and the
hull impulse (0.5x-1.5x) so recoil visibly grows with power while the
barrel's authored maximum travel stays fixed. `fireProjectile` uses
`tank_->shotSpeed()` instead of the old fixed 25 units/s. `CombatHud` shows
"PWR n%" alongside the existing EL/TRV readout; `Tank::kGravity` itself is
unchanged (existing `ProjectileTest` drop-distance assertions stay valid) --
only the velocity range was tuned against it, per the dependency note.
`Projectile::lifetimeRemaining` increased 3.0 -> 12.0.

The full (elevation, power) landing-distance table, computed by
`tests/ProjectileTest.cpp`'s `landingDistance` helper (flat ground, a
representative 1.5 m muzzle height, unbounded simulated time so every cell
reports the true ballistic landing point regardless of the shipped
lifetime):

| elevation | 0% | 25% | 50% | 75% | 100% |
|---|---|---|---|---|---|
| -10° | 6.6 m | 8.2 m | 8.4 m | 8.5 m | 8.5 m |
| 0° (level) | 14.2 m | 46.2 m | 78.3 m | 110.3 m | 142.3 m |
| +10° | 29.7 m | 252.1 m | 706.8 m | 1395.1 m | 2317.3 m |
| +20° | 47.2 m | 462.4 m | 1316.8 m | 2610.0 m | 4343.4 m |

Strictly monotonic in power at every elevation (the acceptance requirement);
minimum power at a level shot lands at 14.2 m (a few of the tank's ~4.48 m
hull lengths) and maximum power at a level shot reaches 142.3 m, well past
the default terrain's 72 m boundary half-extent -- both literal acceptance
targets. The 0°/level row alone stays within a 3.16 s flight regardless of
power; every row's rightmost cells beyond roughly +10° elevation combined
with more than ~25% power exceed the shipped 12 s lifetime and will
disappear mid-flight rather than visibly land (documented, not hidden --
see the table's own numbers and `Projectile.h`'s comment). This is the
directly-observed, now-measured consequence of this roadmap's own earlier
design decision ("range comes from shot power, not a wider elevation
envelope... so players don't [need to lob] shells vertically"): under this
game's deliberately weak `kGravity`, calibrating the velocity range to reach
across the map at a level shot means any real elevation combined with
more than modest power produces an enormous, off-map, impractical range --
increasing `kGravity` cannot fix this while also hitting the two literal
level-shot targets (worked through analytically: the high-elevation range
at a fixed level-shot target is independent of `kGravity`). Elevation
remains most useful as a fine, low-power adjustment near the map's own
scale, exactly as the original design called for; it is not a usable
range lever at high power, by design.

No trajectory preview was added (matches the item's own text). Not done
here: gating firing by match phase (item 3 explicitly left firing
ungated; nothing here changes that either -- shot power/aiming apply the
same whether or not `--match` is set).

### Hits, accuracy-scaled damage and destruction

Detect shell hits against a tank's hull (swept segment against the existing
hull capsule) and terrain impacts within a splash radius of a tank. Damage is
a base value scaled by accuracy: direct hits score full damage, splash falls
off with distance from the hull to zero at the radius. Each tank has hit
points equal to three full hits. Destruction reuses the explosion, debris,
smoke and dynamic-light effects, leaves a darkened wreck that remains a
collision and ray-tracing obstacle, and ends the match. Self-damage from
the firer's own splash is allowed.

- Dependency: match state; opponent tank; shot power.
- Acceptance: unit tests pin the damage curve at the hull, at half radius
  and at the edge; a scripted three-direct-hit match ends in destruction;
  three edge splash hits do not.

Status: completed. `Tank::distanceToHull` (reusing `simulateMovement`'s own
movement-collision capsule via an extracted `hullCapsule()` helper, so hit
detection can never disagree with the hull the tank itself drives/collides
with) and `MatchState::splashDamage(distance, radius) = clamp(1 -
distance/radius, 0, 1)` unify direct hits and splash into one formula and
one code path: a direct hit is just that curve's `distance == 0` case.
`updateProjectilesAndCollisions` gained one new priority check (a plain
per-frame point test, not swept -- an occasional tunnel-through at typical
shell speeds degrades to a close splash hit rather than a clean miss, an
accepted tradeoff) for "did the shell touch a tank," and the existing
`triggerHit` lambda already shared by box/tree/rock/terrain hits now also
sweeps both tanks' `distanceToHull` and applies `MatchState::applyDamage`
-- so a shell landing near a tank (splash) and one landing on it (direct)
go through the exact same damage call. `fireProjectile` now calls
`MatchState::recordShotFired` (only when it's actually the player's
`AimFire` turn) and `updateProjectilesAndCollisions` calls
`notifyProjectilesSettled` once every shell has landed, which is what
makes `applyDamage` functional at all in live play (it no-ops outside
`Phase::Resolving`). Also fixed item 3's `driveEnabled` gap: it now checks
`activeCombatant() == Player`, not just phase, since a turn can actually
pass now.

**The wreck visual needed a real geometry change, not just a material
tweak.** The first version only lowered `specularStrength` (0.15 vs 1.0,
no shader/PushConstant change) and was confirmed by direct screenshot
comparison to be visually imperceptible -- this scene's mostly-diffuse
cloudy lighting rarely puts a strong specular highlight on the hull to dim
in the first place. Replaced with a rigid world-space tilt (10 degrees
about a mixed X/Z axis, pivoting on the hull's own position) applied
identically to the non-instanced hull/turret/barrel parts *and* the
instanced running-gear instances baked into the shared raster buffer --
tilting only the former was tried first and looked broken (the hull
floated at an angle above its own flat, untilted wheels/tracks) before
extending it to both. Confirmed by screenshot: the whole tank now visibly
lists to one side as one rigid wreck. Combined with periodic smoke (moved
from the tank's ground-reference position to above the hull deck after
the first version was confirmed invisible, occluded by the tilted hull
itself) and `specularStrength` still lowered for what little it's worth.
No new shader/PushConstant field was added (confirmed still at Vulkan's
guaranteed-minimum size).

No opponent-turn stopgap was added, as scoped: a live `--match` session
gets exactly one player drive+shoot turn before `MatchState` hands control
to `CombatantId::Opponent`, which has no AI (item 7) to ever act or end
its own turn -- confirmed live, console prints "Opponent's turn -- no AI
yet" at that point. The roadmap's "a scripted three-direct-hit match"
acceptance wording is proven at the `MatchState`+`splashDamage` level in
`tests/MatchStateTest.cpp` instead (headless, no `Tank`/Vulkan needed, same
reasoning as item 3's `Tank::update` gating). Verified live otherwise:
firing near a tank applies splash correctly, a destroyed tank visibly
tilts and smokes, `matchEnabled_ == false` leaves `MatchState` completely
untouched and rendering unchanged.

### Power-up crates

Turn `Box` into a pickup: spawn a bounded number on connected routes away
from both spawns, top up as they are collected, and collect by driving the
hull footprint over one. Each holds one power-up added to the collecting
tank's inventory, chosen and consumed before firing. Initial set: extra
shells this turn, increased damage, larger splash radius, aim assist (a
predicted trajectory line for one shot), tighter accuracy (reduced shot
dispersion, which is introduced as a small default), and larger fuel
capacity. Shells still destroy crates. Crate contents are not visible
until collected in the first pass.

- Dependency: hits and damage; fuel; shot power.
- Acceptance: crates never spawn in water, inside scenery or unreachable;
  each power-up has a test proving its effect and its expiry (this turn,
  this shot or permanent); the HUD shows the inventory and the selected item.

Status: completed. The three expiry categories map onto the six power-ups
as: extra shell = this turn (tops up `shellsRemaining`, which
`notifyProjectilesSettled`'s existing "shells remain -> AimFire again"
path already turns into a real extra shot); increased damage, larger
splash, aim assist = this shot (`MatchState::consumeArmedPowerUp` resets
`damageMultiplier`/`splashRadiusMultiplier` to defaults every call, so an
unrenewed bonus provably expires after one shot); tighter accuracy, more
fuel = permanent, and apply immediately on collection rather than going
through the inventory/arm/consume flow -- there's no "this shot" meaning
for a passive stat boost to wait on, and forcing that ritual on them would
be worse UX for no benefit. `spawnBoxes`/the new `collectBox` top-up share
one `findScenerySpot` helper that now also rejects candidates outside the
spawns' connected navigation component (same clamp-then-index world-to-cell
math `TerrainSurface::sampleAt` already uses) and clear of *both* spawns,
not just the player's.

New baseline shot dispersion (`MatchState::dispersionDegrees`, reduced by
collecting `TighterAccuracy`) and the crate/power-up systems are all gated
behind `matchEnabled_`, matching this roadmap's established pattern: free
play stays byte-identical (exact aim, purely destructible crates, no HUD
inventory line), since dispersion in particular would otherwise silently
add inaccuracy to every existing `--weapon-preview` baseline. `V` (checked
free against every key bound so far) cycles which held power-up is armed.
`fireProjectile` consumes the armed power-up before computing the shell's
velocity; only one shot can ever be pending resolution at a time under the
existing turn machinery, so reading the firer's multipliers when the shell
actually lands (in `triggerHit`, alongside item 5's tank-splash sweep) is
safe without stashing gameplay data on the lean `Projectile` struct.

Aim assist renders a real predicted trajectory (12 samples, the same
step-integration `Projectile::update` uses, projected through the existing
unjittered view-proj) as connected line segments in `CombatHud`, live while
armed and gone the instant a shot fires. Verified live end-to-end
(screenshot): `collectBox` visibly relocates a crate to a new position;
the HUD inventory line and an armed selection render correctly (e.g. `SHL:1
DMG:1 SPL:2 AIM:1 [ARMED AIM]`); the trajectory line is visible and tracks
the aim. `IncreasedDamage`/`LargerSplash`'s numeric effect on
`MatchState::applyDamage` was proven by the unit tests in section 1, not by
a visual comparison -- this implementation does not scale the explosion
effect's visual size by damage dealt, only the recorded health change, so
there is no separate visual signature to screenshot beyond what the number
itself already covers.

### Opponent AI

Implement the opponent's turn with the same inputs a player has. Move phase:
choose between closing distance, reaching the nearest reachable crate and
keeping line of fire, within fuel. Fire phase: solve a ballistic (elevation,
power) for the player's observed position, then apply a deliberate aiming
error that shrinks with a difficulty setting and with the accuracy power-up.
All decisions are deterministic for a seed so matches replay. The opponent
must visibly drive, traverse and elevate before firing rather than snapping.

- Dependency: everything above; the `--seed` deterministic path.
- Acceptance: on the 16 fixed seeds the opponent completes turns without
  stalling, never fires into terrain in front of itself, hits the player
  within a bounded number of turns at the default difficulty, and a scripted
  match reproduces exactly from a seed.

Status: completed. `Tank::update` no longer reads `InputManager` directly;
a new `Tank::Controls` struct (throttle/turn plus held-key-equivalent
booleans for turret/elevation/power) is built once per frame either from
real input (`Tank::Controls::fromInput`, kept on `Tank` since it owns the
key-binding knowledge) or from the AI, and both feed the same `update()`.
The AI's actual decisions live in a new, pure, `Tank`-free
`src/scene/OpponentAI.h/.cpp` (move target, ballistic power solve, aim
error), mirroring the `MatchState`/`Projectile` split of a headlessly
testable formula from its live `Tank`-reading call site --
`tests/OpponentAITest.cpp` covers the crate-seeking/closing/holding move
logic and the power solve's monotonicity/clamping without needing Vulkan.
`Application::updateOpponentAI` reads live `Tank`/`MatchState` state and
drives per-frame `Controls` from those decisions: during the opponent's
Move phase it steers bang-bang toward the chosen target and ends the phase
on arrival (fuel exhaustion already ends it otherwise); during AimFire it
solves the shot once per turn (not re-rolled every frame, so the aim
doesn't visibly jitter) and bang-bangs turret yaw and power toward it,
firing once both (and a small fixed clearance elevation) are within
tolerance. Outside its own turn the opponent still gets a neutral
`update()` every frame so it stays grounded/settled exactly like the
player's tank always does.

Making a second tank genuinely mobile forced two structural changes.
`Pipeline::FrameUBO`'s `prevTankHullModel/TurretModel/BarrelModel` (used
for the motion-vector buffer) became `[2]`-element arrays, and
`basic.vert`'s `isDynamicObject` encoding extended from `1/2/3`
(hull/turret/barrel) to `1..6` (`+3` per tank index) -- both tanks are now
unconditionally "dynamic" except when destroyed (genuinely stationary
again, reverting to `isDynamicObject = 0`, matching item 5's smoothed-
shadow reasoning). And collision became mutual: each tank's obstacle list
now includes a circle at the *other* tank's current position, rebuilt
every frame rather than the static one-time opponent circle item 2 added
back when it never moved. `fireProjectile`/`collectBox` generalized to
take the firing/colliding combatant explicitly instead of hardcoding
`CombatantId::Player`.

Live verification initially chased what looked like an indefinite hang
the moment the opponent's turn activated. Numbered, immediately-flushed
checkpoints through `updateOpponentAI` (removed once diagnosis was done)
showed the function itself completing every frame in well under a
millisecond -- the actual cause was that reaching a high `--screenshot-frame`
means genuinely rendering that many real, vsync-paced frames (plus this
seed's own ~10-19s terrain generation), and a couple of manual repro
attempts simply used a timeout too tight for that combined cost, not an
infinite loop. Confirmed via a temporary forced `endMovePhase` hook (since
nothing else can end the player's first turn without a real key press)
that a full cycle -- player fires, opponent's Move phase drives it visibly
away from spawn toward a new position, AimFire visibly rotates its turret
independently of the hull, it fires, and control correctly hands back to
the player (idle-branch AI updates resume) -- completes correctly on seed
7331. Free play and `--match` with no AI turn yet taken are unaffected, and
all 31 tests (including the new `opponent_ai_test`) pass.

### Turn camera and match HUD

During the opponent's turn the camera follows the opponent tank and then its
shell, returning to the player's settled follow view for the player's turn.
Show whose turn it is, fuel, health for both tanks, shell count, power and
the inventory, keeping the HUD at native output resolution after tone mapping.
Preserve the current aim projection and diagnostics. Existing recoil-camera
and shake rejections still apply.

- Dependency: match state; the other items provide the values shown.
- Acceptance: control never changes hands while the camera is moving, the
  shell is visible during opponent turns, and the HUD remains legible across
  resize and quality modes.

Status: completed. The camera is overridden independent of the player's own
`cameraMode_` selection whenever `matchState_.activeCombatant() == Opponent`:
it follows the opponent tank through its Move/AimFire phases, then the live
shell during Resolving (reusing `Camera::followTarget` with the shell's
travel direction as "forward"), then blends back once control returns to the
player. Transitions are a real blend (`Camera::setPose`, new, plus a `front()`
accessor), not a cut -- a 0.6s lerp between the pose at the moment the target
changed and the newly-computed one, covering all three transitions (home to
opponent, opponent to shell, shell back to home) with one mechanism. `Free`
camera mode is a deliberate special case: it has no fixed "home" pose to
blend back to (it is wherever live input last left it, not a scripted
target), so returning to it resumes live free-fly immediately instead of
locking input against a blend that would never converge.

A related, newly-relevant gap surfaced while designing this: turret,
elevation, power and firing input were never actually turn-gated (only
driving was, since item 5) -- harmless while the opponent's turn was
invisible and instantaneous, but a real, newly-exploitable fairness break
once the camera visibly leaves the player's tank for several seconds. Closed
by generalizing the existing `driveEnabled` gate into one `playerTurnActive`
flag covering all of it, additionally requiring the camera to have finished
blending back (`!cameraTransitioning_`) before control returns -- this is
also literally what "control never changes hands while the camera is
moving" means in code.

`CombatHud::State` gained a `matchActive`/`opponentPresent`/`turnLabel`/
`playerCombat`/`opponentCombat` -- kept as plain data (no `MatchState`/
`CombatantId`/`Phase` dependency), matching how `inventoryText` was already
pre-formatted by the caller for the same reason. Live verification (seeds
7331 and 42, using the same temporary forced-`endMovePhase` hook item 7's
verification used, removed afterward) caught two real layout bugs, both
fixed before considering this done: the turn-label banner was centered at
the same screen position as the existing "GUN OUT OF VIEW" reticle message,
which the away camera makes show almost continuously (moved the label
below it); and the bottom-left stats panel is anchored by its *top*, so its
new, taller two-extra-line case (match stats plus an armed inventory both
showing) pushed its bottom edge past the canvas -- fixed by shifting the
anchor up by exactly one line's height only in that specific case, leaving
the two pre-existing cases (0 or 1 extra lines) at their exact original
position. `tests/CombatHudTest.cpp` now asserts byte-identical vertex output
between the match fields being unset and them being explicitly set-but-
inactive, not just a matching count, so free play's HUD cannot regress here
silently. `tools/HudPreview.cpp` (a standalone CPU-geometry-to-SVG dev tool,
no Vulkan/terrain needed) was extended to illustrate the new panel, which
is how both layout bugs were actually found -- far faster than a full
terrain-load screenshot cycle for pure layout iteration.

### Match flow and deterministic replay

Add match start, win/lose presentation and restart, plus a `--match-preview`
deterministic mode in the style of `--weapon-preview` that plays scripted
turns for both tanks with a fixed step and captures screenshots at known
frames. Extend the Release drive-preview coverage to a full match so
startup, generation and frame budgets are measured with two tanks and
crates present.

- Dependency: all items above.
- Acceptance: a full scripted match runs in Release on the three measured
  seeds without validation errors, within the startup target, and its
  frame times are recorded alongside the terrain baseline.

Status: completed for the functional half; the Release frame-budget
measurement against the terrain baseline is deliberately deferred (Luke's
explicit call when this item was scoped) -- noted below, not done here.

`--match-preview` reuses `driveTankWithAI` (the generalized form of item 7's
`updateOpponentAI`, parameterized on `Tank& self, Tank& opponent, CombatantId
selfId` with one `AiTurnState` per combatant instead of five flat opponent-
only members) for *both* tanks, so the player's manual input/fuel/fire
handling in `mainLoop` is entirely swapped out rather than merely having its
inputs zeroed -- the AI path already does the equivalent turn bookkeeping
internally, and running both would double-spend fuel or double-fire. This
is the same "pure decision logic, one live call site" split item 7
established, just proven out by reusing it for the second call site rather
than adding a new one. Verified: a full `--match-preview` run (seed 7331)
reaches a genuine "Opponent wins! Match over." with zero validation-layer
output, driving the player tank's movement/aim/fire autonomously exactly
like the opponent's own AI (confirmed live via screenshot: it drives off
its own spawn, tracks marks visible, then shows "YOUR TURN - AIM/FIRE"
with turret/power already mid-adjustment, no input given). Re-verified the
refactor didn't regress items 7/8's own opponent behavior with a repeat of
item 8's own seed 7331 frame 400 screenshot -- opponent position, fuel and
HUD state matched the earlier capture on visual inspection (not a pixel
diff).

Restart (`N`, only live once `isGameOver()`) resets `matchState_` to a
fresh `MatchState()` and re-places both tanks at spawn poses cached once in
`initialize()` (`playerSpawnPosition_`/`opponentSpawnPosition_` plus their
`glm::vec2` forwards -- `TerrainPlayability`'s spawn/secondarySpawn results
store forward as 2D, not 3D, corrected from the plan's draft signature
during implementation) -- no terrain regeneration needed. `spawnBoxes()`
seeds its own local RNG from `worldSeed_` every call (not the shared
`powerUpRng_`), so a restart's crate layout is identical to the original
match's, not merely "a fresh valid one" as originally planned -- a better
outcome, found by reading the function rather than assuming.

Found and fixed a real overlap bug during live testing, not caught by the
`hud_preview` mockup: forcing game-over artificially early (inside a test
hook, to exercise restart without playing a full match) landed within the
first-120-frame "MATCH START" window, and both banners rendered at once
(the big banner showed "MATCH START" with "PRESS N TO RESTART" stacked
underneath it -- confusing, since the caption and subtext disagreed).
Fixed by suppressing `showMatchStartBanner` outright once `isGameOver()`,
making the two banners properly mutually exclusive by construction instead
of by the draw order happening to look right. Impossible to hit in real
play (reaching game-over requires many hundreds of frames at minimum,
verified across every prior item's live testing this session) but cheap
and correct to close anyway now that a test caught it.

**Deferred, not done here:** extending `tools/benchmark_runtime.py` with a
`--match-preview` pass-through flag and actually running it in Release
across seeds 7331/0/42 to record frame-time/startup numbers alongside
`docs/TERRAIN_RUNTIME_PERFORMANCE.md`'s existing baseline. All of this
item's *functional* acceptance criteria (match start/win-lose/restart,
deterministic both-sides scripted play, clean validation) are verified in
the debug build; only the Release-specific timing measurement remains
open, as a follow-up rather than blocking this item's completion.

This closes all 9 gameplay roadmap items.

### Gameplay recommendation

Build the rules layer and the second tank first, since they expose the real
frame cost and the spawn/route constraints early. Tune ranging by feel with
the power table before adding power-ups, and keep the opponent simple until
the player's own loop is fun. Do not touch tank handling, terrain
generation or the tree system to make the match work; if a landform makes
a duel unplayable, fix it in spawn selection.

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
- Status: completed for an initial opaque GGX pass in `basic.frag` -- shared
  Trowbridge-Reitz/Smith/Schlick BRDF, per-materialType roughness/metalness/F0
  (terrain, rock, bark, tank armour/tracks/barrel, water), and a flat ambient-
  specular fill so metal parts aren't only lit by the direct sun highlight.
  Tank wear/dust/soot now drives roughness and metalness together instead of
  an ad hoc Blinn-Phong exponent. Leaf transmission is an explicit,
  pixel-identical compatibility path, untouched. Reviewed against the current
  legacy-terrain scene (tank/terrain/water/trees) with no regressions.
  Limitations: no per-pixel roughness/metal/normal textures (still per-type
  constants); the ambient-specular fill is a flat, non-directional stand-in
  for a real prefiltered environment reflection, tuned by eye rather than
  measured (the barrel's tint was pulled well below iron's literal F0 to
  avoid reading as too bright under this flat fill); terrain material
  rebuild (soil/rock exposure from erosion fields) remains separate, deferred
  work, so this pass shades the legacy terrain's existing grass/gravel split
  only.

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
- Status: the linear HDR colour target, single tonemap stage, and a resolved
  motion-vector buffer are done. `basic.frag` writes raw linear HDR color
  into a `ResolveTarget` (`R16G16B16A16_SFLOAT`, MSAA scratch + resolve, no
  ping-pong -- generalized from the initial HDR-only version so a second
  instance backs the velocity buffer too), and `TonemapPass` (fullscreen
  triangle, its own small pipeline/descriptor) maps that down to the
  swapchain's sRGB image right before the HUD draws on top. A 4th MRT
  attachment (`R16G16_SFLOAT`) carries a UV-space current-minus-previous
  motion vector, computed by reusing the existing prevViewProj reprojection
  scaffold; `Tank` now snapshots its hull/turret/barrel world matrices once
  per frame (`prevHullWorldMatrix()` etc., since that pose is a stateful
  spring/rigid-body sim, not a pure function of time like wind bending) and
  `RasterInstance` carries a `previousModel` for instanced draws. F10 toggles
  a debug visualization (velocity mapped to color via `TonemapPass`) used to
  verify it: confirmed correct via wind-driven foliage sway, camera-motion
  parallax on static ground, and the tank's own recoil kick all showing
  distinct, physically-sensible tints, with static terrain reading exactly
  zero. Camera jitter (Halton(2,3), 8-frame cycle, added to the projection's
  `[2][0]/[2][1]` terms -- kept out of the CPU-side crosshair/aim-point
  projection and the tree-LOD footprint calculation, which use an
  unjittered copy, so neither visibly wobbles) and a basic TAA resolve
  (`TaaBlendPass`: reprojects a ping-ponged HDR color history via the
  velocity buffer, clamps it into a 4-tap neighborhood color box built from
  the current frame, blends at a fixed 0.9 history weight; `TonemapPass` now
  reads this blended result instead of the raw HDR target) complete the
  item. Deliberately simple -- no variance clipping, no motion-blurred
  neighborhoods, no adaptive blend factor -- see the acceptance note below.
  F11 toggles TAA on/off for direct comparison.

  Prompted by a user report of unstable/flickering lighting; investigation
  (comparing current state, the state right after the HDR/tonemap commit,
  and the pre-session baseline) confirmed this was pre-existing ray-traced
  shadow/AO noise on the tank (worse at the original baseline, not a
  regression from this work) -- `basic.frag`'s `isTank` branch intentionally
  skips shadow/AO smoothing for moving objects, so the tank was always lit
  by a handful of raw, unsmoothed rays per frame. Verified the fix directly:
  diffing two consecutive frames of an otherwise-static scene dropped from
  a filled-in, flickering tank silhouette (mean channel diff ~1.4-2.2 with
  jitter alone, no TAA) to a thin edge-only outline (~0.57-0.76) with the
  tank's body and the ground both reading as pixel-stable in between --
  confirming TAA fixed the reported problem (broad-surface flicker) while
  leaving the well-known, common residual: sub-pixel jitter still aliases
  differently at high-contrast edges/silhouettes frame to frame, which a
  4-tap neighborhood clamp doesn't fully suppress. That residual is an
  accepted, explicitly-scoped limitation, not a bug -- the same acceptance
  note flags variance clipping/wider neighborhoods as the measured follow-up
  if it's ever not good enough, rather than a default.

  Remaining limitation, unchanged from before: the tank's wheels/track shoes
  still report zero motion (`previousModel == model`, no real rotation
  snapshot yet) -- under-represents fast wheel-spin in the TAA history, not
  a correctness bug. Verified via clean Vulkan validation output (including
  after resize) and matched `--seed 7331` screenshots showing no visible
  regression to normal rendering. Not yet done: the acceptance line's
  "measured bandwidth and memory cost" -- no Release GPU-timing/VRAM
  comparison has been taken for the new passes/buffers (HDR, velocity, TAA
  history, blend pass) against the pre-HDR baseline; worth a real
  measurement pass before treating this as fully closed out.

  Correction after initial landing: the first version (0.9 history weight,
  no sharpening) was reported as making the image "very blurry" -- confirmed
  directly with a same-frame TAA-on/off comparison, not just a subtle
  characteristic. Root cause: 4-tap min/max neighborhood clamping only
  rejects wrong colors (ghosting); it does nothing to stop the blend itself
  from softening detail, since a blurred value still falls inside a smooth
  region's local min/max range. Fixed by reducing the history weight to
  0.85 and adding a cheap unsharp-mask sharpen (reusing the clamp's own 4
  neighbor taps, no new texture reads) -- re-verified with the same
  consecutive-frame diff used to confirm the original fix: the tank/ground
  interior stays stable (thin edge-only residual, same pattern as before)
  while the image is visibly as sharp as TAA off. There's a genuine,
  expected tension between sharpen strength and edge stability (a stronger
  sharpen amplifies frame-to-frame edge noise along with real detail);
  0.15 was chosen as the point that restored sharpness without visibly
  reintroducing that noise -- a further measured tuning pass, or a sharper
  history reconstruction filter (Catmull-Rom) instead of a post-blend
  sharpen, remains a possible follow-up if 0.15 turns out not to be enough
  in practice.

  Second correction, after "glitchy/disappearing lighting when the camera
  moves, slight camera jiggle, still quite blurry" was reported: four
  distinct bugs in the motion-vector/TAA work, found and fixed together.
  (1) The dominant one -- `tree_shadow.vert` declares its own copy of the
  `RasterInstance` struct for the instance SSBO, and it was never updated
  when `previousModel` was added to the C++ struct and `basic.vert`; the
  stride mismatch made the shadow-map pass read garbage matrices for every
  instance, corrupting/removing tree and tank shadows (confirmed by a
  side-by-side against a HEAD-commit build: baseline had full soft shadows,
  the working tree had almost none). Both struct copies now carry a
  must-byte-match warning comment. (2) `TaaBlendPass` and `TonemapPass`
  each had a single descriptor set rewritten every frame from `drawFrame`
  while the other in-flight frame's command buffer could still be executing
  -- spec-illegal and intermittently glitchy; both now hold one set per
  frame in flight, written only at startup/resize (the same pattern as
  `Pipeline`'s history sets), with zero per-frame descriptor updates.
  Confirmed clean under the validation layer's synchronization-validation
  mode (the only remaining hazards are a pre-existing swapchain-acquire
  pattern, unrelated). (3) The TAA camera jitter leaked into reprojection:
  `prevViewProj_` stored the jittered matrix and velocity was computed from
  the jittered `frame.proj`, so every velocity carried the frame-to-frame
  jitter delta -- history resampled off texel-center every frame even with
  a static camera (permanent bilinear blur + visible sub-pixel wobble of
  the whole scene). `FrameUBO` gained `viewProjUnjittered`, `prevViewProj`
  is stored unjittered again, and velocity (now a shared
  `computeScreenVelocity()` helper in basic.frag) uses only unjittered
  matrices on both ends. (4) The previous fix attempt's "sentinel velocity"
  on transient effects was itself wrong -- the effect cards draw without
  discard, so writing any velocity stomps the background's across the full
  billboard quad including fully transparent texels. Reverted to the
  standard treatment: the effects pipeline write-masks the velocity
  attachment (background velocity stays underneath; neighborhood clamp
  bounds the card's own change), the unlit and sky branches write real
  reprojection velocity. Verified: shadows match the HEAD baseline,
  consecutive-frame stability diff back at the accepted magnitude, weapon
  preview frame 94 shows no smoke ghost trails, 25/25 tests pass.

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

  Status: done via the "prefer analytic... before volumetric" reading of this
  item, not sparse irradiance probes -- indirect lighting wasn't visibly
  deficient enough after this to warrant them (see acceptance's own
  conditional: "if indirect lighting remains visibly deficient"). Every
  ambient light term in `basic.frag` (diffuse fill, the metal
  `ambientSpecular` fill, `traceReflection`'s miss color) previously read a
  single flat `frame.ambientColor` constant regardless of surface
  orientation. Replaced with a new `skyAmbientTint(normal)` helper: mixes
  `frame.skyHorizon`/`frame.skyZenith` (the same two colors the visible sky
  dome and distance fog already use) by the surface normal's upward
  component, then rescales to match `frame.ambientColor`'s original luma so
  the overall fill brightness this scene was tuned against doesn't shift --
  only its color and per-surface directional variation do (upward-facing
  surfaces skew toward the zenith tone, sideways-facing toward the horizon
  tone). Reflections: `envColor` used to be a razor-sharp `skyColor(reflectDir)`
  sample at every roughness (only `effectiveReflectivity` scaled down with
  roughness, which reduces how much the sample contributes but not how sharp
  it looks). Now blends that sharp sample toward `skyAmbientTint(shadingNormal)`
  by roughness -- literally the same sky-derived value diffuse lighting
  uses, which is also the direction a fully-rough "reflection" converges
  to (scattered evenly across the local hemisphere). No prefiltered
  mip-mapped environment map: the sky here is a procedural function, not a
  captured cubemap, so there's nothing to prefilter -- this is the cheap
  analytic equivalent instead.

  Verified with `--seed 7331` reference screenshots (tank, water, landscape,
  trees) compared before/after: differences are a subtle, expected sky-tinted
  shift (mean per-channel diff ~1-8/255) with no broken materials and no
  change to non-lighting content. Water specifically (roughness 0.06, the
  only material with reflectivity > 0 today) is visually unchanged, as
  expected -- near-zero roughness barely blends away from the sharp sample.
  25/25 tests pass.

  Correction after initial landing: reported as making the scene "look a bit
  darker." Measuring mean scene luminance before/after (not just per-channel
  diff magnitude, which doesn't carry a sign) confirmed this wasn't really a
  brightness drop -- under 0.3/255 either way -- but a genuine color-cast
  problem: a straight-up surface normal mixed all the way to raw
  `skyZenith`, which is a much more saturated blue than the pale constant it
  replaced (a real sky hemisphere's average is closer to mid-elevation tones
  than the exact zenith point), losing red relative to blue. Equal luma with
  less red reads as a cooler, duller image after the ACES tonemap even
  though total light didn't decrease. Added `kSkyTintStrength = 0.4` in
  `skyAmbientTint` to blend back toward the original flat tone rather than
  fully committing to the sky-derived color -- keeps the directional,
  sky-tied variation the plan item calls for, at a strength that doesn't
  visibly cool the image. Re-verified the same way (luma comparison plus a
  fresh set of reference screenshots): red channel back close to the
  original baseline, luma still flat, 25/25 tests still pass.

  Limitation: only water currently has nonzero `reflectivity`, so the
  roughness-filtered reflection path is exercised by exactly one material
  today; it's implemented and correct for whenever another surface (e.g. a
  future wet/polished material) sets reflectivity above 0. Also unaddressed:
  the acceptance line's "bounded update latency and net acceptable GPU/memory
  cost" doesn't really apply here (no new passes, buffers, or per-frame
  update loop were added -- this is a pure per-fragment shader change with
  zero extra cost), and aerial-perspective/cloud-shadow work beyond the
  existing fog/cloud dome was judged out of scope for what "selective"
  meant here.

### Near-field ground vegetation

Add bounded grass/clump instances driven by final soil/moisture and route
fields. Fade density/detail into the ground material with distance and test
geometry versus coverage cards by overdraw and visual stability. Most blades
should not become individual ray-tracing instances. Existing tree assets stay intact.

- Priority: P2 after terrain/material integration and temporal stability.
- Acceptance: believable ground scale/contact without a carpet of shimmer,
  visible density rings or excessive raster/TLAS cost.

  Status: done, with one deliberate substitution -- this project is
  currently using the basic/legacy terrain generator only (per explicit
  direction earlier in this work), which produces no soil/moisture/route
  fields at all (`TerrainGenerator::BuildResult`'s `generationFields`/
  `erosion`/`drainage`/`water`/`streams`/`playability` are all
  `std::nullopt` on that path). Density instead follows
  `terrainGravelAmount` (the same height+slope material-blend function
  `spawnSmallRocks` already uses, inverted: grass wants LOW gravel/slope,
  scree wants high) -- the closest real signal the active generator
  actually exposes, not a real moisture field.
  New: `Mesh::grassClump`/`appendGrassBlade` (real flat-triangle blade
  geometry, not an alpha-cutout billboard card -- there's no alpha-cutout
  foliage texture/pipeline precedent anywhere in this codebase to reuse,
  and building one was judged out of scope for this pass), a
  `GrassClumpInstance` placement (mirrors `ShrubInstance`), and
  `Application::spawnGrassClumps` (grid-scanned like `spawnSmallRocks`,
  1.6-unit steps, whole terrain, baked once at load). Per-frame: frustum
  culled plus a hard 40-unit draw radius (just short of where distance fog
  starts, so the encroaching haze masks the cutoff rather than it being a
  visible edge on clear ground -- a `dedicated smooth per-instance fade
  would need a new instance field RasterInstance doesn't have; not done
  here, see limitations). Instanced/wind-bent through the same
  `RasterInstance`/`windInstance` path as shrubs (`materialType` 2,
  foliage), drawn via the main pipeline, explicitly not added to the
  ray-traced TLAS (matches the plan text exactly).
  Correction found during verification: the first version bound
  `leafMaterialSets_` (reusing tree foliage's texture, like shrubs do) and
  rendered as visibly dark, near-black spikes. Diagnosed by swapping to
  `whiteMaterialSet_` as a test -- blades turned bright, isolating the
  leaf texture's own dark/high-contrast speckle (tuned for canopy detail
  multiplied over an already-lit rounded blob, not a thin card sampled at
  a minified mip) as the actual cause, not lighting/shadow/AO as first
  suspected. Kept `whiteMaterialSet_` permanently and gave grass its own
  real green vertex color instead of a texture-dependent tint. Also bent
  each blade's flat per-triangle normal partway toward world-up (0.55) so
  a tuft doesn't have half its blades read as unlit silhouettes purely
  from which way they happen to face.
  Verified: 25/25 tests pass, clean validation output, `--seed 7331`
  screenshots (tank and landscape views) show tufts scattered plausibly
  across grassy (non-gravel, non-underwater) ground with no visible density
  ring, no crash against `Pipeline::kMaxRasterInstances` in any tested view.

  Limitations: no smooth per-instance distance fade (hard cutoff, masked by
  fog proximity rather than actually eliminated); no measured GPU/overdraw
  cost comparison against a coverage-card alternative (acceptance's "test
  geometry versus coverage cards" -- real geometry was chosen directly
  based on this codebase having no alpha-cutout foliage precedent to build
  the card version from, not from a measured comparison of both); shimmer
  under camera motion specifically is unverified (this environment can only
  capture single frames, not a moving sequence, for visual review).

  Correction after initial landing: reported as needing more color
  variation, darker especially -- the single shared `grassTint` (only
  varied per-blade within one tuft, by a small +-15% shade jitter) read as
  flat and repetitive once there was enough of it on screen to compare tuft
  to tuft. Replaced with a 6-color palette (one per mesh variant, which
  `spawnGrassClumps` already assigns uniformly at random per tuft), weighted
  toward the darker/mossier end rather than centered -- an even spread
  still read as uniformly bright/yellow-green overall, since real rough
  grass has more dark clumps mixed in than pale ones. Also widened the
  per-blade shade jitter (0.85-1.15x to 0.7-1.3x) for more variation within
  a single tuft. Re-verified with fresh `--seed 7331` screenshots: a clear
  mix of dark, medium and pale/yellow tufts now visible side by side, 25/25
  tests still pass.

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
changes. Add renderer diagnostics when needed for development; match HUD
elements (turn, fuel, health, power, inventory) are specified under
[turn camera and match HUD](#turn-camera-and-match-hud).

- Priority: P1 compatibility during HDR/upscaling; broad UI redesign stays P3.
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

## Reference material

`docs/` holds local working notes: measurements, previews, and raw data
generated by `tools/` and the test suite while investigating the items above.
It is gitignored and not committed; regenerate it as needed rather than
expecting it in git history. The conclusions that matter are already folded
into the prose above. What follows is the material that only ever lived in
those notes and would otherwise be lost.

### Evidence required to promote an experiment

Before making any experimental technique the default: record the visible
problem it addresses, the simplest applicable technique, its prerequisites,
before/after Release-build timings, and accept only after clean validation
output and matching reference screenshots.

### Terrain correctness gates

Hydraulic terrain work must additionally satisfy: no NaNs or negative
water/soil/sediment values; pass fixed test terrains (flat, slope, bowl,
ridge, confluence); reproduce the 16 fixed regression seeds; keep GPU/CPU
determinism tolerances separate; confirm channel/bank edits move material
rather than teleporting it; and finish with clean Vulkan validation output.

### Research references

- Mei, Decaudin & Hu (2007) — virtual-pipe hydraulic erosion; basis for the
  erosion prototype.
- Barnes, Lehman & Mulla (2014) — Priority-Flood; basis for drainage and
  basin resolution.
- Jákó & Tóth (2011) — combined hydraulic/thermal relaxation; basis for the
  soil relaxation pass.
- Filament's material model — target for the physically based material
  foundation.
- GPUOpen's FSR2 integration guide — reference for temporal upscaling.
- jcgt.org's sparse dynamic irradiance-field paper — reference for the
  selective advanced-lighting candidate.
- The Vulkan spec's acceleration-structure update section — reference for
  the ray-tracing TLAS refit candidate.
