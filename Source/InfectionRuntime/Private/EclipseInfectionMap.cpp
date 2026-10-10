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

void UEclipseInfectionMap::PostLoad()
{
	Super::PostLoad();
	Grid.Reset();
	GridCell = 0.0f;
}

#if WITH_EDITOR
void UEclipseInfectionMap::PostEditUndo()
{
	Super::PostEditUndo();
	// The samples may have changed under the grid.
	Grid.Reset();
	GridCell = 0.0f;
}
#endif

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
	auto Consider = [&](int32 Index)
	{
		if (Points.IsValidIndex(Index) && Arrival[Index] <= Growth && FVector3f::DistSquared(Points[Index], P) <= Reach * Reach)
		{
			Best = FMath::Max(Best, Infection[Index]);
		}
	};
	// A huge radius spans more cells than there are points: just scan them.
	const int64 Cells = int64(Hi.X - Lo.X + 1) * int64(Hi.Y - Lo.Y + 1) * int64(Hi.Z - Lo.Z + 1);
	if (Cells > Points.Num())
	{
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			Consider(Index);
		}
		return Best;
	}
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
					Consider(Index);
				}
			}
		}
	}
	return Best;
}
