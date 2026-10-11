#include "EclipseSpreadComponent.h"

#include "Components/MeshComponent.h"
#include "Components/SceneComponent.h"
#include "EclipseInfectionMap.h"
#include "EclipseInfectionSubsystem.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
	const FName GrowthParameter(TEXT("Growth"));
	const FName BurnSoftnessParameter(TEXT("BurnSoftness"));
	const FName OutbreakSoftnessParameter(TEXT("OutbreakSoftness"));
	const FName OutbreakRoughnessParameter(TEXT("OutbreakRoughness"));
	const FName EventOriginParameter(TEXT("EventOrigin"));
	const FName EventsParameter(TEXT("Events"));
	const FName BurnCountParameter(TEXT("BurnCount"));
	const FName OutbreakCountParameter(TEXT("OutbreakCount"));
	// The material reveals a point over this much growth after it arrives;
	// queries count it once it is half shown.
	constexpr float RevealMidpoint = 0.04f;
	// How much an arrival can delay an outbreak's front (in units of
	// OutbreakRoughness); matches the material.
	constexpr float MaxOutbreakLag = 1.5f;

	static_assert(UEclipseSpreadComponent::MaxBurns == UEclipseSpreadComponent::MaxOutbreaks,
		"Burns and outbreaks share one event texture, a row each");
}

UEclipseSpreadComponent::UEclipseSpreadComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;  // ticks only while something moves
}

void UEclipseSpreadComponent::OnRegister()
{
	Super::OnRegister();
	if (UWorld* World = GetWorld())
	{
		if (UEclipseInfectionSubsystem* Infection = World->GetSubsystem<UEclipseInfectionSubsystem>())
		{
			Infection->Register(this);
		}
	}
}

