#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "EclipseMask.generated.h"

class UBoxComponent;
class USphereComponent;

UENUM(BlueprintType)
enum class EEclipseMaskMode : uint8
{
	// The corruption never enters: walkways, doors, gameplay space.
	Block,
	// The surface may corrupt but nothing grows on it: no horns where players walk.
	Bare,
	// The corruption is drawn here first, so it is oldest and most overgrown.
	Dense,
};

UENUM(BlueprintType)
enum class EEclipseMaskShape : uint8
{
	Box,
	Sphere,
};

// A volume that shapes where Eclipse corruption grows. Place it in the level,
// scale it over the area, and grow: every enabled mask applies to every grow.
// Masks are editor helpers; they draw nothing in game.
UCLASS(meta = (DisplayName = "Eclipse Mask"), hidecategories = (Rendering, Physics, Collision, Input, LOD, Cooking, Navigation, HLOD))
class INFECTIONRUNTIME_API AEclipseMask : public AActor
{
	GENERATED_BODY()

public:
	AEclipseMask();

	virtual void OnConstruction(const FTransform& Transform) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse")
	EEclipseMaskMode Mode = EEclipseMaskMode::Block;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse")
	EEclipseMaskShape Shape = EEclipseMaskShape::Box;

	// How far outside the shape its effect fades to nothing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse", meta = (ClampMin = "0", Units = "cm"))
	float Falloff = 20.0f;

	// Below 1, a Block only slows and thins the corruption, a Dense pulls it
	// less hard.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse", meta = (ClampMin = "0", ClampMax = "1"))
	float Strength = 1.0f;

	// The shape in the world: its transform (unscaled) and half-size in
	// centimetres along its own axes; a sphere's radius is HalfExtents.X.
	void GetWorldShape(FTransform& OutTransform, FVector& OutHalfExtents) const;

private:
	UPROPERTY(VisibleAnywhere, Category = "Eclipse")
	TObjectPtr<UBoxComponent> Box;

	UPROPERTY(VisibleAnywhere, Category = "Eclipse")
	TObjectPtr<USphereComponent> Sphere;
};
