#include "EclipseInfectionSensorComponent.h"

#include "EclipseInfectionSubsystem.h"
#include "Engine/World.h"

UEclipseInfectionSensorComponent::UEclipseInfectionSensorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UEclipseInfectionSensorComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	SinceCheck += DeltaTime;
	Elapsed += DeltaTime;
	if (SinceCheck >= Interval)
	{
		SinceCheck = 0.0f;
		Sense();
	}
}

void UEclipseInfectionSensorComponent::Sense()
{
	const UWorld* World = GetWorld();
	const UEclipseInfectionSubsystem* Subsystem = World ? World->GetSubsystem<UEclipseInfectionSubsystem>() : nullptr;
	Infection = Subsystem ? Subsystem->GetInfectionAt(GetComponentLocation(), SearchRadius) : 0.0f;
	const bool bNowInside = Infection >= Threshold;
	const float Delta = Elapsed;
	Elapsed = 0.0f;
	if (bNowInside && !bInside)
	{
		bInside = true;
		OnEnterInfection.Broadcast(Infection);
	}
	else if (!bNowInside && bInside)
	{
		bInside = false;
		OnLeaveInfection.Broadcast(Infection);
	}
	if (bInside)
	{
		OnInfectionTick.Broadcast(Infection, Delta);
	}
}