void UEclipseSpreadComponent::OnUnregister()
{
	if (UWorld* World = GetWorld())
	{
		if (UEclipseInfectionSubsystem* Infection = World->GetSubsystem<UEclipseInfectionSubsystem>())
		{
			Infection->Unregister(this);
		}
	}
	Super::OnUnregister();
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
	if (Materials.Num() == 0)
	{
		return;
	}
	// Burns in one row, outbreaks in the next; the materials Load() texels
	// by index, so no filtering or mips.
	EventTexture = UTexture2D::CreateTransient(MaxBurns, 2, PF_A32B32G32R32F);
	if (EventTexture)
	{
		EventTexture->Filter = TF_Nearest;
		EventTexture->SRGB = false;
		EventTexture->AddressX = TA_Clamp;
		EventTexture->AddressY = TA_Clamp;
		EventTexture->NeverStream = true;
		EventTexture->UpdateResource();
		for (UMaterialInstanceDynamic* Material : Materials)
		{
			Material->SetTextureParameterValue(EventsParameter, EventTexture);
		}
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

void UEclipseSpreadComponent::UpdateEvents(const FVector& EventOrigin)
{
	if (!EventTexture || !EventTexture->GetResource())
	{
		return;  // no render resource (a null RHI): nothing would free the upload
	}
	const AActor* Owner = GetOwner();
	const FTransform ActorTransform = Owner ? Owner->GetActorTransform() : FTransform::Identity;
	const float Scale = static_cast<float>(ActorTransform.GetScale3D().GetAbsMax());
	// One heap copy per update, freed by the render thread once uploaded.
	FLinearColor* Texels = new FLinearColor[MaxBurns * 2];
	for (int32 Index = 0; Index < MaxBurns * 2; ++Index)
	{
		Texels[Index] = FLinearColor(0, 0, 0, 0);
	}
	for (int32 Index = 0; Index < Burns.Num(); ++Index)
	{
		const FVector4& B = Burns[Index];
		const FVector Relative = ActorTransform.TransformPosition(FVector(B.X, B.Y, B.Z)) - EventOrigin;
		Texels[Index] = FLinearColor(Relative.X, Relative.Y, Relative.Z, B.W * Scale);
	}
	for (int32 Index = 0; Index < Outbreaks.Num(); ++Index)
	{
		const FOutbreak& O = Outbreaks[Index];
		const FVector Relative = ActorTransform.TransformPosition(O.Local) - EventOrigin;
		Texels[MaxBurns + Index] = FLinearColor(Relative.X, Relative.Y, Relative.Z, O.Radius);
	}
	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, MaxBurns, 2);
	EventTexture->UpdateTextureRegions(0, 1, Region, MaxBurns * sizeof(FLinearColor), sizeof(FLinearColor),
		reinterpret_cast<uint8*>(Texels),
		[](uint8* Data, const FUpdateTextureRegion2D* Regions)
		{
			delete[] reinterpret_cast<FLinearColor*>(Data);
			delete Regions;
		});
}

void UEclipseSpreadComponent::Apply()
{
	const AActor* Owner = GetOwner();
	// Event positions are relative to this point, in the same float precision
	// the material subtracts it with, so they stay exact far from the origin.
	const FVector3f OriginF = Owner ? FVector3f(Owner->GetActorLocation()) : FVector3f::ZeroVector;
	const FVector EventOrigin(OriginF);
	for (UMaterialInstanceDynamic* Material : Materials)
	{
		if (!Material)
		{
			continue;
		}
		Material->SetScalarParameterValue(GrowthParameter, Growth);
		Material->SetScalarParameterValue(BurnSoftnessParameter, BurnSoftness);
		Material->SetScalarParameterValue(OutbreakSoftnessParameter, OutbreakSoftness);
		Material->SetScalarParameterValue(OutbreakRoughnessParameter, OutbreakRoughness);
		Material->SetScalarParameterValue(BurnCountParameter, static_cast<float>(Burns.Num()));
		Material->SetScalarParameterValue(OutbreakCountParameter, static_cast<float>(Outbreaks.Num()));
		Material->SetVectorParameterValue(EventOriginParameter, FLinearColor(OriginF.X, OriginF.Y, OriginF.Z, 0.0f));
	}
	UpdateEvents(EventOrigin);
}

void UEclipseSpreadComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Outbreaks advance on their own clocks, whatever the spread is doing.
	const AActor* Owner = GetOwner();
	TArray<TPair<FVector, float>> Finished;
	for (FOutbreak& O : Outbreaks)
	{
		if (O.Radius < O.MaxRadius)
		{
			O.Radius = FMath::Min(O.MaxRadius, O.Radius + O.Speed * DeltaTime);
			if (O.Radius >= O.MaxRadius && Owner)
			{
				Finished.Emplace(Owner->GetActorTransform().TransformPosition(O.Local), O.MaxRadius);
			}
		}
	}

	bool bSpreadFinished = false, bReceded = false;
	if (PlayRate != 0.0f)
	{
		const float Step = PlayRate * (FullGrowth / FMath::Max(SpreadDuration, 0.01f)) * DeltaTime;
		Growth += Step;
		if (Growth >= FullGrowth && PlayRate > 0.0f)
		{
			Growth = FMath::Max(FullGrowth, Growth - Step);
			bSpreadFinished = true;
		}
		else if (Growth <= 0.0f && PlayRate < 0.0f)
		{
			Growth = 0.0f;
			bReceded = true;
		}
	}
	Apply();
	if (bSpreadFinished || bReceded)
	{
		SetPlayRate(0.0f);
		(bSpreadFinished ? OnSpreadFinished : OnFullyReceded).Broadcast();
	}
	for (const TPair<FVector, float>& Done : Finished)
	{
		OnOutbreakFinished.Broadcast(Done.Key, Done.Value);
	}
	SetComponentTickEnabled(NeedsTick());
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
	for (const FOutbreak& O : Outbreaks)
	{
		if (O.Radius < O.MaxRadius)
		{
			return true;
		}
	}
	// Burns and outbreaks on a moving actor still need following.
	const AActor* Owner = GetOwner();
	const USceneComponent* Root = Owner ? Owner->GetRootComponent() : nullptr;
	return (Burns.Num() > 0 || Outbreaks.Num() > 0) && Root && Root->Mobility == EComponentMobility::Movable;
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
	if (!Owner || Radius <= 0.0f)
	{
		return;  // an inactive burn must not merge into, or displace, a real one
	}
	// Kept in the actor's space, so the burn moves with it.
	const FTransform ActorTransform = Owner->GetActorTransform();
	const FVector Local = ActorTransform.InverseTransformPosition(WorldLocation);
	const float Scale = FMath::Max(static_cast<float>(ActorTransform.GetScale3D().GetAbsMax()), KINDA_SMALL_NUMBER);
	FVector4 Entry(Local.X, Local.Y, Local.Z, Radius / Scale);
	if (Burns.Num() < MaxBurns)
	{
		Burns.Add(Entry);
	}
	else
	{
		// Full: grow the nearest scar into one sphere around both, so no
		// burned tissue ever comes back.
		int32 Nearest = 0;
		double NearestDistance = TNumericLimits<double>::Max();
		for (int32 Index = 0; Index < Burns.Num(); ++Index)
		{
			const double D = FVector::Distance(FVector(Burns[Index].X, Burns[Index].Y, Burns[Index].Z), Local) - Burns[Index].W;
			if (D < NearestDistance)
			{
				NearestDistance = D;
				Nearest = Index;
			}
		}
		const FVector4& Old = Burns[Nearest];
		const FVector OldCentre(Old.X, Old.Y, Old.Z);
		const double Gap = FVector::Distance(OldCentre, Local);
		if (Gap + Entry.W <= Old.W)
		{
			Entry = Old;  // already inside the old scar
		}
		else if (Gap + Old.W <= Entry.W)
		{
			// the new burn swallows the old one
		}
		else
		{
			const double R = (Gap + Old.W + Entry.W) * 0.5;
			const FVector Centre = OldCentre + (Local - OldCentre).GetSafeNormal() * (R - Old.W);
			Entry = FVector4(Centre.X, Centre.Y, Centre.Z, R);
		}
		Burns[Nearest] = Entry;
	}
	Apply();
	SetComponentTickEnabled(NeedsTick());
	OnBurned.Broadcast(ActorTransform.TransformPosition(FVector(Entry.X, Entry.Y, Entry.Z)), Entry.W * Scale);
}

