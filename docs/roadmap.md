# Roadmap

## Phase 1: core growth (done)

- Spread over any closed or open mesh, seeded, with coverage, gravity and crease bias.
- Flow-accumulation vein network, smoothed off the vertex grid.
- Anisotropic carapace plates with leaking seams.
- Surface displacement and per-vertex corruption data.
- glTF 2.0 export (passes the Khronos validator), OBJ in and out.
- Built-in test hosts (mannequin, corridor) and a software look-dev renderer.
- Deterministic and compiler-independent; tests cover each stage.

## Phase 2: growths (done)

- **Vein tubes** along the vein curves, crisp at any mesh density.
- **Thorns** on ridges and plate crests, curved along the growth direction.
- **Eyes and sacs** in hollows, with emissive pupils or crowns.
- **Hanging strands and drips** from overhangs.
- **Bridges**: sagging tendrils across gaps between facing surfaces.
- **Torn membranes** between neighbouring bridges, two-sided, with glowing hole rims.
- Growth density and size scale with tissue age, so the oldest corruption is the most overgrown.
- Surface and growths export as separate meshes in one `.glb`.

Not yet: the full late-stage **shredding of the silhouette** (the wraith reference),
where the host's own outline tears into hanging fringe. The strands thicken
toward that look at high coverage, but tearing the host surface itself is a
later step.

## Phase 3: Unreal Engine 5.8 plugin (written, not yet compiled against the engine)

- The repository is the plugin. `EclipseCore` compiles the generator sources directly; one copy serves the plugin, CLI and tests.
- **Eclipse Growth** panel (Tools menu): every setting in editor units, remembered per project; Grow on Selected, Add Seed, Rebuild Materials.
- **Eclipse Seed** actor marks where corruption starts; editor-only.
- Hosts are read from the static mesh's source geometry in world space, welded across seams and subdivided to a target resolution.
- **Overlay** output (a corrupted shell over the untouched host, which keeps its materials) or **Replace** output.
- Static mesh assets written with Nanite on, attached to the host and replaced on each grow; undoable placement.
- `M_EclipseBiomass` built in code: clear-coat carapace, inflamed front, pulsing violet glow, and a `Growth` parameter that replays the spread.
- In the core: vertex welding, crack-free adaptive subdivision and shell extraction, each tested; `--overlay`, `--weld` and `--max-edge` on the CLI.

## Phase 4: skeletal hosts (written; core tested, engine side not yet built)

