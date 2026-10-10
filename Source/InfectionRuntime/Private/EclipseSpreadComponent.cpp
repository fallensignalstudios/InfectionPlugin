#include "EclipseSpreadComponent.h"

#include "Components/MeshComponent.h"
#include "Components/SceneComponent.h"
#include "EclipseInfectionMap.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
	const FName GrowthParameter(TEXT("Growth"));
	const FName BurnSoftnessParameter(TEXT("BurnSoftness"));
	// The material reveals a point over this much growth after it arrives;
	// queries count it once it is half shown.
	constexpr float RevealMidpoint = 0.04f;
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
	if (AActor* Owner = GetOwner())
	{
		CollectFrom(Owner);
	}
}

void UEclipseSpreadComponent::CollectFrom(AActor* Actor)
{
	TInlineComponentArray<UMeshComponent*> Meshes(Actor);
	for (UMeshComponent* Mesh : Meshes)
	{
		for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
		{
			UMaterialInterface* Material = Mesh->GetMaterial(Slot);
			float Unused = 0.0f;
			// Only Eclipse materials carry both of these; leave every other material alone.
			if (!Material || !Material->GetScalarParameterValue(FHashedMaterialParameterInfo(GrowthParameter), Unused) ||
				!Material->GetScalarParameterValue(FHashedMaterialParameterInfo(BurnSoftnessParameter), Unused))
			{
				continue;
			}
			if (UMaterialInstanceDynamic* Dynamic = Mesh->CreateDynamicMaterialInstance(Slot, Material))
			{
				Materials.Add(Dynamic);
			}
		}
	}
	TArray<AActor*> Children;
	Actor->GetAttachedActors(Children);
	for (AActor* Child : Children)
	{
		// A separately grown host attached here plays its own spread.
		if (Child && !Child->FindComponentByClass<UEclipseSpreadComponent>())
		{
			CollectFrom(Child);
		}
	}
}

void UEclipseSpreadComponent::Apply()
{
	const AActor* Owner = GetOwner();
	const float Scale = Owner ? static_cast<float>(Owner->GetActorScale3D().GetAbsMax()) : 1.0f;
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
			FLinearColor World(0, 0, 0, 0);
			if (Burns.IsValidIndex(Index) && Owner)
			{
				const FVector4& B = Burns[Index];
				const FVector Centre = Owner->GetActorTransform().TransformPosition(FVector(B.X, B.Y, B.Z));
				World = FLinearColor(Centre.X, Centre.Y, Centre.Z, B.W * Scale);
			}
			Material->SetVectorParameterValue(BurnParameters[Index], World);
		}
	}
}

void UEclipseSpreadComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (PlayRate == 0.0f)
	{
		// Only burns on a moving actor still need following.
		Apply();
		SetComponentTickEnabled(NeedsTick());
		return;
	}
	Growth += PlayRate * (FullGrowth / FMath::Max(SpreadDuration, 0.01f)) * DeltaTime;
	bool bFinished = false, bReceded = false;
	if (Growth >= FullGrowth && PlayRate > 0.0f)
	{
		Growth = FMath::Max(FullGrowth, Growth - PlayRate * (FullGrowth / FMath::Max(SpreadDuration, 0.01f)) * DeltaTime);
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

bool UEclipseSpreadComponent::NeedsTick() const
{
	if (PlayRate != 0.0f)
	{
		return true;
	}
	const AActor* Owner = GetOwner();
	const USceneComponent* Root = Owner ? Owner->GetRootComponent() : nullptr;
	return Burns.Num() > 0 && Root && Root->Mobility == EComponentMobility::Movable;
}

void UEclipseSpreadComponent::SetPlayRate(float Rate)
{
	PlayRate = Rate;
	SetComponentTickEnabled(NeedsTick());
}

void UEclipseSpreadComponent::SetGrowth(float NewGrowth)
{
	Growth = FMath::Max(NewGrowth, 0.0f);
	Apply();
}

void UEclipseSpreadComponent::Burn(FVector WorldLocation, float Radius)
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	// Kept in the actor's space, so the burn moves with it.
	const FVector Local = Owner->GetActorTransform().InverseTransformPosition(WorldLocation);
	const float Scale = FMath::Max(static_cast<float>(Owner->GetActorScale3D().GetAbsMax()), KINDA_SMALL_NUMBER);
	const FVector4 Entry(Local.X, Local.Y, Local.Z, FMath::Max(Radius, 0.0f) / Scale);
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
	SetComponentTickEnabled(NeedsTick());
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
	// Everything in the actor's space, where the map and the burns live.
	const FVector Local = Owner->GetActorTransform().InverseTransformPosition(WorldLocation);
	const float Scale = FMath::Max(static_cast<float>(Owner->GetActorScale3D().GetAbsMax()), KINDA_SMALL_NUMBER);
	const float LocalSoftness = BurnSoftness / Scale;
	for (const FVector4& B : Burns)
	{
		// Burned until the material shows it at least half again.
		const float Edge = B.W + 0.5f * LocalSoftness;
		if (FVector::DistSquared(FVector(B.X, B.Y, B.Z), Local) < Edge * Edge)
		{
			return 0.0f;
		}
	}
	return InfectionMap->InfectionAt(Local, SearchRadius / Scale, Growth - RevealMidpoint);
}
