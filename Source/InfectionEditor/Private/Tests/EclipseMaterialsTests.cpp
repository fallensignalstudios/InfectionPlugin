#include "EclipseMaterials.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Texture2D.h"
#include "MaterialEditingLibrary.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "RHIGlobals.h"
#include "RHIShaderPlatform.h"
#include "ShaderCompiler.h"

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
		// The Eclipse Spread component drives these.
		float Value = 0.0f;
		TestTrue(TEXT("Every instance takes Growth"), Instance->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Growth")), Value));
		TestTrue(TEXT("Every instance takes BurnSoftness"),
			Instance->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("BurnSoftness")), Value));
		TestTrue(TEXT("Every instance takes DetailScale"),
			Instance->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("DetailScale")), Value));
		for (const TCHAR* Name : {TEXT("BurnCount"), TEXT("OutbreakCount"), TEXT("OutbreakSoftness"), TEXT("OutbreakRoughness")})
		{
			TestTrue(FString::Printf(TEXT("Every instance takes %s"), Name),
				Instance->GetScalarParameterValue(FHashedMaterialParameterInfo(Name), Value));
		}
		UTexture* Events = nullptr;
		TestTrue(TEXT("Every instance takes the Events texture"),
			Instance->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("Events")), Events));
		TestNotNull(TEXT("with an empty default"), Events);
	}
	float Reach = 0.0f;
	TestTrue(TEXT("Surfaces carry the halo"),
		Materials.Overlay->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("HaloReach")), Reach));
	TestEqual(TEXT("The halo reaches as far as the shells are grown"), Reach, FEclipseMaterialOptions().HaloReach);
	UMaterial* Carapace = Materials.Growths->GetMaterial();
	UMaterial* Flesh = Materials.Flesh->GetMaterial();
	if (!TestNotNull(TEXT("The carapace master exists"), Carapace) ||
		!TestNotNull(TEXT("The flesh master exists"), Flesh))
	{
		return false;
	}
	TestTrue(TEXT("Hard and soft tissue have their own masters"), Carapace != Flesh);
	TestTrue(TEXT("The carapace is clear-coated"), Carapace->GetShadingModels().HasShadingModel(MSM_ClearCoat));
	TestTrue(TEXT("Soft tissue scatters light"), Flesh->GetShadingModels().HasShadingModel(MSM_Subsurface));
	if (GUsingNullRHI)
	{
		AddInfo(TEXT("NullRHI: shader validation is skipped; structural material checks still run. Run on a rendering RHI to validate the generated HLSL."));
	}
	else
	{
		for (UMaterial* Master : {Carapace, Flesh})
		{
			const FString Name = Master->GetName();
			FMaterialResource* Resource = Master->GetMaterialResource(GMaxRHIShaderPlatform);
			if (!TestNotNull(Name + TEXT(" has a resource for the current shader platform"), Resource))
			{
				continue;
			}
			// Match MaterialEditingLibrary::GetStatistics: lazy shader jobs may
			// need submitting before FinishCompilation can wait for them.
			if (!Resource->IsGameThreadShaderMapComplete())
			{
				Resource->SubmitCompileJobs_GameThread(EShaderCompileJobPriority::High);
			}
			Resource->FinishCompilation();
			TestTrue(Name + TEXT(" finishes shader compilation"), Resource->IsCompilationFinished());
			for (const FString& CompileError : Resource->GetCompileErrors())
			{
				AddError(Name + TEXT(": ") + CompileError);
			}
			TestTrue(Name + TEXT(" has no shader compiler errors"), Resource->GetCompileErrors().IsEmpty());
			TestTrue(Name + TEXT(" has a complete shader map"), Resource->IsGameThreadShaderMapComplete());
			const FMaterialStatistics Statistics = UMaterialEditingLibrary::GetStatistics(Master);
			TestTrue(Name + TEXT(" has compiled pixel shader instructions"), Statistics.NumPixelShaderInstructions > 0);
			TestTrue(Name + TEXT(" has compiled vertex shader instructions"), Statistics.NumVertexShaderInstructions > 0);
			if (FMaterialShaderMap* Map = Resource->GetGameThreadShaderMap();
				TestNotNull(Name + TEXT(" has a shader map"), Map))
			{
				// In editor JobCacheDDC mode, UE keeps the historical whole-map
				// finalized/success flags false even after all required shaders
				// compile. Check resident shaders and renderable frozen content,
				// alongside errors and completeness, rather than those DDC flags.
				TestTrue(Name + TEXT(" has resident compiled shaders"), Map->GetShaderNum() > 0);
				TestTrue(Name + TEXT(" has renderable shader content"), Map->IsValidForRendering());
			}
		}
	}

	for (const TCHAR* Name : {TEXT("T_EclipseChitin"), TEXT("T_EclipseFlesh"), TEXT("T_EclipseCapillaries")})
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
