#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "EclipseSeed.generated.h"

class UBillboardComponent;

// Marks where Eclipse corruption starts. Place one or more in the level near
// a mesh, select the mesh, and grow from the Eclipse Growth panel. Seeds are
// editor helpers; they draw nothing in game.
UCLASS(meta = (DisplayName = "Eclipse Seed"), hidecategories = (Rendering, Physics, Collision, Input, LOD, Cooking))
class INFECTIONRUNTIME_API AEclipseSeed : public AActor
{
	GENERATED_BODY()

public:
	AEclipseSeed();

	// Off: this seed is ignored when growing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse")
	bool bEnabled = true;

#if WITH_EDITORONLY_DATA
private:
	UPROPERTY()
	TObjectPtr<UBillboardComponent> Sprite;
#endif
};