void UEclipseSpreadComponent::ClearBurns()
{
	Burns.Reset();
	Apply();
}

void UEclipseSpreadComponent::StartOutbreak(FVector WorldLocation, float MaxRadius, float Speed)
{
	const AActor* Owner = GetOwner();
	if (!Owner || MaxRadius <= 0.0f || Outbreaks.Num() >= MaxOutbreaks)
	{
		return;
	}
	FOutbreak O;
	O.Local = Owner->GetActorTransform().InverseTransformPosition(WorldLocation);
	O.Radius = 0.0f;
	O.MaxRadius = MaxRadius;
	O.Speed = FMath::Max(Speed, 0.0f);
	if (O.Speed <= 0.0f)
	{
		O.Radius = MaxRadius;  // all at once
	}
	Outbreaks.Add(O);
	Apply();
	SetComponentTickEnabled(NeedsTick());
	OnOutbreakStarted.Broadcast(WorldLocation, MaxRadius);
	if (O.Radius >= O.MaxRadius)
	{
		OnOutbreakFinished.Broadcast(WorldLocation, MaxRadius);
	}
}

void UEclipseSpreadComponent::ClearOutbreaks()
{
	Outbreaks.Reset();
	Apply();
	SetComponentTickEnabled(NeedsTick());
}

