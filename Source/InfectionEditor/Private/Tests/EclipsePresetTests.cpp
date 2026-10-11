#include "EclipseGrowthSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEclipsePresetTest,
	"Infection.Editor.Settings.PresetsRewriteTheLook",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEclipsePresetTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UEclipseGrowthSettings> Settings(NewObject<UEclipseGrowthSettings>(GetTransientPackage()));
	Settings->Seed = 7;
	Settings->Coverage = 0.3f;
	Settings->FeatureScale = 2.0f;

	Settings->Preset = EEclipsePreset::Thornfield;
	Settings->ApplyPreset();
	TestTrue(TEXT("Thornfield packs horns closer"), Settings->ThornSpacing < 6.0f);
	TestTrue(TEXT("Thornfield adds barbs"), Settings->HornBarbs > 5);
	TestFalse(TEXT("Thornfield drops sacs"), Settings->bSacs);

	Settings->Preset = EEclipsePreset::Cathedral;
	Settings->ApplyPreset();
	TestTrue(TEXT("Cathedral raises spires"), Settings->bSpires);
	TestTrue(TEXT("Cathedral lays ribcages"), Settings->bRibcages);

	Settings->Preset = EEclipsePreset::Eclipse;
	Settings->ApplyPreset();
	TestFalse(TEXT("The default look has no set pieces"), Settings->bSpires || Settings->bRibcages || Settings->bPods || Settings->bShelves);
	TestEqual(TEXT("Thorn spacing returns to its default"), Settings->ThornSpacing, 6.0f);

	// Framing is left alone.
	TestEqual(TEXT("Seed is kept"), Settings->Seed, 7);
	TestEqual(TEXT("Coverage is kept"), Settings->Coverage, 0.3f);
	TestEqual(TEXT("Feature scale is kept"), Settings->FeatureScale, 2.0f);
	return true;
}

#endif
