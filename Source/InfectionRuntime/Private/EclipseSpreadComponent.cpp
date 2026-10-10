#include "EclipseSpreadComponent.h"

#include "Components/MeshComponent.h"
#include "EclipseInfectionMap.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
	const FName GrowthParameter(TEXT("Growth"));
	const FName BurnSoftnessParameter(TEXT("BurnSoftness"));
	const FName BurnParameters[UEclipseSpreadComponent::MaxBurns] = {
		TEXT("Burn0"), TEXT("Burn1"), TEXT("Burn2"), TEXT("Burn3")};
}

UEclipseSpreadComponent::UEclipseSpreadComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;  // ticks only while playing
}

void UEclipseSpreadComponent::BeginPlay()
{
	Super::BeginPlay();
	CollectMaterials();
	if (bSpreadOnBeginPlay)
	{
		StartSpread(false);
	}
	else
	{
		Apply();
	}
}

void UEclipseSpreadComponent::CollectMaterials()
{
	Materials.Reset();
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	TArray<AActor*> Actors;
	Owner->GetAttachedActors(Actors, true, /*bRecursivelyIncludeAttachedActors*/ true);
	Actors.Insert(Owner, 0);
	for (AActor* Actor : Actors)
	{
		TInlineComponentArray<UMeshComponent*> Meshes(Actor);
		for (UMeshComponent* Mesh : Meshes)
		{
			for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
			{
				UMaterialInterface* Material = Mesh->GetMaterial(Slot);
				float Unused = 0.0f;
				// Only Eclipse materials have a Growth parameter; leave the rest alone.
				if (!Material || !Material->GetScalarParameterValue(FHashedMaterialParameterInfo(GrowthParameter), Unused))
				{
					continue;
				}
				if (UMaterialInstanceDynamic* Dynamic = Mesh->CreateDynamicMaterialInstance(Slot, Material))
				{
					Materials.Add(Dynamic);
				}
			}
		}
	}
}

void UEclipseSpreadComponent::Apply()
{
	for (UMaterialInstanceDynamic* Material : Materials)
	{
		if (!Material)
		{
			continue;
		}
		Material->SetScalarParameterValue(GrowthParameter, Growth);
		Material->SetScalarParameterValue(BurnSoftnessParameter, BurnSoftness);
		for (int32 Index = 0; Index < MaxBurns; ++Index)
		{
			const FVector4 B = Burns.IsValidIndex(Index) ? Burns[Index] : FVector4(0, 0, 0, 0);
			Material->SetVectorParameterValue(BurnParameters[Index], FLinearColor(B.X, B.Y, B.Z, B.W));
		}
	}
}

void UEclipseSpreadComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (PlayRate == 0.0f)
	{
		SetComponentTickEnabled(false);
		return;
	}
	Growth += PlayRate * (FullGrowth / FMath::Max(SpreadDuration, 0.01f)) * DeltaTime;
	bool bFinished = false, bReceded = false;
	if (Growth >= FullGrowth && PlayRate > 0.0f)
	{
		Growth = FullGrowth;
		bFinished = true;
	}
	else if (Growth <= 0.0f && PlayRate < 0.0f)
	{
		Growth = 0.0f;
		bReceded = true;
	}
	Apply();
	if (bFinished || bReceded)
	{
		SetPlayRate(0.0f);
		(bFinished ? OnSpreadFinished : OnFullyReceded).Broadcast();
	}
}

void UEclipseSpreadComponent::StartSpread(bool bFromCurrent)
{
	if (!bFromCurrent)
	{
		Growth = 0.0f;
	}
	SetPlayRate(1.0f);
	Apply();
}

void UEclipseSpreadComponent::Recede()
{
	SetPlayRate(-1.0f);
}

void UEclipseSpreadComponent::SetPlayRate(float Rate)
{
	PlayRate = Rate;
	SetComponentTickEnabled(Rate != 0.0f);
}

void UEclipseSpreadComponent::SetGrowth(float NewGrowth)
{
	Growth = FMath::Max(NewGrowth, 0.0f);
	Apply();
}

void UEclipseSpreadComponent::Burn(FVector WorldLocation, float Radius)
{
	const FVector4 Entry(WorldLocation.X, WorldLocation.Y, WorldLocation.Z, FMath::Max(Radius, 0.0f));
	if (Burns.Num() < MaxBurns)
	{
		Burns.Add(Entry);
	}
	else
	{
		Burns[NextBurn] = Entry;
		NextBurn = (NextBurn + 1) % MaxBurns;
	}
	Apply();
}

void UEclipseSpreadComponent::ClearBurns()
{
	Burns.Reset();
	NextBurn = 0;
	Apply();
}

float UEclipseSpreadComponent::GetInfectionAt(FVector WorldLocation, float SearchRadius) const
{
	const AActor* Owner = GetOwner();
	if (!InfectionMap || !Owner)
	{
		return 0.0f;
	}
	for (const FVector4& B : Burns)
	{
		if (FVector::DistSquared(FVector(B.X, B.Y, B.Z), WorldLocation) < B.W * B.W)
		{
			return 0.0f;
		}
	}
	const FVector Local = Owner->GetActorTransform().InverseTransformPosition(WorldLocation);
	return InfectionMap->InfectionAt(Local, SearchRadius, Growth);
}