- Bone weights are carried through welding (first of the merged vertices), subdivision (midpoint blends, exact along edges) and shell extraction (the source vertex's weights).
- Growths are skinned from the nearest host tissue by an inverse-square blend of up to four vertices, so spans between limbs stretch.
- Tested with a two-bone rig on the mannequin: the shell and the growths on a swung arm keep their exact offsets from the skin they grew on, and growths on the torso stay put.
- In Unreal: skeletal hosts are read in their reference pose with their weights; output is a skeletal mesh on the host's skeleton (a one-LOD copy of the host), shown through **Eclipse Follower** components that copy the host's pose.

Next on characters: strands that swing (physics or AnimDynamics), carrying morph targets.

## Phase 5: live spread in levels (written; core tested, engine side not yet built)

- Each grow writes an infection map (where and when, sampled every 5 cm × feature scale) and adds an **Eclipse Spread** component to the host.
- The component plays the spread forward or back at any speed with events at either end, cuts the corruption back with up to four burn spheres that smoulder at the edge, and answers "how infected is this spot now?" for gameplay.
- The material gains burn spheres (distance taken by the engine's large-world-safe Distance node) and an ember glow.

## Art pass: toward the designer's corridor

- Roots, hearts and maws, vertebrae and long-tailed scythe horns; dark raised veins; glow kept to cores, eyes and a few seams.
- Defaults retuned to match (flatter, longer plates; quieter seams; a dark bruise at the front instead of magenta).

## Realism pass

- **Soft tissue** is its own mesh with its own subsurface-scattering material (`M_EclipseFlesh`): hearts, throats, eyes, sacs, membranes and drips are wet and translucent; carapace stays clear-coated.
- **Fusion**: the surface swells up around every growth's base, so growths rise out of the tissue.
- **Maws** are real craters: lumpy lips, a ribbed throat glowing from the bottom, teeth on the rim, claws curling over; other growths keep out.
- **Sac clusters** in folds and corners.
- **Detail maps** generated in code (chitin scales and striations, flesh folds and pores), tiled triplanar over the bind pose so they stay put on animated characters; cavity darkening; raw tissue in the seams.
- The seed's core light no longer smears along creases.
- **Organic shapes**: knotted roots and beaded strands, unevenly ringed horns and claws, lumpy sacs, orbs and vertebrae (one Roughness setting); tubes resample themselves so the detail never aliases.
- **Branching**: rootlets fork off the roots; big horns carry hooked barbs.

## Realism pass 2

- **Colour variation**: bruised patches through the carapace; horn, claw, tooth and barb tips paling toward bone (a tone channel, `TEXCOORD_1.y`).
- **Halo**: clean tissue ahead of the front darkens and grows a generated capillary network, denser near the front, moving with the spread during play. Overlay shells extend past the front to carry it; the material dithers it into a soft wash.
- **Slime**: beaded mucus strings from horns and claws to the tissue below, and across tight creases.
- **Nanite displacement** (opt-in): the carapace detail pushed out into geometry by tessellation.
- The preview renderer now actually shows soft tissue, tone and the halo (it had been dropping those attributes).

## Formations and presets

- **Formations**: spires, ribcages, pods and shelf fungus, large set pieces placed before the smaller growths so those keep out of their space.
- **Presets**: Eclipse, Creep, Thornfield, Rootmass, Hive, Carcass, Cathedral; in the CLI (`--preset`) and the panel (a dropdown that rewrites the look settings).

## Rooms and art direction

- **Scene growth**: separate meshes that touch grow as one surface (contact links between pieces), split back per mesh for output.
- **Mask volumes**: Block, Bare and Dense boxes and spheres with soft edges; the Eclipse Mask actor in Unreal.
- **Variations**: Previous and Next Variation in the panel; `--variations N` in the CLI.

Next here: painting masks directly on surfaces (vertex paint) as well as volumes.

## Runtime features (done)

- **Outbreaks**: `StartOutbreak` reveals the grown corruption outward from a hit point, its front following the corruption's own arrival order and glowing as it advances.
- **Permanent burn scars**: 64 kept apart, then merged into enclosing spheres so none ever heal; burns and outbreaks reach the material through an event texture relative to the owner, safe in large worlds.
- **VFX and audio hooks**: `OnBurned`, `OnOutbreakStarted`, `OnOutbreakFinished` with location and radius.
- **Gameplay sensing**: the Eclipse Infection world subsystem (query, burn and start outbreaks anywhere without knowing the actor) and the Eclipse Infection Sensor component (enter, leave and per-check events for damage, audio and AI).

Next here: growing new corruption on the fly onto meshes that were never grown (a runtime generator call off the game thread).

## Production hardening (done)

- **Multiplayer**: the Spread component replicates growth, playback, burns and outbreaks; late joiners place each outbreak's front from its server start time.
- **Save games**: `GetState` / `RestoreState` with an `Eclipse Spread State`.
- **Remove from Selected** in the panel (and `UEclipseEditorLibrary::RemoveFromSelection`): undoable clean-up that shows hosts a Replace grow hid.
- A `LogEclipse` log category, a changelog, and the plugin version in step with it.
- **LODs** on generated meshes without Nanite and on characters; **collision** (None, Traces, Block) for the surface and the growths, with a physical material.

## Later

- Strands that swing (physics or AnimDynamics) on characters.
- Tearing the host silhouette at late stage.
- Cutting into the host: a portal or burrow carved into a wall (maws are raised craters for now).
