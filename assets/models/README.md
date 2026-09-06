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

The multi-view revision has 5,652 authored positions and 9,942 triangles. Objects are
named individually, including left/right tread shoes, six road wheels per
side, raised end wheels, hubs, deep skirts, armour, hatches, sights, aerials,
rear auxiliary drums, and barrel sections.
Keep these six material names, which the runtime merges into five meshes:

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
```

This standard-library-only tool renders the authoring mesh orthographically,
using the same coordinates as the supplied reference (at double resolution,
with ten reference pixels of margin above and below). Flat shading removes
terrain, perspective and camouflage as distractions when comparing proportions.
It is an inspection preview, not an in-game render or a replacement for testing
the exported OBJ. A subdued depth-step outline helps distinguish fittings in
these previews; it is not an in-game rendering effect. See the
[inspection views](../../docs/VISUAL_TARGET.md#tank-geometry-checkpoint).

## Deliberate limits

Wheels and track shoes are real geometry but **static**. They are named for
future animation but merged at load time; per-side belt motion, rotating
wheels, suspension articulation and dedicated track UVs remain separate work.
Small sprocket teeth, bolts and flexible skirts are omitted at this detail
level. Launchers, lamps and optics are static details, not new gameplay systems.
The current camouflage is retained; desert paint, markings and photographic
weathering from the new references are intentionally not copied.
