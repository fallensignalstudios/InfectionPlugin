#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"

#include "EclipseInfectionMap.generated.h"

// Where a grown corruption lies and when each part of it arrived, sampled
// sparsely so gameplay can ask about it cheaply. Written by the Eclipse
// Growth panel next to the generated meshes; positions are in the space of
// the actor the corruption was grown on.
UCLASS(BlueprintType)
class INFECTIONRUNTIME_API UEclipseInfectionMap : public UDataAsset
{
	GENERATED_BODY()

public:
	// Actor-space positions, centimetres.
	UPROPERTY(VisibleAnywhere, Category = "Eclipse")
	TArray<FVector3f> Points;

	// When each point turned: 0 at a seed, 1 at the front (the material's Growth scale).
	UPROPERTY(VisibleAnywhere, Category = "Eclipse")
	TArray<float> Arrival;

	// How overtaken each point is once arrived, 0..1.
	UPROPERTY(VisibleAnywhere, Category = "Eclipse")
	TArray<float> Infection;

	// Distance between neighbouring points, centimetres.
	UPROPERTY(VisibleAnywhere, Category = "Eclipse")
	float Spacing = 5.0f;

	// The strongest infection within Radius of an actor-space point, counting
	// only what has arrived by Growth. 0 when nothing is there yet.
	float InfectionAt(const FVector& ActorSpacePoint, float Radius, float Growth) const;

	void SetSamples(TArray<FVector3f>&& InPoints, TArray<float>&& InArrival, TArray<float>&& InInfection, float InSpacing);

private:
	void BuildGridIfNeeded() const;

	// Built on first query, never saved.
	mutable TMap<FIntVector, TArray<int32>> Grid;
	mutable float GridCell = 0.0f;
};
