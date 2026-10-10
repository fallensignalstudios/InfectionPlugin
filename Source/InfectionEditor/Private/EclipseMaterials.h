#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;

// The Eclipse master material and its three instances, created on first use
// under <OutputFolder>/Materials. Existing assets are reused untouched, so
// your edits survive; pass bRebuild to regenerate them from code.
struct FEclipseMaterials
{
	UMaterialInterface* Overlay = nullptr;  // shell over an untouched host
	UMaterialInterface* Replace = nullptr;  // whole corrupted surface
	UMaterialInterface* Growths = nullptr;  // thorns, eyes, strands, membranes

	static bool Ensure(const FString& OutputFolder, bool bRebuild, FEclipseMaterials& Out, FText& OutError);
};
