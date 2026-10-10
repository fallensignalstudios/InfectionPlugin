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
	return InfectionAt(ActorSpacePoint, Radius, Growth, [](const FVector3f&) { return true; });
}

float UEclipseInfectionMap::InfectionAt(float Growth, TFunctionRef<bool(const FVector3f&)> AcceptSample) const
{
	if (Arrival.Num() != Points.Num() || Infection.Num() != Points.Num())
	{
		return 0.0f;
	}
	float Best = 0.0f;
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		if (Arrival[Index] <= Growth && AcceptSample(Points[Index]))
		{
			Best = FMath::Max(Best, Infection[Index]);
		}
	}
	return Best;
}

float UEclipseInfectionMap::InfectionAt(const FVector& ActorSpacePoint, float Radius, float Growth,
	TFunctionRef<bool(const FVector3f&)> AcceptSample) const
{
	BuildGridIfNeeded();
	if (Grid.Num() == 0 || Arrival.Num() != Points.Num() || Infection.Num() != Points.Num())
	{
		return 0.0f;
	}
	// Samples are Spacing apart, so search a little wider than asked.
	const float Reach = FMath::Max(Radius, 0.0f) + Spacing;
	const FVector3f P(ActorSpacePoint);
	float Best = 0.0f;
	auto Consider = [&](int32 Index)
	{
		if (Points.IsValidIndex(Index) && Arrival[Index] <= Growth && FVector3f::DistSquared(Points[Index], P) <= Reach * Reach &&
			AcceptSample(Points[Index]))
		{
			Best = FMath::Max(Best, Infection[Index]);
		}
	};
	// Very small actor scales can produce a local search outside the grid's
	// integer range. The samples themselves are still safe to check directly.
	const double MaxCellCoordinate = (static_cast<double>(P.GetAbsMax()) + Reach) / GridCell;
	if (MaxCellCoordinate >= static_cast<double>(MAX_int32 - 1))
	{
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			Consider(Index);
		}
		return Best;
	}
	const FIntVector Lo(FMath::FloorToInt((static_cast<double>(P.X) - Reach) / GridCell),
		FMath::FloorToInt((static_cast<double>(P.Y) - Reach) / GridCell),
		FMath::FloorToInt((static_cast<double>(P.Z) - Reach) / GridCell));
	const FIntVector Hi(FMath::FloorToInt((static_cast<double>(P.X) + Reach) / GridCell),
		FMath::FloorToInt((static_cast<double>(P.Y) + Reach) / GridCell),
		FMath::FloorToInt((static_cast<double>(P.Z) + Reach) / GridCell));
	// A huge radius spans more cells than there are points: just scan them.
	// Bound each factor before multiplying so the cell count cannot overflow.
	const int64 XCells = int64(Hi.X) - int64(Lo.X) + 1;
	const int64 YCells = int64(Hi.Y) - int64(Lo.Y) + 1;
	const int64 ZCells = int64(Hi.Z) - int64(Lo.Z) + 1;
	const int64 PointCount = Points.Num();
	if (XCells > PointCount || YCells > PointCount || ZCells > PointCount ||
		XCells * YCells > PointCount || XCells * YCells * ZCells > PointCount)
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
