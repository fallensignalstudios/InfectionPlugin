#include "EclipseInfectionMap.h"

void UEclipseInfectionMap::SetSamples(TArray<FVector3f>&& InPoints, TArray<float>&& InArrival, TArray<float>&& InInfection,
	float InSpacing)
{
	Points = MoveTemp(InPoints);
	Arrival = MoveTemp(InArrival);
	Infection = MoveTemp(InInfection);
	Spacing = InSpacing;
	Grid.Reset();
	GridCell = 0.0f;
}

void UEclipseInfectionMap::BuildGridIfNeeded() const
{
	if (GridCell > 0.0f || Points.Num() == 0)
	{
		return;
	}
	GridCell = FMath::Max(Spacing * 2.0f, 1.0f);
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		const FVector3f& P = Points[Index];
		Grid.FindOrAdd(FIntVector(FMath::FloorToInt(P.X / GridCell), FMath::FloorToInt(P.Y / GridCell), FMath::FloorToInt(P.Z / GridCell)))
			.Add(Index);
	}
}

float UEclipseInfectionMap::InfectionAt(const FVector& ActorSpacePoint, float Radius, float Growth) const
{
	BuildGridIfNeeded();
	if (Grid.Num() == 0 || Arrival.Num() != Points.Num() || Infection.Num() != Points.Num())
	{
		return 0.0f;
	}
	// Samples are Spacing apart, so search a little wider than asked.
	const float Reach = FMath::Max(Radius, 0.0f) + Spacing;
	const FVector3f P(ActorSpacePoint);
	const FIntVector Lo(FMath::FloorToInt((P.X - Reach) / GridCell), FMath::FloorToInt((P.Y - Reach) / GridCell),
		FMath::FloorToInt((P.Z - Reach) / GridCell));
	const FIntVector Hi(FMath::FloorToInt((P.X + Reach) / GridCell), FMath::FloorToInt((P.Y + Reach) / GridCell),
		FMath::FloorToInt((P.Z + Reach) / GridCell));
	float Best = 0.0f;
	for (int32 X = Lo.X; X <= Hi.X; ++X)
	{
		for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
		{
			for (int32 Z = Lo.Z; Z <= Hi.Z; ++Z)
			{
				const TArray<int32>* Cell = Grid.Find(FIntVector(X, Y, Z));
				if (!Cell)
				{
					continue;
				}
				for (const int32 Index : *Cell)
				{
					if (Arrival[Index] <= Growth && FVector3f::DistSquared(Points[Index], P) <= Reach * Reach)
					{
						Best = FMath::Max(Best, Infection[Index]);
					}
				}
			}
		}
	}
	return Best;
}
