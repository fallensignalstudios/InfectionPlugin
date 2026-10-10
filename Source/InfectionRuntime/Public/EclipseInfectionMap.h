#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Templates/Function.h"

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

	// Conservative distance between neighbouring points in actor-space centimetres.
	UPROPERTY(VisibleAnywhere, Category = "Eclipse")
	float Spacing = 5.0f;

	// The strongest infection within Radius of an actor-space point, counting
	// only what has arrived by Growth. 0 when nothing is there yet.
	float InfectionAt(const FVector& ActorSpacePoint, float Radius, float Growth) const;

	// The same local search, accepting only samples that also pass a caller's
	// world-distance or visibility check. The grid remains the broad phase.
	float InfectionAt(const FVector& ActorSpacePoint, float Radius, float Growth,
		TFunctionRef<bool(const FVector3f&)> AcceptSample) const;

	// Scan without a local-distance bound when an actor transform is singular.
	float InfectionAt(float Growth, TFunctionRef<bool(const FVector3f&)> AcceptSample) const;

	void SetSamples(TArray<FVector3f>&& InPoints, TArray<float>&& InArrival, TArray<float>&& InInfection, float InSpacing);

	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditUndo() override;
#endif

private:
	void BuildGridIfNeeded() const;

	// Built on first query, never saved; game thread only.
	mutable TMap<FIntVector, TArray<int32>> Grid;
	mutable float GridCell = 0.0f;
};
