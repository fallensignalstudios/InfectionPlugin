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

## Later

- Strands that swing (physics or AnimDynamics) on characters.
- Seeding new outbreaks at runtime, growing on the fly where the player is.
- Permanent, unlimited burn scars (a render-target mask instead of four spheres).
- Tearing the host silhouette at late stage.
- Cutting into the host: a portal or burrow carved into a wall, not only grown onto it.
