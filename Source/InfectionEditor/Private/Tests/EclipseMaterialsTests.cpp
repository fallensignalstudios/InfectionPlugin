#include "EclipseMaterials.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEclipseMaterialsBuildTest,
	"Infection.Editor.Materials.BuildsCarapaceAndFlesh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEclipseMaterialsBuildTest::RunTest(const FString& Parameters)
{
	// In-memory packages only; nothing is saved to disk.
	const FString Folder = TEXT("/Temp/InfectionPluginTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	FEclipseMaterials Materials;
	FText Error;
	if (!TestTrue(TEXT("The materials build"), FEclipseMaterials::Ensure(Folder, false, Materials, Error)))
	{
		AddError(Error.ToString());
		return false;
	}
	UMaterialInterface* All[] = {Materials.Overlay, Materials.Replace, Materials.Growths, Materials.Flesh};
	for (UMaterialInterface* Instance : All)
	{
		if (!TestNotNull(TEXT("Every instance exists"), Instance))
		{
			return false;
		}
		// The Eclipse Spread component drives exactly these two.
		float Value = 0.0f;
		TestTrue(TEXT("Every instance takes Growth"), Instance->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Growth")), Value));
		TestTrue(TEXT("Every instance takes BurnSoftness"),
			Instance->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("BurnSoftness")), Value));
		TestTrue(TEXT("Every instance takes DetailScale"),
			Instance->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("DetailScale")), Value));
	}
	const UMaterial* Carapace = Materials.Growths->GetMaterial();
	const UMaterial* Flesh = Materials.Flesh->GetMaterial();
	TestTrue(TEXT("Hard and soft tissue have their own masters"), Carapace != Flesh);
	TestTrue(TEXT("The carapace is clear-coated"), Carapace->GetShadingModels().HasShadingModel(MSM_ClearCoat));
	TestTrue(TEXT("Soft tissue scatters light"), Flesh->GetShadingModels().HasShadingModel(MSM_Subsurface));

	for (const TCHAR* Name : {TEXT("T_EclipseChitin"), TEXT("T_EclipseFlesh")})
	{
		const FString Path = Folder / TEXT("Materials") / Name + TEXT(".") + Name;
		const UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *Path);
		if (TestNotNull(TEXT("Each detail map exists"), Texture))
		{
			TestTrue(TEXT("Detail maps are 512 square"), Texture->Source.GetSizeX() == 512 && Texture->Source.GetSizeY() == 512);
			TestFalse(TEXT("Detail maps hold data, not colour"), static_cast<bool>(Texture->SRGB));
		}
	}

	// A second call reuses what exists instead of rebuilding it.
	FEclipseMaterials Again;
	TestTrue(TEXT("Ensuring again succeeds"), FEclipseMaterials::Ensure(Folder, false, Again, Error));
	TestTrue(TEXT("Ensuring again reuses the instances"), Again.Flesh == Materials.Flesh && Again.Overlay == Materials.Overlay);
	return true;
}

#endif
