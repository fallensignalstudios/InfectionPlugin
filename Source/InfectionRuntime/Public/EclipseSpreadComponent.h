#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"

#include "EclipseSpreadComponent.generated.h"

class UEclipseInfectionMap;
class UMaterialInstanceDynamic;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FEclipseSpreadEvent);

// Plays a grown corruption during the game. Every mesh on this actor (and on
// actors attached to it) that uses an Eclipse material follows Growth: 0
// shows nothing, 1 the corruption as grown, FullGrowth also the veins that
// ran ahead. Burn() cuts it back locally with glowing edges, and
// GetInfectionAt() tells gameplay how infected a spot is right now.
//
// The Eclipse Growth panel adds one of these to every host it grows on.
UCLASS(ClassGroup = (Eclipse), meta = (BlueprintSpawnableComponent, DisplayName = "Eclipse Spread"))
class INFECTIONRUNTIME_API UEclipseSpreadComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	static constexpr int32 MaxBurns = 4;

	UEclipseSpreadComponent();

	// Where the corruption lies, for GetInfectionAt. Written by the growth panel.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eclipse")
	TObjectPtr<UEclipseInfectionMap> InfectionMap;

	// How far the growth has played; see the class comment.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Eclipse", meta = (ClampMin = "0"))
	float Growth = 1.4f;

	// Growth at which the spread counts as finished.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse", meta = (ClampMin = "0.1"))
	float FullGrowth = 1.4f;

	// Seconds a spread takes from nothing to FullGrowth at play rate 1.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse", meta = (ClampMin = "0.01", Units = "s"))
	float SpreadDuration = 30.0f;

	// Start from nothing and spread as soon as play begins.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse")
	bool bSpreadOnBeginPlay = false;

	// Width of the glowing edge around a burn.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse", meta = (ClampMin = "1", Units = "cm"))
	float BurnSoftness = 40.0f;

	// The spread reached FullGrowth.
	UPROPERTY(BlueprintAssignable, Category = "Eclipse")
	FEclipseSpreadEvent OnSpreadFinished;

	// A recession reached nothing.
	UPROPERTY(BlueprintAssignable, Category = "Eclipse")
	FEclipseSpreadEvent OnFullyReceded;

	// Play the spread forward; from nothing unless bFromCurrent.
	UFUNCTION(BlueprintCallable, Category = "Eclipse")
	void StartSpread(bool bFromCurrent = false);

	// Play it backward until nothing is left.
	UFUNCTION(BlueprintCallable, Category = "Eclipse")
	void Recede();

	// 1 spreads, -1 recedes, 0 holds; any other speed scales SpreadDuration.
	UFUNCTION(BlueprintCallable, Category = "Eclipse")
	void SetPlayRate(float Rate);

	UFUNCTION(BlueprintPure, Category = "Eclipse")
	float GetPlayRate() const { return PlayRate; }

	UFUNCTION(BlueprintCallable, Category = "Eclipse")
	void SetGrowth(float NewGrowth);

	UFUNCTION(BlueprintPure, Category = "Eclipse")
	float GetGrowth() const { return Growth; }

	// Burn the corruption away within Radius of a point. The four most recent
	// burns are kept; a fifth replaces the oldest.
	UFUNCTION(BlueprintCallable, Category = "Eclipse")
	void Burn(FVector WorldLocation, float Radius);

	UFUNCTION(BlueprintCallable, Category = "Eclipse")
	void ClearBurns();

	// The strongest infection within SearchRadius of a world point that has
	// arrived and not been burned: 0 clean, 1 fully overtaken.
	UFUNCTION(BlueprintPure, Category = "Eclipse")
	float GetInfectionAt(FVector WorldLocation, float SearchRadius = 30.0f) const;

	UFUNCTION(BlueprintPure, Category = "Eclipse")
	bool IsInfectedAt(FVector WorldLocation, float SearchRadius = 30.0f, float Threshold = 0.5f) const
	{
		return GetInfectionAt(WorldLocation, SearchRadius) >= Threshold;
	}

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

private:
	// Dynamic instances of every Eclipse material slot under this actor.
	void CollectMaterials();
	void Apply();

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;

	TArray<FVector4> Burns;  // world centre, radius in W
	int32 NextBurn = 0;
	float PlayRate = 0.0f;
};
