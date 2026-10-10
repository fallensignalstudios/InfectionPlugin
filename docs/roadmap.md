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

## Phase 3: Unreal Engine 5.8 plugin

- Editor plugin compiling this core directly: pick a static mesh, place seeds in the viewport, set the sliders, generate.
- Writes `UStaticMesh` assets with Nanite enabled; no export or import step.
- `M_EclipseBiomass` master material: clear-coat carapace, subsurface violet, vein and seam emission, a pulse, and a growth parameter driven by the arrival channel.

## Phase 4: skeletal hosts

- Corrupt skeletal meshes; every new vertex and growth inherits skin weights from the nearest host vertex, so it animates with the rig.

## Phase 5: live spread in levels

- Runtime component that grows corruption during play using the baked arrival data, the PCG framework and instanced growths.
