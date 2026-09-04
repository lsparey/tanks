# Project Plan

This document records the project's delivered foundations and possible
follow-up work. The progress index is the source of truth for task status;
tick an item only after its implementation has been completed and verified.
Unchecked items are options rather than a committed roadmap.

## Progress index

### Completed foundations

- [x] [Core Vulkan renderer](#core-renderer)
- [x] [Procedural world and environment](#procedural-world-and-environment)
- [x] [Tank, weapons, and interaction](#tank-weapons-and-interaction)
- [x] [Lighting, materials, and effects](#lighting-materials-and-effects)
- [x] [English temperate grass palette](#english-temperate-grass-palette)
- [x] [Tank movement and suspension physics](#tank-physics-checkpoint)
- [x] [Independent tread marks and track dust](#tank-physics-checkpoint)
- [x] [Existing performance foundation](#existing-performance-foundation)

### Tank and physics candidates

- [ ] [Gun recoil](#gun-recoil)
- [ ] [Hull-shaped collision](#hull-shaped-collision)
- [ ] [Surface-dependent traction](#surface-dependent-traction)
- [ ] [Animated tracks](#animated-tracks)
- [ ] [Physics tuning tools](#physics-tuning-tools)
- [ ] [Dynamic props](#dynamic-props)
- [ ] [Full suspension and airborne simulation](#full-suspension-and-airborne-simulation)

### Performance candidates

- [ ] [Extend performance instrumentation](#extend-performance-instrumentation)
- [ ] [Instance decals and short-lived effects](#instance-decals-and-short-lived-effects)
- [ ] [Remove the cross-frame CPU history wait](#remove-the-cross-frame-cpu-history-wait)
- [ ] [Refit the ray-tracing TLAS](#refit-the-ray-tracing-tlas)
- [ ] [Reuse per-frame CPU scratch storage](#reuse-per-frame-cpu-scratch-storage)
- [ ] [Add scalable ray-tracing quality presets](#add-scalable-ray-tracing-quality-presets)
- [ ] [Dynamic resolution or upscaling](#dynamic-resolution-or-upscaling)
- [ ] [GPU-driven visibility and indirect drawing](#gpu-driven-visibility-and-indirect-drawing)

### Visual candidates

- [ ] [Establish visual targets](#establish-visual-targets)
- [ ] [Weapon firing presentation](#weapon-firing-presentation)
- [ ] [Tank material detail and wear](#tank-material-detail-and-wear)
- [ ] [Foliage and environmental motion](#foliage-and-environmental-motion)
- [ ] [Unified sky, sun, and atmosphere](#unified-sky-sun-and-atmosphere)
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
- Placement rules that account for water, spawn clearance, spacing, slope,
  scale, and reusable visual variants.

### Tank, weapons, and interaction

- Imported multipart tank model with separately rendered hull, tracks,
  turret, and barrel materials.
- Player driving, independent turret traverse, follow camera, and free camera.
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

## Tank physics checkpoint

The current tank movement is a solid foundation and is a reasonable place to
stop pending wider playtesting. It currently includes:

- Fixed-step movement simulation.
- Momentum, acceleration, braking, drag, and rolling resistance.
- Differential-track steering and stationary pivot turns.
- Forward/reverse speed differences and speed-dependent steering.
- Terrain slope forces, lateral traction, and a maximum climbing angle.
- Velocity-aware obstacle sliding and stable boundary collisions.
- Four-contact visual suspension with pitch, roll, and heave damping.
- Acceleration squat, braking dive, and restrained cornering lean.
- Independent left/right tread marks and track dust.
- Mark and dust intensity driven by acceleration, slip, turning, and contact.

Further movement work should be driven by a specific issue found during
playtesting rather than added for completeness.

## Candidate improvements

### Gun recoil

Animate the barrel backward when firing and return it with a damped spring.
Add a small chassis impulse and, if it feels appropriate, subtle camera
feedback.

- Value: high visual and tactile payoff.
- Complexity: low to medium.
- Suggested priority: best next self-contained improvement.

### Hull-shaped collision

Replace the circular tank footprint with a capsule or oriented rectangle. This
would better represent the long hull when scraping rocks, approaching corners,
or passing through narrow spaces.

- Value: high if current obstacle contacts feel awkward; otherwise modest.
- Complexity: medium.
- Suggested priority: address in response to collision-related playtest issues.

### Surface-dependent traction

Expose the terrain's visible surface classification to gameplay so grass,
gravel, rock, mud, or wet ground can affect acceleration, braking, lateral
grip, tread darkness, and dust production.

- Value: adds handling variety and connects physics to the environment.
- Complexity: medium.
- Suggested priority: useful when the terrain types become meaningful to play.

### Animated tracks

Scroll track texture coordinates according to each track's speed and direction,
including opposing motion during pivot turns. The current model may require its
left and right track geometry or material data to be separated first.

- Value: visual polish most noticeable near the tank.
- Complexity: medium, depending on the model's UV and mesh layout.
- Suggested priority: after core gameplay and camera presentation are settled.

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
- Trees and rocks use three projected-size LODs with hysteresis.
- Ray-tracing geometry uses simplified proxies where appropriate, and
  numerous minor effects are excluded from the TLAS.
- Shadow and AO ray counts decrease with distance and use temporal
  accumulation to recover quality.
- Two frame-in-flight resource slots are already available.

### Extend performance instrumentation

Add CPU timings for simulation, visibility grouping, TLAS instance gathering,
command recording, fence waits, and presentation. Record visible-instance and
draw-call counts, plus rolling average and worst-frame percentiles. Keep a
small set of repeatable camera/gameplay scenarios for before-and-after checks.

- Value: identifies whether the next limit is CPU submission, ray queries,
  acceleration-structure work, or ordinary rasterization.
- Complexity: low.
- Suggested priority: always measure before selecting a larger optimization.

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
beyond the current scene; at present, CPU culling plus instancing is simpler
and likely sufficient.

- Value: scales to much denser environments.
- Complexity: high.
- Suggested priority: defer unless profiling shows CPU visibility/submission
  becoming a bottleneck as scene density grows.

### Performance recommendation

Keep the current renderer until a representative scene misses its target frame
budget. When optimization resumes, first extend the existing profiler with CPU
wait and draw-count measurements. The likely first implementation candidate is
instancing track marks and other effects; synchronization and TLAS changes
should follow only when their measured timings justify the additional risk.

## Visual improvements

The current presentation already includes procedural terrain and materials,
clouds and distance fog, ACES-style tonemapping, ray-traced soft shadows/AO and
selected reflections, depth-aware reflective water, dynamic muzzle/explosion
lighting, debris, smoke, dust, and independent tread decals. Future work should
strengthen a chosen art direction and improve motion/readability rather than
add effects indiscriminately.

### Establish visual targets

Collect a small reference board and define a few visual pillars, such as
grounded realism, stylised military diorama, or high-contrast arcade action.
Capture several fixed in-game viewpoints for repeatable comparisons when
changing lighting, materials, atmosphere, or post-processing.

- Value: keeps otherwise-good effects visually coherent and prevents endless
  local tweaking.
- Complexity: low.
- Suggested priority: do before a broad visual-polish pass.

### Weapon firing presentation

Build on the existing muzzle flash, smoke, shell, impact light, and debris with
barrel recoil, spring return, a brief chassis kick, shaped muzzle flame, and
restrained camera impulse. Scorch or impact decals could make hits persist
after the short-lived particles disappear.

- Value: high; firing is a frequent focal action and currently offers the
  clearest opportunity for stronger visual feedback.
- Complexity: low to medium for recoil, medium for persistent impact decals.
- Suggested priority: strongest self-contained visual improvement. See also
  the earlier **Gun recoil** item.

### Tank material detail and wear

Give painted metal, bare tracks, and the barrel more distinct surface response
through packed roughness/metalness or material masks rather than relying mainly
on uniform per-part constants. Add restrained edge wear, soot near the muzzle,
dust accumulation on lower surfaces, and possibly track-driven mud buildup.

- Value: improves the main object at every camera distance where detail is
  visible.
- Complexity: medium; procedural masks can avoid requiring a full new asset
  pipeline.
- Suggested priority: after the overall visual target is chosen.

### Foliage and environmental motion

Add coherent wind animation to branches, leaves, shrubs, and grass shading.
Use low-frequency world-space gusts with smaller high-frequency leaf motion so
the environment moves as one weather system rather than as unrelated wobbling
objects. Dust and clouds can follow the same wind direction.

- Value: makes an otherwise-static landscape feel alive and improves motion
  cues when the tank is stationary.
- Complexity: medium.
- Suggested priority: high if environmental stillness is noticeable.

### Unified sky, sun, and atmosphere

The rasterized cloud dome and analytic reflection sky currently use related but
separate implementations. Drive both from shared sky parameters or a generated
environment texture, add a visible sun disk, and tie sky hue, fog colour,
directional-light colour, and cloud lighting to the same time-of-day state.
Optional moving cloud shadows would further connect the sky to the terrain.

- Value: improves scene-wide cohesion, particularly in water reflections and
  at the horizon.
- Complexity: medium; dynamic time of day and cloud shadows increase it.
- Suggested priority: useful before adding more isolated atmospheric effects.

### Shorelines and terrain transitions

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

Choose an explicit visual target before attempting a general polish pass. The
best immediate package is weapon recoil plus restrained camera response because
it strengthens the prototype's central action using systems already present.
After that, unified sky/lighting and coherent foliage wind offer the largest
scene-wide improvement; material, shoreline, particle, and post-processing
work should be selected from actual screenshot and playtest weaknesses.
