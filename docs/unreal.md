# Using the plugin in Unreal Engine 5.8

This repository *is* the plugin: `InfectionPlugin.uplugin` sits at its root and
Unreal only looks inside `Source/` and `Resources/`. The CMake project, the
CLI, the tests and the docs alongside them are ignored by the engine.

## Install

1. Your project needs to be a C++ project. If it is Blueprint-only, add any C++
   class once through **Tools → New C++ Class** to convert it.
2. Clone this repository into the project's `Plugins` folder:
   ```
   cd <YourProject>/Plugins
   git clone https://github.com/fallensignalstudios/InfectionPlugin.git
   ```
3. Right-click the `.uproject` file and choose **Generate Visual Studio project files**.
4. Build the editor target from Visual Studio (Development Editor, Win64), or just
   open the project and accept the prompt to rebuild the missing modules.
5. In the editor, check **Edit → Plugins → Procedural → Infection (Eclipse Biomass)** is enabled.

The plugin has three modules:

| Module | Type | What it holds |
|---|---|---|
| `EclipseCore` | Runtime | The generator itself: the same plain C++ sources the CLI and tests build. Its API header includes Unreal's platform definitions only during an Unreal build. |
| `InfectionRuntime` | Runtime | The **Eclipse Seed** actor (editor-only, stripped from cooked builds). |
| `InfectionEditor` | Editor | The **Eclipse Growth** panel, mesh conversion, asset writing and the materials. |

## Grow corruption

1. Open **Tools → Eclipse Growth**.
2. Select a static mesh actor in the level and press **Add Seed**, or drag an
   **Eclipse Seed** in from the Place Actors panel. Move the seed to where the
   infection should start. Several seeds grow several outbreaks that merge.
   A seed affects a mesh when it is within 1 m of the mesh's bounds.
3. Select the mesh (or several) and press **Grow on Selected**.

The generated actors are attached to the host, labelled `<Host> Eclipse_Shell`,
`<Host> Eclipse_Growths` and `<Host> Eclipse_Flesh` (the soft tissue), and
replaced each time you grow again. Their
meshes are written to `<Output Folder>/Generated` and are unsaved until you
**Save All**. Undo removes the placed actors; the generated assets stay until you delete them.

### If an earlier version was used

The panel remembers its settings, so values from before the art pass (plate
thickness, stretch, seam glow) stay as they were. Right-click the panel's
categories and **Reset to Default** to pick up the new look, and press
**Rebuild Materials** once: it regenerates the carapace material with the
detail maps and the tissue in its seams. (The soft-tissue material and the
detail textures are created on the next grow even without it.)

### Output modes

- **Overlay** (default): only the corrupted part of the surface is generated,
  as a shell 1.5 mm above the host. The host keeps its own materials wherever
  the corruption has not reached. Use this for real assets.
- **Replace**: the whole host is regenerated and the original is hidden; clean
  areas use the material's *Host Color*. Handy for untextured blockouts.

### Resolution

Corruption detail lives on vertices, so the host is subdivided until no edge is
longer than **Resolution** (default 1 cm) before growing. Large environment
meshes at 1 cm can reach millions of vertices; raise Resolution to 2–4 cm for
architecture, and **Feature Scale** to about 2. Growing stops with a message
rather than exceed **Max Vertices**.

Vertices that are split along UV or smoothing seams are welded first, so the
spread crosses those seams. Generated meshes have Nanite enabled by default.

**Fusion** (Growths) sets how much the tissue swells up around each growth's
base; 0 leaves growths sitting on the surface. **Roughness** sets how knotted,
ringed and lumpy growths are; 0 makes them smooth and round and about halves
their triangles, worth it on skinned characters without Nanite. **Root
Branching** and **Horn Barbs** control the side rootlets and the hooked thorns
on big horns. **Sacs** and **Sac Chance**
control the pod clusters that gather in folds and corners.

## Characters (skeletal meshes)

Select an actor with a skeletal mesh and grow as usual. The corruption is
grown over the mesh's **reference pose**, so keep the character in its
reference pose while placing seeds (no animation assigned in the editor), and
seeds go where the corruption should start on that pose.

The result is two skeletal meshes on the host's own skeleton,
`SK_Eclipse_<Host>_Shell`, `SK_Eclipse_<Host>_Growths` and `SK_Eclipse_<Host>_Flesh`, each a copy of the
host mesh reduced to one LOD and one material. Every vertex carries bone
weights: the shell takes the weights of the skin it lies on, and growths blend
the weights of the tissue nearest them, so a strand follows the limb it hangs
from and a membrane between arm and torso stretches with both.

