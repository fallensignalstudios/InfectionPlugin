#include "EclipseInfectionMap.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEclipseInfectionMapQueryTest,
	"Infection.Runtime.InfectionMap.AnswersByDistanceAndGrowth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEclipseInfectionMapQueryTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UEclipseInfectionMap> Map(NewObject<UEclipseInfectionMap>(GetTransientPackage(), NAME_None, RF_Transient));
	// A line of samples along X, arriving later the further out they are.
	TArray<FVector3f> Points;
	TArray<float> Arrival, Infection;
	for (int32 Index = 0; Index < 20; ++Index)
	{
		Points.Add(FVector3f(Index * 5.0f, 0.0f, 0.0f));
		Arrival.Add(Index / 19.0f);
		Infection.Add(Index < 10 ? 1.0f : 0.5f);
	}
	Map->SetSamples(MoveTemp(Points), MoveTemp(Arrival), MoveTemp(Infection), 5.0f);

	TestEqual(TEXT("A sample that has arrived is found"), Map->InfectionAt(FVector(0, 0, 0), 1.0f, 0.5f), 1.0f);
	TestEqual(TEXT("Nothing has arrived before the growth starts"), Map->InfectionAt(FVector(0, 0, 0), 1.0f, -0.1f), 0.0f);
	TestEqual(TEXT("Far tissue has not arrived at half growth"), Map->InfectionAt(FVector(95, 0, 0), 1.0f, 0.5f), 0.0f);
	TestEqual(TEXT("Far tissue arrives at full growth"), Map->InfectionAt(FVector(95, 0, 0), 1.0f, 1.0f), 0.5f);
	TestEqual(TEXT("Nothing is found away from the corruption"), Map->InfectionAt(FVector(0, 500, 0), 30.0f, 1.0f), 0.0f);
	TestEqual(TEXT("The strongest sample in reach wins"), Map->InfectionAt(FVector(50, 0, 0), 10.0f, 1.0f), 1.0f);
	return true;
}

#endif
