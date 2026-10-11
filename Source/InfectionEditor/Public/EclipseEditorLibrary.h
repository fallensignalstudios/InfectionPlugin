#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "EclipseEditorLibrary.generated.h"

class UEclipseGrowthSettings;
class UStaticMeshComponent;

// Editor scripting entry points for the same actions used by the Growth panel.
UCLASS()
class INFECTIONEDITOR_API UEclipseEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Grow on the selected hosts using enabled nearby Eclipse Seeds. Generated
	// assets and level changes remain unsaved, as they do in the Growth panel.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Editor")
	static FText GrowOnSelection(UEclipseGrowthSettings* Settings);

	// Remove the corruption from the selected hosts, as the panel's Remove button does.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Editor")
	static FText RemoveFromSelection();

	// Rebuild the Eclipse material assets in Settings' output folder, unsaved.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Editor")
	static FText RebuildMaterials(UEclipseGrowthSettings* Settings);

	// One world-space point on a real LOD0 triangle per welded surface island.
	// Returns no points for an invalid host or while Play/Simulate is active.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Editor")
	static TArray<FVector> GetSurfaceSeedLocations(UStaticMeshComponent* Host);
};