float UEclipseSpreadComponent::OutbreakReveal(const FVector& WorldPoint, float Arrival) const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return 0.0f;
	}
	const FTransform ActorTransform = Owner->GetActorTransform();
	const double Softness = FMath::Max(OutbreakSoftness, 1.0f);
	const double Lag = FMath::Min(Arrival, MaxOutbreakLag) * OutbreakRoughness;
	double Best = 0.0;
	for (const FOutbreak& O : Outbreaks)
	{
		const double D = FVector::Distance(WorldPoint, ActorTransform.TransformPosition(O.Local));
		Best = FMath::Max(Best, FMath::Clamp((O.Radius - D - Lag) / Softness, 0.0, 1.0));
	}
	return static_cast<float>(Best);
}

FBox UEclipseSpreadComponent::GetWorldBounds() const
{
	const AActor* Owner = GetOwner();
	if (!InfectionMap || !Owner)
	{
		return FBox(ForceInit);
	}
	const FBox LocalBounds = InfectionMap->GetBounds();
	return LocalBounds.IsValid ? LocalBounds.TransformBy(Owner->GetActorTransform()).ExpandBy(InfectionMap->Spacing)
							   : FBox(ForceInit);
}

float UEclipseSpreadComponent::GetInfectionAt(FVector WorldLocation, float SearchRadius) const
{
	const AActor* Owner = GetOwner();
	if (!InfectionMap || !Owner)
	{
		return 0.0f;
	}
	const FTransform ActorTransform = Owner->GetActorTransform();
	const FVector Local = ActorTransform.InverseTransformPosition(WorldLocation);
	const float MinScale = static_cast<float>(ActorTransform.GetScale3D().GetAbsMin());
	const float MaxScale = static_cast<float>(ActorTransform.GetScale3D().GetAbsMax());
	const float WorldRadius = FMath::Max(SearchRadius, 0.0f);
	// The smallest scale gives a conservative local search under nonuniform
	// scaling. Check its candidates in world space so the long axis does not
	// stretch the query or the sampling tolerance.
	const double WorldReach = WorldRadius + InfectionMap->Spacing * MinScale;
	const double Softness = FMath::Max(BurnSoftness, 1.0f);
	const float Shown = Growth - RevealMidpoint;
	auto AcceptSample = [&](const FVector3f& Point, float Arrival)
	{
		const FVector WorldPoint = ActorTransform.TransformPosition(FVector(Point));
		if (FVector::DistSquared(WorldPoint, WorldLocation) > WorldReach * WorldReach)
		{
			return false;
		}
		// Arrived by the spread, or revealed by an outbreak, at least half way.
		if (Arrival > Shown && OutbreakReveal(WorldPoint, Arrival) < 0.5f)
		{
			return false;
		}
		// Match the material's product of world-space burn masks at this
		// sample, rather than testing the query centre against each burn.
		double BurnMask = 1.0;
		for (const FVector4& B : Burns)
		{
			const double Radius = B.W * MaxScale;
			if (Radius <= 0.0)
			{
				continue;  // the material ignores nonpositive burn radii
			}
			const FVector Centre = ActorTransform.TransformPosition(FVector(B.X, B.Y, B.Z));
			BurnMask *= FMath::Clamp((FVector::Distance(WorldPoint, Centre) - Radius) / Softness, 0.0, 1.0);
			if (BurnMask < 0.5)
			{
				return false;
			}
		}
		return true;
	};
	// With outbreaks, tissue that has not arrived by Growth may still show:
	// let every sample through to the check above.
	const float Horizon = Outbreaks.Num() > 0 ? TNumericLimits<float>::Max() : Shown;
	if (MinScale <= SMALL_NUMBER)
	{
		// Collapsed axes have no inverse local-distance bound; the world
		// predicate still gives the correct answer when scanning all samples.
		return InfectionMap->InfectionAt(Horizon, AcceptSample);
	}
	return InfectionMap->InfectionAt(Local, WorldRadius / MinScale, Horizon, AcceptSample);
}
