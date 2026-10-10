#include "EclipseMaterials.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EclipseMeshBridge.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/FeedbackContext.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "EclipseMaterials"

namespace
{
	const TCHAR* MasterName = TEXT("M_EclipseBiomass");

	// Shared by every node: how far the growth has played (reveal) and how
	// overtaken this point is once revealed (s). UV.x is arrival: 0 at the
	// seed, 1 at the front. Animate Growth from 0 to about 1.4 to replay it.
	const TCHAR* Prelude = TEXT(
		"float reveal = saturate((Growth - UV.x) / max(Softness, 0.001));\n"
		"float s = saturate(VC.r) * reveal;\n");

	const TCHAR* BaseColorCode = TEXT(
		"float inflamed = 4.0 * s * (1.0 - s);\n"
		"float3 host = lerp(HostColor, InflamedColor, OverlayMode);\n"
		"float3 c = lerp(host, InflamedColor, 0.55 * inflamed * (1.0 - s));\n"
		"return lerp(c, CarapaceColor + CarapaceTint * VA, s);\n");

	// The glow pulses in waves travelling outward from the seed.
	const TCHAR* EmissiveCode = TEXT(
		"float pulse = 1.0 + PulseAmount * sin(Time * PulseSpeed * 6.2831853 - UV.x * 8.0);\n"
		"return GlowColor * GlowIntensity * saturate(VC.g) * reveal * pulse;\n");

	const TCHAR* RoughnessCode = TEXT("return lerp(0.7, 0.22, s);\n");
	const TCHAR* ClearCoatCode = TEXT("return s;\n");

	// Overlay shells show only where corrupted or on a vein's bright core;
	// growths show once revealed; a replacing surface is always solid.
	const TCHAR* OpacityCode = TEXT(
		"float visible = saturate(max(VC.r * 2.0, (VC.g - 0.3) * 3.0));\n"
		"float surfaceOpacity = lerp(1.0, reveal * visible, OverlayMode);\n"
		"return lerp(surfaceOpacity, reveal, IsGrowth);\n");

	FString ObjectPath(const FString& PackageName, const FString& Name)
	{
		return PackageName + TEXT(".") + Name;
	}

