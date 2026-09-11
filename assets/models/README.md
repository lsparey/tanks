# Tank assets

`challenger2.obj` is the default, simplified Challenger 2 model. Its revised
side silhouette is based on the user-supplied 600×190 profile: hull ends at
image X=156/554, ground at Y=181, turret roof at Y=57, and gun axis at Y=84.
The generator records these landmarks and converts them into game units.
The front/rear remodel uses the subsequently supplied large reference images,
and the roof layout uses the supplied top drawing. Broad cheeks, rear grilles,
paired drums, asymmetric hatches, optics and engine access panels are now
represented. These references differ in equipment fit/perspective, so this
remains a simplified interpretation, not a dimensionally exact replica.
The isometric references add surface-following roof access panels, a chamfered
loader hatch with gasket and hinges, cupola lid hardware, bolted front roof
applique, shoulder seams, tie-downs, framed cooling grilles, rear tool bins,
tow cables and hull access caps. The original armour cross-sections, barrel,
skirts and running gear are retained. Raised deck hardware uses HullFittings
to preserve the hull collision bounds.
The complete turret, gun and attached fittings are raised by 0.08 model units
from those profile landmarks to expose a small hull/armour gap. The mounting
ring extends down to its original hull seating; gun pivots follow the imported
barrel geometry automatically. Adjust `TURRET_RISE` in the generator to tune it.
The [British Army's Challenger 2 overview](https://www.army.mod.uk/learn-and-explore/equipment/combat-vehicles/challenger-2/)
provides additional vehicle context. No third-party mesh or texture is incorporated.

The original `tank.x` is unmodified. Compare with `--model original` or
`--model refined` using the same seed and view. Try `--view tank-side` and
`--view tank-front`, `--view tank-rear` and `--view tank-top` to inspect the model.

## Editing

Import `challenger2.obj` into a mesh editor such as Blender, retaining its MTL
file beside it. The asset uses +Y up, +Z forward, ground at Y=0, and a turret
yaw pivot on the origin's vertical axis. Choose matching axes on import/export;
apply object transforms to mesh coordinates when exporting. Units match the
existing game tank rather than real-world metres.

The deterministic authoring source uses Python 3's standard library:

```bash
python3 tools/generate_tank_model.py
```

This regenerates (overwrites) `challenger2.obj` and `challenger2.mtl`. Preserve
manual editor changes separately, or edit the script for reproducible changes.
Use `--output-dir /tmp/tank-model-preview` to generate a separate copy.
Neither Python nor Blender is required to build or run the game.

The rear cooling banks use four broad slats per panel, with simple frames and
no crossing spines or small hinges. The rear turret vents use three wider
slats each. This reduces fine overlapping edges at shallow rear camera angles.

The current revision has 6,732 authored positions and 11,822 triangles. Objects are
named individually, including left/right tread shoes, six road wheels per
side, raised end wheels, hubs, deep skirts, armour, hatches, sights, aerials,
rear auxiliary drums, and barrel sections.
Keep these six material names. The runtime extracts the named moving gear,
then merges the remaining objects into five rigid meshes:

| Material | Transform and finish |
| --- | --- |
| Base | Hull transform; painted armour, wheel discs and hubs |
| HullFittings | Merged with Base; painted drums/fittings excluded from collision bounds |
| Turret | Turret yaw; painted armour and roof fittings |
| TurretDark | Turret yaw; dark optics, cheek panels, launchers and rear grilles |
| Barrel | Turret yaw, elevation and recoil; mantlet, tube and muzzle |
| Tracks | Hull transform; dark tires, belts, shoes, mud flaps and fixed hardware |

The renderer generates flat face normals and tiled planar UVs, replacing the
MTL preview colours with its existing camouflage/metal textures. The barrel
must remain aligned with +Z, with a symmetric X/Y bound around its bore and
minimum Z at the trunnion; the muzzle is centred at maximum Z. Mesh detail
does not become individual physics colliders: the existing hull capsule and
ground-contact system remain in use, with approximately the original footprint.
Rear auxiliary drums do not enlarge that hull collision footprint.

## Profile check

```bash
python3 tools/preview_tank_profile.py /tmp/tank-profile.png
python3 tools/preview_tank_profile.py /tmp/tank-front.png --view front
python3 tools/preview_tank_profile.py /tmp/tank-rear.png --view rear
python3 tools/preview_tank_profile.py /tmp/tank-top.png --view top
python3 tools/preview_tank_profile.py /tmp/tank-iso-front.png --view iso-front --clay
python3 tools/preview_tank_profile.py /tmp/tank-iso-rear.png --view iso-rear --clay
python3 tools/preview_tank_profile.py /tmp/tank-rear-raised.png --view rear-raised --clay
```

This standard-library-only tool renders the authoring mesh orthographically,
using the same coordinates as the supplied reference (at double resolution,
with ten reference pixels of margin above and below). Flat shading removes
terrain, perspective and camouflage as distractions when comparing proportions.
It is an inspection preview, not an in-game render or a replacement for testing
the exported OBJ. A subdued depth-step outline helps distinguish fittings in
these previews; it is not an in-game rendering effect. See the
[inspection views](../../docs/VISUAL_TARGET.md#tank-geometry-checkpoint).
The two isometric views show the roof/deck surface details; `--clay` uses
neutral grey inspection materials and works with every view.

## Deliberate limits

Wheels and shoes now animate independently per side, using five shared mesh
batches (shoes, road tires, road discs/hubs, end tires, end discs/hubs). All 128
shoes circulate around a convex path extracted from each belt. The belt body
stays fixed as backing geometry; visible shoes move. Wheel radius comes from
the imported tire bounds. Mirrored wheel placement retains outward-facing hubs.
Templates reuse the right-side source geometry and UVs, so repeated wheels
share their paint pattern. Preserve the existing object names, 64 shoes per
side, six road wheels and two end wheels per side when regenerating the asset.
An incomplete named rig reports a load error rather than dropping geometry.

The same transforms drive rasterization and ray tracing. Colliders retain the
original full hull bounds; shoe/wheel movement does not affect handling.
`--static-tracks` disables extraction/animation for A/B checks. `tank.x` has no
named rig and remains a supported static fallback.

Individual suspension articulation, track sag and powered wheelspin are not
implemented. Motion follows resolved hull travel, not engine/throttle demand.
Small sprocket teeth, wheel bolts and flexible skirts are omitted at this detail
level. Launchers, lamps and optics are static details, not new gameplay systems.
The current camouflage is retained; desert paint, markings and photographic
weathering from the new references are intentionally not copied.

## Realism roadmap and future asset work

The [rendering roadmap](../../docs/RENDERING_ROADMAP.md) prioritises shared PBR
materials and temporal stability before blanket mesh subdivision. The current
material finishes are approximations; a future migration should map painted
armour to dielectric coating, expose metal only where appropriate, and keep
rubber distinct. Preserve authored scale, rig names, pivots, camouflage masks
and static/animated comparisons during that migration. Filter fine normal and
roughness detail so highlights do not sparkle at driving distance.

Visual per-wheel contact and limited track adaptation are P2 work after eroded
ground sampling is correct, separate from full suspension/airborne physics.
Update raster and ray transforms together and review wheel/track intersections.
Limited visual track sag is part of that P2 investigation; powered wheelspin,
flexible-skirt physics and full contact dynamics remain P3 gameplay work.
Additional fittings or improved authored normals/UVs should address visible
silhouette or shading defects at actual screen size. Existing rig/material
names describe today's compatibility contract; a future intentional change
must update the loader, generator and regression tests together.

Use normal animated gameplay for runtime budgets and `--static-tracks` only
for controlled comparisons. The terrain overhaul does not require a new tank
model, cinematic camera effects or per-shoe physics.
