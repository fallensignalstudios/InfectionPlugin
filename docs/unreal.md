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
| `EclipseCore` | Runtime | The generator itself: the same plain C++ sources the CLI and tests build. No Unreal headers. |
| `InfectionRuntime` | Runtime | The **Eclipse Seed** actor (editor-only, stripped from cooked builds). |
| `InfectionEditor` | Editor | The **Eclipse Growth** panel, mesh conversion, asset writing and the materials. |

## Grow corruption

1. Open **Tools → Eclipse Growth**.
2. Select a static mesh actor in the level and press **Add Seed**, or drag an
   **Eclipse Seed** in from the Place Actors panel. Move the seed to where the
   infection should start. Several seeds grow several outbreaks that merge.
   A seed affects a mesh when it is within 1 m of the mesh's bounds.
3. Select the mesh (or several) and press **Grow on Selected**.

The generated actors are attached to the host, labelled `<Host> Eclipse_Shell`
and `<Host> Eclipse_Growths`, and replaced each time you grow again. Their
meshes are written to `<Output Folder>/Generated` and are unsaved until you
**Save All**. Undo removes the placed actors; the generated assets stay until you delete them.

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

## The material

The first grow creates `<Output Folder>/Materials/M_EclipseBiomass` and three
instances. Edit the instances; regenerate everything with **Rebuild Materials**.

| Instance | Used on |
|---|---|
| `MI_Eclipse_Overlay` | overlay shells |
| `MI_Eclipse_Replace` | replacing surfaces |
| `MI_Eclipse_Growths` | thorns, eyes, strands, bridges, membranes, vein tubes |

The master is masked, two-sided and clear-coat. Its logic is five small Custom
(HLSL) nodes you can read and edit in the material editor. Parameters:

| Parameter | Default | Effect |
|---|---|---|
| `Growth` | 4 | How far the growth has played. Arrival is 0 at the seed and 1 at the front, so **animate Growth from 0 to about 1.4 to watch the infection spread** (Sequencer, a Timeline, or a Material Parameter Collection). |
| `Softness` | 0.08 | Width of the advancing edge while animating. |
| `GlowIntensity` | 12 | Emissive strength of veins, seams and eyes. |
| `PulseSpeed`, `PulseAmount` | 0.6, 0.35 | Waves of light travelling outward from the seed. |
| `CarapaceColor`, `CarapaceTint` | near-black, violet | The shell; the tint varies per plate. |
| `GlowColor` | violet | |
| `InflamedColor` | dark magenta | Tissue at the front, before it blackens. |
| `HostColor` | grey | Clean tissue, Replace mode only. |
| `ClearCoatRoughness` | 0.06 | The wet sheen. |
| `OverlayMode`, `IsGrowth` | set per instance | Leave as the instances set them. |

The vertex data the material reads is listed in the README's output contract.

## If it does not compile

The generator core is built and tested on Windows, Linux, GCC, Clang and MSVC
by CI, including as a DLL that exports only the functions marked
`ECLIPSECORE_API`, the way the Unreal editor links it. The editor module is
not: it cannot be compiled without the engine, so it has only been written
against the 5.x API and reviewed, not built. Engine
APIs it relies on that have moved between versions, and what to change:

| Symptom | Where | Fix |
|---|---|---|
| `GetMeshDescription` / `CreateMeshDescription` / `CommitMeshDescription` errors | `EclipseMeshBridge.cpp` | These are the standard UStaticMesh editor calls; check their current signatures in `Engine/StaticMesh.h`. |
| `FCustomInput::InputName` type mismatch | `EclipseMaterials.cpp` | It is an `FName` in UE5; if it became an `FString`, drop the `FName(...)` wrap. |
| `UMaterialEditingLibrary` missing | `InfectionEditor.Build.cs` | It lives in the `MaterialEditor` module, already listed. |
| Material settings not applied | `EclipseMaterials.cpp` | Blend mode and two-sided are set by property name; check the names `BlendMode` and `TwoSided` on `UMaterial`. |
| Nanite not enabled on outputs | `EclipseMeshBridge.cpp` | Set by property name `NaniteSettings.bEnabled`; enable it in the mesh editor meanwhile. |

Paste the compiler output into a session and it can be fixed quickly.