	template <class T>
	T* LoadQuietly(const FString& PackageName, const FString& Name)
	{
		return LoadObject<T>(nullptr, *ObjectPath(PackageName, Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	UMaterialExpressionScalarParameter* Scalar(UMaterial* M, const TCHAR* Name, float Value, int32 Y)
	{
		auto* P = Cast<UMaterialExpressionScalarParameter>(
			UMaterialEditingLibrary::CreateMaterialExpression(M, UMaterialExpressionScalarParameter::StaticClass(), -900, Y));
		P->ParameterName = Name;
		P->DefaultValue = Value;
		P->Group = TEXT("Eclipse");
		return P;
	}

	UMaterialExpressionVectorParameter* Colour(UMaterial* M, const TCHAR* Name, const FLinearColor& Value, int32 Y)
	{
		auto* P = Cast<UMaterialExpressionVectorParameter>(
			UMaterialEditingLibrary::CreateMaterialExpression(M, UMaterialExpressionVectorParameter::StaticClass(), -1200, Y));
		P->ParameterName = Name;
		P->DefaultValue = Value;
		P->Group = TEXT("Eclipse Colours");
		return P;
	}

	struct FInputs
	{
		TArray<TPair<FName, TPair<UMaterialExpression*, FString>>> List;  // input name -> (expression, its output)
		void Add(const TCHAR* Name, UMaterialExpression* From, const TCHAR* Output = TEXT(""))
		{
			List.Add({FName(Name), {From, FString(Output)}});
		}
	};

	UMaterialExpressionCustom* Custom(UMaterial* M, const TCHAR* Description, const FString& Body,
		ECustomMaterialOutputType Type, const FInputs& Inputs, int32 Y)
	{
		auto* C = Cast<UMaterialExpressionCustom>(
			UMaterialEditingLibrary::CreateMaterialExpression(M, UMaterialExpressionCustom::StaticClass(), -350, Y));
		C->Description = Description;
		C->Code = FString(Prelude) + Body;
		C->OutputType = Type;
		C->Inputs.Reset();
		for (const auto& Input : Inputs.List)
		{
			FCustomInput Slot;
			Slot.InputName = Input.Key;
			C->Inputs.Add(Slot);
		}
		for (const auto& Input : Inputs.List)
		{
			UMaterialEditingLibrary::ConnectMaterialExpressions(Input.Value.Key, Input.Value.Value, C, Input.Key.ToString());
		}
		return C;
	}

	// Builds the graph into Existing (cleared first) or into a new asset.
	UMaterial* BuildMaster(const FString& Folder, UMaterial* Existing, FText& OutError)
	{
		UMaterial* M = Existing;
		if (M)
		{
			M->Modify();
			UMaterialEditingLibrary::DeleteAllMaterialExpressions(M);
		}
		else
		{
			UPackage* Package = CreatePackage(*(Folder / MasterName));
			auto* Factory = NewObject<UMaterialFactoryNew>();
			M = Cast<UMaterial>(Factory->FactoryCreateNew(UMaterial::StaticClass(), Package, MasterName,
				RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
			if (!M)
			{
				OutError = LOCTEXT("MaterialFailed", "Could not create the Eclipse material.");
				return nullptr;
			}
			FAssetRegistryModule::AssetCreated(M);
		}

		// Masked so overlay shells and growths can be revealed; two-sided for
		// membranes and open environment shells; clear coat for the wet sheen.
		EclipseBridge::SetEnumProperty(M, TEXT("BlendMode"), BLEND_Masked);
		EclipseBridge::SetBoolProperty(M, TEXT("TwoSided"), true);
		M->SetShadingModel(MSM_ClearCoat);

		auto* Vertex = UMaterialEditingLibrary::CreateMaterialExpression(M, UMaterialExpressionVertexColor::StaticClass(), -900, -700);
		auto* Coords = Cast<UMaterialExpressionTextureCoordinate>(
			UMaterialEditingLibrary::CreateMaterialExpression(M, UMaterialExpressionTextureCoordinate::StaticClass(), -900, -560));
		Coords->CoordinateIndex = 0;
		auto* Time = UMaterialEditingLibrary::CreateMaterialExpression(M, UMaterialExpressionTime::StaticClass(), -900, -460);

		auto* Growth = Scalar(M, TEXT("Growth"), 4.0f, -380);
		auto* Softness = Scalar(M, TEXT("Softness"), 0.08f, -300);
		auto* GlowIntensity = Scalar(M, TEXT("GlowIntensity"), 12.0f, -220);
		auto* PulseSpeed = Scalar(M, TEXT("PulseSpeed"), 0.6f, -140);
		auto* PulseAmount = Scalar(M, TEXT("PulseAmount"), 0.35f, -60);
		auto* OverlayMode = Scalar(M, TEXT("OverlayMode"), 1.0f, 20);
		auto* IsGrowth = Scalar(M, TEXT("IsGrowth"), 0.0f, 100);
		auto* CoatRoughness = Scalar(M, TEXT("ClearCoatRoughness"), 0.06f, 180);

		auto* HostColor = Colour(M, TEXT("HostColor"), FLinearColor(0.50f, 0.43f, 0.41f), -700);
		auto* InflamedColor = Colour(M, TEXT("InflamedColor"), FLinearColor(0.32f, 0.10f, 0.24f), -560);
		auto* CarapaceColor = Colour(M, TEXT("CarapaceColor"), FLinearColor(0.018f, 0.015f, 0.022f), -420);
		auto* CarapaceTint = Colour(M, TEXT("CarapaceTint"), FLinearColor(0.02f, 0.008f, 0.03f), -280);
		auto* GlowColor = Colour(M, TEXT("GlowColor"), FLinearColor(0.55f, 0.14f, 1.0f), -140);

		FInputs Common;
		Common.Add(TEXT("VC"), Vertex);
		Common.Add(TEXT("VA"), Vertex, TEXT("A"));
		Common.Add(TEXT("UV"), Coords);
		Common.Add(TEXT("Growth"), Growth);
		Common.Add(TEXT("Softness"), Softness);

		FInputs BaseInputs = Common;
		BaseInputs.Add(TEXT("HostColor"), HostColor);
		BaseInputs.Add(TEXT("InflamedColor"), InflamedColor);
		BaseInputs.Add(TEXT("CarapaceColor"), CarapaceColor);
		BaseInputs.Add(TEXT("CarapaceTint"), CarapaceTint);
		BaseInputs.Add(TEXT("OverlayMode"), OverlayMode);

		FInputs EmissiveInputs = Common;
		EmissiveInputs.Add(TEXT("Time"), Time);
		EmissiveInputs.Add(TEXT("GlowColor"), GlowColor);
		EmissiveInputs.Add(TEXT("GlowIntensity"), GlowIntensity);
		EmissiveInputs.Add(TEXT("PulseSpeed"), PulseSpeed);
		EmissiveInputs.Add(TEXT("PulseAmount"), PulseAmount);

		FInputs OpacityInputs = Common;
		OpacityInputs.Add(TEXT("OverlayMode"), OverlayMode);
		OpacityInputs.Add(TEXT("IsGrowth"), IsGrowth);

		auto* BaseNode = Custom(M, TEXT("Eclipse Base Colour"), BaseColorCode, CMOT_Float3, BaseInputs, -600);
		auto* EmissiveNode = Custom(M, TEXT("Eclipse Glow"), EmissiveCode, CMOT_Float3, EmissiveInputs, -350);
		auto* RoughNode = Custom(M, TEXT("Eclipse Roughness"), RoughnessCode, CMOT_Float1, Common, -150);
		auto* CoatNode = Custom(M, TEXT("Eclipse Clear Coat"), ClearCoatCode, CMOT_Float1, Common, 0);
		auto* OpacityNode = Custom(M, TEXT("Eclipse Opacity"), OpacityCode, CMOT_Float1, OpacityInputs, 150);

		UMaterialEditingLibrary::ConnectMaterialProperty(BaseNode, TEXT(""), MP_BaseColor);
		UMaterialEditingLibrary::ConnectMaterialProperty(EmissiveNode, TEXT(""), MP_EmissiveColor);
		UMaterialEditingLibrary::ConnectMaterialProperty(RoughNode, TEXT(""), MP_Roughness);
		UMaterialEditingLibrary::ConnectMaterialProperty(CoatNode, TEXT(""), MP_CustomData0);       // clear coat
		UMaterialEditingLibrary::ConnectMaterialProperty(CoatRoughness, TEXT(""), MP_CustomData1);  // clear coat roughness
		UMaterialEditingLibrary::ConnectMaterialProperty(OpacityNode, TEXT(""), MP_OpacityMask);

		UMaterialEditingLibrary::RecompileMaterial(M);
		M->MarkPackageDirty();
		return M;
	}

	// Sets the instance's switches on Existing, or on a new asset.
	UMaterialInterface* BuildInstance(const FString& Folder, const TCHAR* Name, UMaterial* Parent,
		UMaterialInstanceConstant* Existing, const TArray<TPair<FName, float>>& Scalars)
	{
		UMaterialInstanceConstant* Instance = Existing;
		if (Instance)
		{
			Instance->Modify();
			Instance->SetParentEditorOnly(Parent);
		}
		else
		{
			UPackage* Package = CreatePackage(*(Folder / Name));
			auto* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
			Factory->InitialParent = Parent;
			Instance = Cast<UMaterialInstanceConstant>(Factory->FactoryCreateNew(UMaterialInstanceConstant::StaticClass(),
				Package, Name, RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
			if (!Instance)
			{
				return nullptr;
			}
			FAssetRegistryModule::AssetCreated(Instance);
		}
		for (const auto& Pair : Scalars)
		{
			Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(Pair.Key), Pair.Value);
		}
		Instance->PostEditChange();
		Instance->MarkPackageDirty();
		return Instance;
	}
}

bool FEclipseMaterials::Ensure(const FString& OutputFolder, bool bRebuild, FEclipseMaterials& Out, FText& OutError)
{
	const FString Folder = OutputFolder / TEXT("Materials");
	struct FSpec
	{
		const TCHAR* Name;
		UMaterialInterface* FEclipseMaterials::*Slot;
		TArray<TPair<FName, float>> Scalars;
	};
	const FSpec Specs[] = {
		{TEXT("MI_Eclipse_Overlay"), &FEclipseMaterials::Overlay,
			{TPair<FName, float>(TEXT("OverlayMode"), 1.0f), TPair<FName, float>(TEXT("IsGrowth"), 0.0f)}},
		{TEXT("MI_Eclipse_Replace"), &FEclipseMaterials::Replace,
			{TPair<FName, float>(TEXT("OverlayMode"), 0.0f), TPair<FName, float>(TEXT("IsGrowth"), 0.0f)}},
		{TEXT("MI_Eclipse_Growths"), &FEclipseMaterials::Growths,
			{TPair<FName, float>(TEXT("OverlayMode"), 1.0f), TPair<FName, float>(TEXT("IsGrowth"), 1.0f)}},
	};

	UMaterial* Master = LoadQuietly<UMaterial>(Folder / MasterName, MasterName);
	if (!Master || bRebuild)
	{
		Master = BuildMaster(Folder, Master, OutError);
		if (!Master)
		{
			return false;
		}
	}
	for (const FSpec& Spec : Specs)
	{
		UMaterialInstanceConstant* Existing = LoadQuietly<UMaterialInstanceConstant>(Folder / Spec.Name, Spec.Name);
		UMaterialInterface* Instance = Existing;
		if (!Existing || bRebuild)
		{
			Instance = BuildInstance(Folder, Spec.Name, Master, Existing, Spec.Scalars);
		}
		if (!Instance)
		{
			OutError = FText::Format(LOCTEXT("InstanceFailed", "Could not create {0}."), FText::FromString(Spec.Name));
			return false;
		}
		Out.*Spec.Slot = Instance;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
