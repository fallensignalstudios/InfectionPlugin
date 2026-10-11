# Changelog

The plugin's version is in `InfectionPlugin.uplugin` (`VersionName`). Unreal
side changes need **Rebuild Materials** once when the materials changed.

## 0.4.0

- **Runtime**: outbreaks (`StartOutbreak`) reveal the corruption outward from
  a point; burn scars are permanent (64, then merged); `OnBurned`,
  `OnOutbreakStarted` and `OnOutbreakFinished` events; the Eclipse Infection
  world subsystem and the Eclipse Infection Sensor component. Materials read
  burns and outbreaks from an event texture (*Rebuild Materials*).
- **Multiplayer**: the Spread component replicates; late joiners catch up.
- **Save games**: `GetState` / `RestoreState`.
- **Editor**: Remove from Selected.
- `LogEclipse` log category.

## Before 0.4.0

The version stayed at 0.3.0 while these landed, newest first:

- Rooms, masks and variations: separate meshes grown as one, Eclipse Mask
  volumes (Block, Bare, Dense), Previous / Next Variation.
- Formations and presets: spires, ribcages, pods and shelves; seven presets.
- Realism passes: soft tissue (flesh master), fusion, crater maws, sacs,
  detail maps, bruise and bone tones, the halo ahead of the front, optional
  Nanite displacement.
- Live spread, receding and four burns; characters (skeletal hosts); the
  Unreal plugin and Eclipse Growth panel; growths (veins, thorns, eyes,
  strands, bridges, membranes); the core generator, CLI and glTF export.