In the level they are added to the host actor as **Eclipse Follower**
components, attached under its skeletal mesh. A follower copies its parent's
pose every frame, so the corruption plays whatever animation the character
plays. To use it on a character Blueprint, add an *Eclipse Follower* component
under the character's Mesh and set its Skeletal Mesh Asset to the generated mesh.

Limits for now:
- Hosts with clothing assets are refused; grow on a copy without clothing.
- Growths are rigid with the bone that carries them: strands do not swing or
  hang with gravity as the character moves. Physics on strands is a later step.
- The host's morph targets are not carried over to the corruption.
- Growing places followers on level actors. On a character Blueprint whose
  skeletal mesh is added in the Blueprint itself (not the inherited Mesh),
  the construction script may rebuild that component and orphan the
  followers; add the follower in the Blueprint instead, as above.

![A grown arm swung down through the transferred weights](previews/skinned_pose.jpg)

## Spreading, receding and burning during play

Every grow also adds an **Eclipse Spread** component to the host actor and
writes `IM_Eclipse_<Host>`, a sparse record of where the corruption is and when
each part of it arrived. At runtime the component drives every Eclipse material
on the host and on the actors attached to it.

![The spread playing out from two seeds, then a burn cutting it back](previews/spread_and_burn.jpg)

| On the component | |
|---|---|
| `Growth` | How far the spread has played: 0 nothing, 1 the corruption as grown, `FullGrowth` (set from Vein Reach when grown, about 1.5) everything including the veins that ran ahead. |
| `bSpreadOnBeginPlay`, `SpreadDuration` | Start from nothing when play begins and take this many seconds to finish. |
| `StartSpread`, `Recede`, `SetPlayRate`, `SetGrowth` | Drive it from Blueprint: play forward, play backward, any speed, or jump. |
| `OnSpreadFinished`, `OnFullyReceded` | Events at either end. |
| `Burn(Location, Radius)`, `ClearBurns` | Cut the corruption away in a sphere; the edge smoulders with ember light (`EmberColor`, `BurnSoftness`). The four latest burns are kept. |
| `GetInfectionAt(Location, Radius)`, `IsInfectedAt` | How infected a spot is right now (0..1): arrived, not burned. Use it for damage zones, AI avoidance, audio. |

Notes:
- Burns and queries need the materials from this version: press **Rebuild
  Materials** once if you grew with an earlier one.
- Saved growth settings retain their old values. To use the revised darker
  palette, reset the colour, vein and seam settings to their defaults before
  growing again.
- Regrow scaled hosts made with earlier live-spread builds to rewrite their
  infection maps with actor-space sample spacing. Query radii stay in world
  centimetres, and burns are checked at the nearby samples rather than the
  query centre.
- The component changes the materials only in play (PIE, Simulate or a
  build). In the editor, preview a stage by setting `Growth` on the
  `MI_Eclipse_*` instances.
- Animate spreads in **Overlay** mode. In Replace mode the plates' raised
  shape stays visible, in the host colour, ahead of the spread.
- `GetInfectionAt` on a character answers for its reference pose, not its
  current animation.
- An infected actor attached under another plays its own spread; the parent's
  component leaves it alone.

## The materials

![Maw, fusion and the detail maps](previews/realism.jpg)

The first grow creates, under `<Output Folder>/Materials`, two masters, four
instances and two detail textures. Edit the instances; regenerate everything
with **Rebuild Materials**.

| Asset | What it is |
|---|---|
| `M_EclipseBiomass` | Carapace: masked, two-sided, clear coat |
| `M_EclipseFlesh` | Soft tissue: masked, two-sided, subsurface scattering |
| `MI_Eclipse_Overlay` | overlay shells |
| `MI_Eclipse_Replace` | replacing surfaces |
| `MI_Eclipse_Growths` | horns, claws, roots, vertebrae, strands, bridges |
| `MI_Eclipse_Flesh` | hearts, throats, eyes, sacs, membranes, drips |
| `T_EclipseChitin`, `T_EclipseFlesh` | Generated detail maps (512², uncompressed): height slopes in RG, height in B, cavity in A. Not colour. |

The detail is tiled triplanar over the mesh's **bind pose** (pre-skinned
position), so it stays fixed to a character's skin as it animates. The
materials output a world-space normal, so they need no tangents from the mesh.
Their logic is small Custom (HLSL) nodes you can read and edit in the material
editor. Parameters:

