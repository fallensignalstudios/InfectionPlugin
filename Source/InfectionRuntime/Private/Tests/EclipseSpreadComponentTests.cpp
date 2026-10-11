#include "EclipseSpreadComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/SceneComponent.h"
#include "EclipseInfectionMap.h"
#include "EclipseInfectionSensorComponent.h"
#include "EclipseInfectionSubsystem.h"
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
	TestEqual(TEXT("Inactive burns cannot evict or merge into the active burn"),
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEclipseSpreadManyBurnsTest,
	"Infection.Runtime.Spread.KeepsEveryBurnScar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEclipseSpreadManyBurnsTest::RunTest(const FString& Parameters)
{
	FSpreadQueryFixture Fixture;
	if (!TestNotNull(TEXT("The temporary spread component exists"), Fixture.Spread))
	{
		return false;
	}
	Fixture.Sample(FVector3f::ZeroVector);
	Fixture.Spread->Burn(FVector::ZeroVector, 20.0f);
	for (int32 Index = 0; Index < 8; ++Index)
	{
		Fixture.Spread->Burn(FVector(1000 + Index * 100, 0, 0), 20.0f);
	}
	TestEqual(TEXT("More than four burns are all kept"), Fixture.Spread->GetBurnCount(), 9);
	TestEqual(TEXT("The first burn still scars after eight more"), Fixture.Spread->GetInfectionAt(FVector::ZeroVector, 0.0f), 0.0f);

	for (int32 Index = 0; Index < UEclipseSpreadComponent::MaxBurns * 2; ++Index)
	{
		Fixture.Spread->Burn(FVector(5000 + Index * 100, 0, 0), 20.0f);
	}
	TestEqual(TEXT("Burns beyond the limit merge instead of growing the list"),
		Fixture.Spread->GetBurnCount(), UEclipseSpreadComponent::MaxBurns);
	TestEqual(TEXT("Merging never lets a scar heal"), Fixture.Spread->GetInfectionAt(FVector::ZeroVector, 0.0f), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEclipseSpreadOutbreakTest,
	"Infection.Runtime.Spread.OutbreakRevealsFromItsCentre",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEclipseSpreadOutbreakTest::RunTest(const FString& Parameters)
{
	FSpreadQueryFixture Fixture;
	if (!TestNotNull(TEXT("The temporary spread component exists"), Fixture.Spread))
	{
		return false;
	}
	UEclipseSpreadComponent* Spread = Fixture.Spread;
	Fixture.Sample(FVector3f::ZeroVector);
	Spread->SetGrowth(0.0f);
	TestEqual(TEXT("Nothing shows before the spread plays"), Spread->GetInfectionAt(FVector::ZeroVector, 0.0f), 0.0f);

	// The sample arrived at 0, so it does not lag: it shows once the front is
	// half of OutbreakSoftness past it, 115 cm from a centre 100 cm away.
	Spread->OutbreakSoftness = 30.0f;
	Spread->StartOutbreak(FVector(100, 0, 0), 300.0f, 100.0f);
	TestEqual(TEXT("An outbreak is under way"), Spread->GetOutbreakCount(), 1);
	TestEqual(TEXT("An outbreak starts from nothing"), Spread->GetInfectionAt(FVector::ZeroVector, 0.0f), 0.0f);
	Spread->TickComponent(1.0f, LEVELTICK_All, &Spread->PrimaryComponentTick);
	TestEqual(TEXT("The front has not reached the sample after one second"), Spread->GetInfectionAt(FVector::ZeroVector, 0.0f), 0.0f);
	Spread->TickComponent(0.5f, LEVELTICK_All, &Spread->PrimaryComponentTick);
	TestEqual(TEXT("The front has passed the sample after one and a half"), Spread->GetInfectionAt(FVector::ZeroVector, 0.0f), 1.0f);

	Spread->Burn(FVector::ZeroVector, 20.0f);
	TestEqual(TEXT("A burn still cuts an outbreak"), Spread->GetInfectionAt(FVector::ZeroVector, 0.0f), 0.0f);
	Spread->ClearBurns();
	Spread->ClearOutbreaks();
	TestEqual(TEXT("Clearing outbreaks hides it again"), Spread->GetInfectionAt(FVector::ZeroVector, 0.0f), 0.0f);

	Spread->StartOutbreak(FVector(100, 0, 0), 300.0f, 0.0f);
	TestEqual(TEXT("Speed 0 reveals the whole sphere at once"), Spread->GetInfectionAt(FVector::ZeroVector, 0.0f), 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEclipseSubsystemTest,
	"Infection.Runtime.Subsystem.RoutesToNearbySpreads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEclipseSubsystemTest::RunTest(const FString& Parameters)
{
	FSpreadQueryFixture Fixture;
	if (!TestNotNull(TEXT("The temporary spread component exists"), Fixture.Spread))
	{
		return false;
	}
	UEclipseInfectionSubsystem* Infection = Fixture.World->GetSubsystem<UEclipseInfectionSubsystem>();
	if (!TestNotNull(TEXT("The world has an infection subsystem"), Infection))
	{
		return false;
	}
	Fixture.Sample(FVector3f::ZeroVector);
	TestEqual(TEXT("The subsystem finds the registered spread"), Infection->GetInfectionAt(FVector::ZeroVector, 10.0f), 1.0f);
	TestEqual(TEXT("Nothing is far from it"), Infection->GetInfectionAt(FVector(5000, 0, 0), 10.0f), 0.0f);
	TestEqual(TEXT("A burn far away reaches no spread"), Infection->BurnAt(FVector(5000, 0, 0), 20.0f), 0);
	TestEqual(TEXT("A burn on the corruption reaches its spread"), Infection->BurnAt(FVector::ZeroVector, 20.0f), 1);
	TestTrue(TEXT("and burns it"), !Infection->IsInfectedAt(FVector::ZeroVector, 0.0f));

	Fixture.Spread->ClearBurns();
	Fixture.Spread->SetGrowth(0.0f);
	TestEqual(TEXT("An outbreak on the corruption reaches its spread"), Infection->StartOutbreakAt(FVector::ZeroVector, 200.0f, 0.0f), 1);
	TestTrue(TEXT("and reveals it"), Infection->IsInfectedAt(FVector::ZeroVector, 0.0f));

	Fixture.Spread->UnregisterComponent();
	TestEqual(TEXT("An unregistered spread is forgotten"), Infection->GetInfectionAt(FVector::ZeroVector, 10.0f), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEclipseSensorTest,
	"Infection.Runtime.Sensor.EntersAndLeaves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEclipseSensorTest::RunTest(const FString& Parameters)
{
	FSpreadQueryFixture Fixture;
	if (!TestNotNull(TEXT("The temporary spread component exists"), Fixture.Spread))
	{
		return false;
	}
	Fixture.Sample(FVector3f::ZeroVector);
	AActor* Walker = Fixture.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("The walker exists"), Walker))
	{
		return false;
	}
	UEclipseInfectionSensorComponent* Sensor = NewObject<UEclipseInfectionSensorComponent>(Walker, NAME_None, RF_Transient);
	Walker->SetRootComponent(Sensor);
	Walker->AddInstanceComponent(Sensor);
	Sensor->RegisterComponent();
	Sensor->SearchRadius = 10.0f;

	Walker->SetActorLocation(FVector(500, 0, 0));
	Sensor->Sense();
	TestFalse(TEXT("Off the corruption"), Sensor->IsInInfection());
	Walker->SetActorLocation(FVector(0, 0, 5));
	Sensor->Sense();
	TestTrue(TEXT("Stepped onto it"), Sensor->IsInInfection());
	TestEqual(TEXT("Senses its strength"), Sensor->GetInfection(), 1.0f);
	Fixture.Spread->Burn(FVector::ZeroVector, 40.0f);
	Sensor->Sense();
	TestFalse(TEXT("Burned away under it"), Sensor->IsInInfection());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEclipseSpreadStateTest,
	"Infection.Runtime.Spread.SavesAndRestoresState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEclipseSpreadStateTest::RunTest(const FString& Parameters)
{
	FSpreadQueryFixture Saved;
	FSpreadQueryFixture Loaded;
	if (!TestNotNull(TEXT("The saved spread exists"), Saved.Spread) || !TestNotNull(TEXT("The loaded spread exists"), Loaded.Spread))
	{
		return false;
	}
	TestTrue(TEXT("The spread replicates by default"), Saved.Spread->GetIsReplicated());

	Saved.Sample(FVector3f::ZeroVector);
	Loaded.Sample(FVector3f::ZeroVector);
	Saved.Spread->SetGrowth(0.0f);
	Saved.Spread->StartOutbreak(FVector(100, 0, 0), 300.0f, 100.0f);
	Saved.Spread->TickComponent(1.5f, LEVELTICK_All, &Saved.Spread->PrimaryComponentTick);
	Saved.Spread->Burn(FVector(0, 500, 0), 20.0f);
	const FEclipseSpreadState State = Saved.Spread->GetState();
	TestEqual(TEXT("The state holds the burn"), State.Burns.Num(), 1);
	TestEqual(TEXT("The state holds the outbreak"), State.Outbreaks.Num(), 1);
	TestEqual(TEXT("with its front where it had reached"), State.Outbreaks[0].Radius, 150.0f);

	Loaded.Spread->RestoreState(State);
	TestEqual(TEXT("Growth comes back"), Loaded.Spread->GetGrowth(), 0.0f);
	TestEqual(TEXT("Burns come back"), Loaded.Spread->GetBurnCount(), 1);
	TestEqual(TEXT("Outbreaks come back"), Loaded.Spread->GetOutbreakCount(), 1);
	TestEqual(TEXT("A restored outbreak shows what it had revealed, without starting over"),
		Loaded.Spread->GetInfectionAt(FVector::ZeroVector, 0.0f), 1.0f);

	FEclipseSpreadState Huge = State;
	Huge.Burns.Init(FVector4(0, 0, 0, 1), UEclipseSpreadComponent::MaxBurns + 5);
	Loaded.Spread->RestoreState(Huge);
	TestEqual(TEXT("A restored state is held to the burn limit"), Loaded.Spread->GetBurnCount(), UEclipseSpreadComponent::MaxBurns);
	return true;
}

#endif
