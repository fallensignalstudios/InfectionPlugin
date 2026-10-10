#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;

// The Eclipse master materials (hard carapace and soft tissue), their
// instances and the detail textures they tile, created on first use under
// <OutputFolder>/Materials. Existing assets are reused untouched, so your
// edits survive; pass bRebuild to regenerate them from code.
struct FEclipseMaterials
{
	UMaterialInterface* Overlay = nullptr;  // shell over an untouched host
	UMaterialInterface* Replace = nullptr;  // whole corrupted surface
	UMaterialInterface* Growths = nullptr;  // horns, claws, roots, strands
	UMaterialInterface* Flesh = nullptr;    // hearts, throats, eyes, sacs, membranes

	static bool Ensure(const FString& OutputFolder, bool bRebuild, FEclipseMaterials& Out, FText& OutError);
};
