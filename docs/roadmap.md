# Roadmap

## Phase 1: core growth (done)

- Spread over any closed or open mesh, seeded, with coverage, gravity and crease bias.
- Flow-accumulation vein network, smoothed off the vertex grid.
- Anisotropic carapace plates with leaking seams.
- Surface displacement and per-vertex corruption data.
- glTF 2.0 export (passes the Khronos validator), OBJ in and out.
- Built-in test hosts (mannequin, corridor) and a software look-dev renderer.
- Deterministic and compiler-independent; tests cover each stage.

## Phase 2: growths (geometry added on top of the surface)

- **Vein tubes**: the vein curves as real geometry, so veins stay crisp at any mesh density.
- **Spikes and thorns** on ridges and crests, oriented along the ridge.
- **Nodules and eyes** in hollows, with emissive cores.
- **Hanging strands and drips**: gravity-relaxed curves from overhangs, and catenary bridges across gaps.
- **Membranes** stretched across concave gaps (armpits, corners, doorways), perforated with holes.
- **Late-stage fraying** of the silhouette.

## Phase 3: Unreal Engine 5.8 plugin

- Editor plugin compiling this core directly: pick a static mesh, place seeds in the viewport, set the sliders, generate.
- Writes `UStaticMesh` assets with Nanite enabled; no export or import step.
- `M_EclipseBiomass` master material: clear-coat carapace, subsurface violet, vein and seam emission, a pulse, and a growth parameter driven by the arrival channel.

## Phase 4: skeletal hosts

- Corrupt skeletal meshes; every new vertex and growth inherits skin weights from the nearest host vertex, so it animates with the rig.

## Phase 5: live spread in levels

- Runtime component that grows corruption during play using the baked arrival data, the PCG framework and instanced growths.
