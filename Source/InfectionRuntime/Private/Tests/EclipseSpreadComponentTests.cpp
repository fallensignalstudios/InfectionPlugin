#include "EclipseSpreadComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/SceneComponent.h"
#include "EclipseInfectionMap.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"

namespace
{

struct FSpreadQueryFixture
{
	UWorld* World = nullptr;
	AActor* Host = nullptr;
	UEclipseSpreadComponent* Spread = nullptr;

	FSpreadQueryFixture()
	{
		const UWorld::InitializationValues Init = UWorld::InitializationValues()
			.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false)
			.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
		World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
		if (!World)
		{
			return;
		}
		Host = World->SpawnActor<AActor>();
		if (!Host)
		{
			return;
		}
		USceneComponent* Root = NewObject<USceneComponent>(Host, NAME_None, RF_Transient);
		Host->SetRootComponent(Root);
		Host->AddInstanceComponent(Root);
		Root->RegisterComponent();
		Spread = NewObject<UEclipseSpreadComponent>(Host, NAME_None, RF_Transient);
		Host->AddInstanceComponent(Spread);
		Spread->RegisterComponent();
		Spread->InfectionMap = NewObject<UEclipseInfectionMap>(Spread, NAME_None, RF_Transient);
	}

	~FSpreadQueryFixture()
	{
		if (World)
		{
			World->DestroyWorld(false);
		}
	}

