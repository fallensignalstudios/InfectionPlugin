#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;

// The Eclipse master materials (hard carapace and soft tissue), their
// instances and the detail textures they tile, created on first use under
// <OutputFolder>/Materials. Existing assets are reused untouched, so your
// edits survive; pass bRebuild to regenerate them from code.
// Choices baked into the materials when they are built.
struct FEclipseMaterialOptions
{
	// Nanite tessellation pushes the carapace detail out into the silhouette.
	// Needs r.Nanite.AllowTessellation=1 and r.Nanite.Tessellation=1 in the
	// project, and Nanite meshes; ignored otherwise.
	bool bDisplacement = false;
	float DisplacementDepth = 0.8f;  // centimetres
	// How far past the front the halo reaches; must match the grown shells
	// (GrowthParams::haloReach). Updated on the instances on every grow.
	float HaloReach = 0.35f;
};

struct FEclipseMaterials
{
	UMaterialInterface* Overlay = nullptr;  // shell over an untouched host
	UMaterialInterface* Replace = nullptr;  // whole corrupted surface
	UMaterialInterface* Growths = nullptr;  // horns, claws, roots, strands
	UMaterialInterface* Flesh = nullptr;    // hearts, throats, eyes, sacs, membranes

	static bool Ensure(const FString& OutputFolder, bool bRebuild, FEclipseMaterials& Out, FText& OutError,
		const FEclipseMaterialOptions& Options = FEclipseMaterialOptions());
};
