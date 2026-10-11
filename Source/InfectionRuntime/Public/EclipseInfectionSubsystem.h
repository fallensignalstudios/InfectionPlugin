#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "EclipseInfectionSubsystem.generated.h"

class UEclipseSpreadComponent;

// Every Eclipse Spread in a level, for gameplay that should not need to know
// which actor the corruption is on: a flamethrower burns where it hits, a
// projectile starts an outbreak where it lands, a pawn asks how infected the
// ground under it is.
UCLASS()
class INFECTIONRUNTIME_API UEclipseInfectionSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// The strongest infection within SearchRadius of a world point, across
	// every infected actor: 0 clean, 1 fully overtaken.
	UFUNCTION(BlueprintPure, Category = "Eclipse")
	float GetInfectionAt(FVector WorldLocation, float SearchRadius = 30.0f) const;

	UFUNCTION(BlueprintPure, Category = "Eclipse")
	bool IsInfectedAt(FVector WorldLocation, float SearchRadius = 30.0f, float Threshold = 0.5f) const
	{
		return GetInfectionAt(WorldLocation, SearchRadius) >= Threshold;
	}

	// Burns every infected actor the sphere reaches. Returns how many.
	UFUNCTION(BlueprintCallable, Category = "Eclipse")
	int32 BurnAt(FVector WorldLocation, float Radius);

	// Starts an outbreak on every infected actor within MaxRadius. Returns how many.
	UFUNCTION(BlueprintCallable, Category = "Eclipse")
	int32 StartOutbreakAt(FVector WorldLocation, float MaxRadius = 300.0f, float Speed = 60.0f);

	// The spreads whose corruption lies within Radius of a world point.
	UFUNCTION(BlueprintCallable, Category = "Eclipse")
	TArray<UEclipseSpreadComponent*> GetSpreadsNear(FVector WorldLocation, float Radius) const;

	// Called by the spread components themselves.
	void Register(UEclipseSpreadComponent* Spread);
	void Unregister(UEclipseSpreadComponent* Spread);

private:
	TArray<TWeakObjectPtr<UEclipseSpreadComponent>> Spreads;
};
