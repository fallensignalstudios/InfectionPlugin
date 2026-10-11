#include "EclipseEditorLibrary.h"

#include "Components/StaticMeshComponent.h"
#include "EclipseGrowthRunner.h"
#include "EclipseGrowthSettings.h"
#include "EclipseMeshBridge.h"
#include "Editor.h"
#include "Engine/World.h"

#include "eclipse/mesh.h"

#define LOCTEXT_NAMESPACE "EclipseEditorLibrary"

namespace
{
	FText ValidateEditorState()
	{
		if (!GEditor)
		{
			return LOCTEXT("NoEditor", "Eclipse generation requires the editor.");
		}
		if (GEditor->IsPlaySessionInProgress())
		{
			return LOCTEXT("PlayInProgress", "Stop Play or Simulate before generating Eclipse assets.");
		}
		const UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!World || World->WorldType != EWorldType::Editor)
		{
			return LOCTEXT("NoEditorWorld", "No editor world is open.");
		}
		return FText::GetEmpty();
	}

	FText ValidateEditorCall(const UEclipseGrowthSettings* Settings)
	{
		return IsValid(Settings) ? ValidateEditorState() : LOCTEXT("NoSettings", "Provide Eclipse Growth settings.");
	}
}

FText UEclipseEditorLibrary::GrowOnSelection(UEclipseGrowthSettings* Settings)
{
	const FText Error = ValidateEditorCall(Settings);
	return Error.IsEmpty() ? EclipseGrowthRunner::GrowOnSelection(*Settings) : Error;
}

FText UEclipseEditorLibrary::RemoveFromSelection()
{
	const FText Error = ValidateEditorState();
	return Error.IsEmpty() ? EclipseGrowthRunner::RemoveFromSelection() : Error;
}

FText UEclipseEditorLibrary::RebuildMaterials(UEclipseGrowthSettings* Settings)
{
	const FText Error = ValidateEditorCall(Settings);
	return Error.IsEmpty() ? EclipseGrowthRunner::RebuildMaterials(*Settings) : Error;
}

TArray<FVector> UEclipseEditorLibrary::GetSurfaceSeedLocations(UStaticMeshComponent* Host)
{
	TArray<FVector> Seeds;
	if (!IsValid(Host) || !ValidateEditorState().IsEmpty() || Host->GetWorld() != GEditor->GetEditorWorldContext().World())
	{
		return Seeds;
	}
	EclipseBridge::FHost Source;
	FText Error;
	if (!EclipseBridge::ReadHost(Host, Source, Error))
	{
		return Seeds;
	}
	// Match the runner's 0.01 cm world-space weld before finding connectivity.
	eclipse::weldVertices(Source.Mesh, 0.0001f);
	if (Source.Mesh.triangles.empty())
	{
		return Seeds;
	}
	std::vector<uint32_t> Parents(Source.Mesh.positions.size());
	for (uint32_t Vertex = 0; Vertex < Parents.size(); ++Vertex)
	{
		Parents[Vertex] = Vertex;
	}
	auto FindRoot = [&](uint32_t Vertex)
	{
		uint32_t Root = Vertex;
		while (Parents[Root] != Root)
		{
			Root = Parents[Root];
		}
		while (Parents[Vertex] != Vertex)
		{
			const uint32_t Next = Parents[Vertex];
			Parents[Vertex] = Root;
			Vertex = Next;
		}
		return Root;
	};
	auto Join = [&](uint32_t A, uint32_t B)
	{
		const uint32_t RootA = FindRoot(A);
		const uint32_t RootB = FindRoot(B);
		// The root is always the minimum vertex index, giving each island a
		// deterministic key regardless of hash-map iteration order.
		Parents[FMath::Max(RootA, RootB)] = FMath::Min(RootA, RootB);
	};
	for (const auto& Triangle : Source.Mesh.triangles)
	{
		Join(Triangle[0], Triangle[1]);
		Join(Triangle[0], Triangle[2]);
	}
	struct FSurfaceIsland
	{
		FVector WeightedCentre = FVector::ZeroVector;
		double Area = 0.0;
		FVector Seed = FVector::ZeroVector;
		double SeedDistanceSquared = 0.0;
		bool bHasSeed = false;
	};
	TMap<uint32, FSurfaceIsland> Islands;
	for (const auto& Triangle : Source.Mesh.triangles)
	{
		const FVector A = EclipseBridge::ToUnreal(Source.Mesh.positions[Triangle[0]]);
		const FVector B = EclipseBridge::ToUnreal(Source.Mesh.positions[Triangle[1]]);
		const FVector C = EclipseBridge::ToUnreal(Source.Mesh.positions[Triangle[2]]);
		const double Area = 0.5 * FVector::CrossProduct(B - A, C - A).Size();
		if (Area > 0.0 && FMath::IsFinite(Area))
		{
			FSurfaceIsland& Island = Islands.FindOrAdd(FindRoot(Triangle[0]));
			Island.WeightedCentre += ((A + B + C) / 3.0) * Area;
			Island.Area += Area;
		}
	}
	for (auto& Pair : Islands)
	{
		Pair.Value.WeightedCentre /= Pair.Value.Area;
	}
	// The average can lie in empty space inside a hollow or concave island.
	// Choose an actual positive-area triangle's centroid nearest that average.
	// Strictly closer wins, so stable triangle order also resolves equal ties.
	for (const auto& Triangle : Source.Mesh.triangles)
	{
		const FVector A = EclipseBridge::ToUnreal(Source.Mesh.positions[Triangle[0]]);
		const FVector B = EclipseBridge::ToUnreal(Source.Mesh.positions[Triangle[1]]);
		const FVector C = EclipseBridge::ToUnreal(Source.Mesh.positions[Triangle[2]]);
		const double Area = 0.5 * FVector::CrossProduct(B - A, C - A).Size();
		if (Area <= 0.0 || !FMath::IsFinite(Area))
		{
			continue;
		}
		FSurfaceIsland& Island = Islands.FindChecked(FindRoot(Triangle[0]));
		const FVector Centre = (A + B + C) / 3.0;
		const double DistanceSquared = FVector::DistSquared(Centre, Island.WeightedCentre);
		if (FMath::IsFinite(DistanceSquared) && (!Island.bHasSeed || DistanceSquared < Island.SeedDistanceSquared))
		{
			Island.Seed = Centre;
			Island.SeedDistanceSquared = DistanceSquared;
			Island.bHasSeed = true;
		}
	}
	TArray<uint32> Roots;
	Islands.GetKeys(Roots);
	Roots.Sort();
	for (const uint32 Root : Roots)
	{
		const FSurfaceIsland& Island = Islands.FindChecked(Root);
		if (Island.bHasSeed)
		{
			Seeds.Add(Island.Seed + Source.Origin);
		}
	}
	return Seeds;
}

#undef LOCTEXT_NAMESPACE
