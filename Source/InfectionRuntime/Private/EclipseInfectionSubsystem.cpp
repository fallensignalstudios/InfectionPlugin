#include "EclipseInfectionSubsystem.h"

#include "EclipseSpreadComponent.h"

void UEclipseInfectionSubsystem::Register(UEclipseSpreadComponent* Spread)
{
	if (Spread)
	{
		Spreads.AddUnique(Spread);
	}
}

void UEclipseInfectionSubsystem::Unregister(UEclipseSpreadComponent* Spread)
{
	Spreads.RemoveAll([Spread](const TWeakObjectPtr<UEclipseSpreadComponent>& Entry) { return !Entry.IsValid() || Entry.Get() == Spread; });
}

TArray<UEclipseSpreadComponent*> UEclipseInfectionSubsystem::GetSpreadsNear(FVector WorldLocation, float Radius) const
{
	TArray<UEclipseSpreadComponent*> Near;
	const double Reach = FMath::Max(Radius, 0.0f);
	for (const TWeakObjectPtr<UEclipseSpreadComponent>& Entry : Spreads)
	{
		UEclipseSpreadComponent* Spread = Entry.Get();
		if (!Spread)
		{
			continue;
		}
		const FBox Bounds = Spread->GetWorldBounds();
		if (Bounds.IsValid && Bounds.ComputeSquaredDistanceToPoint(WorldLocation) <= Reach * Reach)
		{
			Near.Add(Spread);
		}
	}
	return Near;
}

float UEclipseInfectionSubsystem::GetInfectionAt(FVector WorldLocation, float SearchRadius) const
{
	float Best = 0.0f;
	for (const UEclipseSpreadComponent* Spread : GetSpreadsNear(WorldLocation, SearchRadius))
	{
		Best = FMath::Max(Best, Spread->GetInfectionAt(WorldLocation, SearchRadius));
	}
	return Best;
}

int32 UEclipseInfectionSubsystem::BurnAt(FVector WorldLocation, float Radius)
{
	if (Radius <= 0.0f)
	{
		return 0;
	}
	TArray<UEclipseSpreadComponent*> Near = GetSpreadsNear(WorldLocation, Radius);
	for (UEclipseSpreadComponent* Spread : Near)
	{
		Spread->Burn(WorldLocation, Radius);
	}
	return Near.Num();
}

int32 UEclipseInfectionSubsystem::StartOutbreakAt(FVector WorldLocation, float MaxRadius, float Speed)
{
	if (MaxRadius <= 0.0f)
	{
		return 0;
	}
	TArray<UEclipseSpreadComponent*> Near = GetSpreadsNear(WorldLocation, MaxRadius);
	for (UEclipseSpreadComponent* Spread : Near)
	{
		Spread->StartOutbreak(WorldLocation, MaxRadius, Speed);
	}
	return Near.Num();
}
