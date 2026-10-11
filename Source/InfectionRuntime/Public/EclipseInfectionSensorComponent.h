#pragma once

#include "Components/SceneComponent.h"
#include "CoreMinimal.h"

#include "EclipseInfectionSensorComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEclipseSensorEvent, float, Infection);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEclipseSensorTick, float, Infection, float, DeltaSeconds);

// Senses Eclipse corruption where it sits: attach one to a pawn (at its feet,
// say) for damage, slowing, audio or AI. It asks every infected actor in the
// level, a few times a second, and fires as it steps onto and off corruption
// and while it stays on it.
UCLASS(ClassGroup = (Eclipse), meta = (BlueprintSpawnableComponent, DisplayName = "Eclipse Infection Sensor"))
class INFECTIONRUNTIME_API UEclipseInfectionSensorComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UEclipseInfectionSensorComponent();

	// How far around the sensor counts.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse", meta = (ClampMin = "0", Units = "cm"))
	float SearchRadius = 40.0f;

	// How infected a spot must be to count as on the corruption.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse", meta = (ClampMin = "0", ClampMax = "1"))
	float Threshold = 0.5f;

	// Seconds between checks.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse", meta = (ClampMin = "0", Units = "s"))
	float Interval = 0.2f;

	// Stepped onto corruption.
	UPROPERTY(BlueprintAssignable, Category = "Eclipse")
	FEclipseSensorEvent OnEnterInfection;

	// Stepped off it (or it was burned or receded away).
	UPROPERTY(BlueprintAssignable, Category = "Eclipse")
	FEclipseSensorEvent OnLeaveInfection;

	// Every check while on it, with the seconds since the last: apply damage
	// as Rate * Infection * DeltaSeconds.
	UPROPERTY(BlueprintAssignable, Category = "Eclipse")
	FEclipseSensorTick OnInfectionTick;

	// The infection found by the last check.
	UFUNCTION(BlueprintPure, Category = "Eclipse")
	float GetInfection() const { return Infection; }

	UFUNCTION(BlueprintPure, Category = "Eclipse")
	bool IsInInfection() const { return bInside; }

	// Check now, without waiting for the interval.
	UFUNCTION(BlueprintCallable, Category = "Eclipse")
	void Sense();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	float Infection = 0.0f;
	bool bInside = false;
	float SinceCheck = 0.0f;
	float Elapsed = 0.0f;  // since the previous check, for OnInfectionTick
};