	void Sample(FVector3f Position, float Spacing = 1.0f)
	{
		Spread->InfectionMap->SetSamples(TArray<FVector3f>{Position}, TArray<float>{0.0f}, TArray<float>{1.0f}, Spacing);
	}
};

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEclipseSpreadCandidateBurnTest,
	"Infection.Runtime.Spread.FiltersBurnsAtCandidateSamples",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEclipseSpreadCandidateBurnTest::RunTest(const FString& Parameters)
{
	FSpreadQueryFixture Fixture;
	if (!TestNotNull(TEXT("The temporary spread component exists"), Fixture.Spread))
	{
		return false;
	}
	Fixture.Sample(FVector3f::ZeroVector);
	Fixture.Spread->Burn(FVector::ZeroVector, 20.0f);
	TestEqual(TEXT("A burned candidate is excluded even when the query centre is outside its burn"),
		Fixture.Spread->GetInfectionAt(FVector(50, 0, 0), 60.0f), 0.0f);

	Fixture.Sample(FVector3f(60, 0, 0));
	TestEqual(TEXT("A clean candidate is found even when the query centre is inside a burn"),
		Fixture.Spread->GetInfectionAt(FVector::ZeroVector, 65.0f), 1.0f);

	Fixture.Spread->ClearBurns();
	Fixture.Sample(FVector3f::ZeroVector);
	Fixture.Spread->Burn(FVector(-44, 0, 0), 20.0f);
	Fixture.Spread->Burn(FVector(44, 0, 0), 20.0f);
	TestEqual(TEXT("Two 0.6 burn masks multiply to 0.36 and hide the candidate"),
		Fixture.Spread->GetInfectionAt(FVector::ZeroVector, 0.0f), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEclipseSpreadBurnRadiusTest,
	"Infection.Runtime.Spread.MatchesMaterialBurnRadiusAndSoftness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEclipseSpreadBurnRadiusTest::RunTest(const FString& Parameters)
{
	FSpreadQueryFixture Fixture;
	if (!TestNotNull(TEXT("The temporary spread component exists"), Fixture.Spread))
	{
		return false;
	}
	Fixture.Sample(FVector3f::ZeroVector);
	Fixture.Spread->Burn(FVector::ZeroVector, 0.0f);
	TestEqual(TEXT("The material ignores a zero-radius burn"),
		Fixture.Spread->GetInfectionAt(FVector::ZeroVector, 0.0f), 1.0f);
	Fixture.Spread->ClearBurns();
	Fixture.Spread->Burn(FVector::ZeroVector, -10.0f);
	TestEqual(TEXT("A negative radius is clamped to an inactive burn"),
		Fixture.Spread->GetInfectionAt(FVector::ZeroVector, 0.0f), 1.0f);
	Fixture.Spread->ClearBurns();
	Fixture.Spread->Burn(FVector::ZeroVector, 20.0f);
	for (int32 Index = 0; Index <= UEclipseSpreadComponent::MaxBurns; ++Index)
	{
		Fixture.Spread->Burn(FVector(1000 + Index * 100, 0, 0), Index % 2 == 0 ? 0.0f : -10.0f);
	}
	TestEqual(TEXT("Inactive burns cannot evict the active burn from the four-slot ring"),
		Fixture.Spread->GetInfectionAt(FVector::ZeroVector, 0.0f), 0.0f);

	Fixture.Spread->ClearBurns();
	Fixture.Sample(FVector3f(0.25f, 0, 0));
	Fixture.Spread->BurnSoftness = 0.0f;
	Fixture.Spread->Burn(FVector::ZeroVector, 0.1f);
	TestEqual(TEXT("Burn softness has the material's minimum of one world centimetre"),
		Fixture.Spread->GetInfectionAt(FVector(0.25f, 0, 0), 0.0f), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEclipseSpreadWorldDistanceTest,
	"Infection.Runtime.Spread.UsesWorldDistancesOnScaledHosts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEclipseSpreadWorldDistanceTest::RunTest(const FString& Parameters)
{
	FSpreadQueryFixture Fixture;
	if (!TestNotNull(TEXT("The temporary spread component exists"), Fixture.Spread))
	{
		return false;
	}
	Fixture.Host->SetActorScale3D(FVector(10, 10, 10));
	// Five centimetres of world sampling spacing becomes 0.5 in this actor's space.
	Fixture.Sample(FVector3f::ZeroVector, 0.5f);
	TestEqual(TEXT("Uniform scale keeps the five-centimetre sampling tolerance"),
		Fixture.Spread->GetInfectionAt(FVector(0, 0, 4), 0.0f), 1.0f);
	TestEqual(TEXT("Uniform scale keeps SearchRadius in world centimetres"),
		Fixture.Spread->GetInfectionAt(FVector(0, 0, 40), 40.0f), 1.0f);
	Fixture.Spread->Burn(FVector::ZeroVector, 20.0f);
	TestEqual(TEXT("Uniformly scaled burned samples cannot leak into a nearby clean query"),
		Fixture.Spread->GetInfectionAt(FVector(50, 0, 0), 60.0f), 0.0f);

	Fixture.Spread->ClearBurns();
	Fixture.Host->SetActorScale3D(FVector(10, 1, 1));
	Fixture.Sample(FVector3f::ZeroVector, 5.0f);
	TestEqual(TEXT("Nonuniform scale does not shrink the search on the short axis"),
		Fixture.Spread->GetInfectionAt(FVector(0, 40, 0), 40.0f), 1.0f);
	TestEqual(TEXT("Nonuniform scale does not stretch the sampling tolerance on the long axis"),
		Fixture.Spread->GetInfectionAt(FVector(40, 0, 0), 0.0f), 0.0f);

	Fixture.Sample(FVector3f(0, 30, 0), 5.0f);
	Fixture.Spread->Burn(FVector::ZeroVector, 20.0f);
	TestEqual(TEXT("Nonuniform scale uses the material's world-space burn sphere"),
		Fixture.Spread->GetInfectionAt(FVector(0, 30, 0), 0.0f), 0.0f);
	Fixture.Spread->ClearBurns();
	TestEqual(TEXT("Clearing the burn restores the sample"),
		Fixture.Spread->GetInfectionAt(FVector(0, 30, 0), 0.0f), 1.0f);

	Fixture.Host->SetActorScale3D(FVector(10, 0, 1));
	Fixture.Sample(FVector3f(0, 1000, 0), 5.0f);
	TestEqual(TEXT("A collapsed axis uses the sample's world position without an inverse-distance bound"),
		Fixture.Spread->GetInfectionAt(FVector::ZeroVector, 0.0f), 1.0f);
	return true;
}

#endif