| Parameter | Default | Effect |
|---|---|---|
| `Growth` | 4 | How far the growth has played. Arrival is 0 at the seed and 1 at the front, so **animate Growth from 0 to about 1.4 to watch the infection spread** (Sequencer, a Timeline, or a Material Parameter Collection). |
| `Softness` | 0.08 | Width of the advancing edge while animating. |
| `GlowIntensity` | 12 | Emissive strength of veins, seams and eyes. |
| `PulseSpeed`, `PulseAmount` | 0.6, 0.35 | Waves of light travelling outward from the seed. |
| `CarapaceColor`, `CarapaceTint` | near-black, violet | The shell; the tint varies per plate. |
| `GlowColor` | violet | |
| `EmberColor`, `BurnSoftness` | orange, 40 cm | The smouldering edge of a burn, and its width. |
| `Burn0`..`Burn3` | unused | Set by the Eclipse Spread component: centre in RGB, radius in A. |
| `InflamedColor` | dark magenta | Tissue at the front, before it blackens. |
| `HostColor` | grey | Clean tissue, Replace mode only. |
| `ClearCoatRoughness` | 0.06 | The wet sheen. |
| `DetailScale` | 12 cm | Size of one tile of the chitin detail (flesh tiles 1.7 times finer). |
| `DetailStrength`, `FleshDetail` | 0.35, 1.3 | How deep the detail reads; the second multiplies it on soft tissue. |
| `CavityStrength` | 0.6 | Darkening down in the detail's grooves. |
| `FleshColor`, `FleshDeep` | dark wine | Soft tissue, and its folds. Also shows in carapace seams. |
| `ScatterColor`, `Translucency` | red-violet, 0.65 | Light passing through soft tissue (flesh master). |
| `FleshRoughness`, `FleshGlow` | 0.32, 0.15 | Wet sheen of tissue; faint scattered light at grazing angles. |
| `OverlayMode`, `IsGrowth` | set per instance | Leave as the instances set them. |

The vertex data the material reads is listed in the README's output contract.

## If it does not compile

The generator core is built and tested on Windows, Linux, GCC, Clang and MSVC
by CI, including as a DLL that exports only the functions marked
`ECLIPSECORE_API`, the way the Unreal editor links it. On UE 5.8.3, the plugin's
Editor, Game Development and Game Shipping builds have passed with precompiled
headers and unity builds disabled (`-NoPCH -NoSharedPCH -DisableUnity`, the
flags used by `BuildPlugin -StrictIncludes`), with the 35 standalone tests and
six Unreal automation tests passing, as of the live-spread fixes. The realism
pass adds standalone tests (40 now) and a materials automation test; its
engine side awaits the same build. Run all Unreal tests with
`Automation RunTests Infection`: they check skeletal corruption-channel bytes,
skeletal regeneration after host changes, sparse-map queries, and live-spread
burns and scaled world distances. The three spread tests failed on the original
live-spread implementation and pass after the query fixes. These synthetic
fixtures do not qualify the appearance or animation of a production character.

Engine APIs that have moved between versions, and what to check:

| Symptom | Where | Fix |
|---|---|---|
| Syntax errors at `ECLIPSECORE_API` declarations in a no-PCH build | `eclipse/api.h` | UBT's export macros require `HAL/Platform.h`; include it only when building an Unreal module so standalone builds stay portable. |
| `GetMeshDescription` / `CreateMeshDescription` / `CommitMeshDescription` errors | `EclipseMeshBridge.cpp` | These are the standard UStaticMesh editor calls; check their current signatures in `Engine/StaticMesh.h`. |
| `FCustomInput::InputName` type mismatch | `EclipseMaterials.cpp` | It is an `FName` in UE5; if it became an `FString`, drop the `FName(...)` wrap. |
| `UMaterialEditingLibrary` missing | `InfectionEditor.Build.cs` | It lives in the `MaterialEditor` module, already listed. |
| Material settings not applied | `EclipseMaterials.cpp` | Blend mode and two-sided are set by property name; check the names `BlendMode` and `TwoSided` on `UMaterial`. |
| Nanite not enabled on outputs | `EclipseMeshBridge.cpp` | Set by property name `NaniteSettings.bEnabled`; enable it in the mesh editor meanwhile. |
| `PreSkinnedPosition` / `VertexInterpolator` / `Transform` errors | `EclipseMaterials.cpp` | Detail uses the bind-pose position through a vertex interpolator; check those expression classes' member names (`VSOutput`, `Input`, `TransformSourceType`). |
| Detail textures empty or wrong | `EclipseMaterials.cpp`, `BuildDetailTexture` | Written with `Source.Init(..., TSF_BGRA8, ...)` and `TC_VectorDisplacementmap`; check `FTextureSource::Init`. |
| Lighting looks inverted or flat | `EclipseMaterials.cpp` | The normal is world space via the `bTangentSpaceNormal` property set by name; check it is off on both masters. |
| Skeletal output is empty or fails to commit | `EclipseMeshBridge.cpp`, `WriteSkeletalMesh` | The description includes skin weights and the host's reference bones; check the current skeletal mesh commit and build APIs. |

Paste the compiler output into a session and it can be fixed quickly.
