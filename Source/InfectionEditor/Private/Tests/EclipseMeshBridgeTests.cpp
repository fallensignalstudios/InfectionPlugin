#include "EclipseMeshBridge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Engine/StaticMesh.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "MeshDescription.h"
#include "Misc/AutomationTest.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "ReferenceSkeleton.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "SkeletalMeshAttributes.h"
#include "StaticToSkeletalMeshConverter.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	const FName TestSlot(TEXT("EclipseTestSurface"));
	const FColor TestData(128, 64, 32, 192);
	constexpr float TestFlesh = 0.75f;

	FMeshDescription TestDescription(const FReferenceSkeleton* Reference = nullptr)
	{
		eclipse::Mesh Geometry;
		Geometry.positions = {{0.0f, 0.0f, 0.0f}, {0.1f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.1f}};
		Geometry.triangles = {{0, 2, 1}};
		Geometry.normals.resize(3, eclipse::Vec3(0.0f, 1.0f, 0.0f));
		Geometry.attributes.resize(3);
		for (auto& Data : Geometry.attributes)
		{
			Data.infection = TestData.R / 255.0f;
			Data.glow = TestData.G / 255.0f;
			Data.crack = TestData.B / 255.0f;
			Data.plateTint = TestData.A / 255.0f;
			Data.flesh = TestFlesh;
			Data.arrival = 0.0f;
		}
		Geometry.attributes[1].arrival = 1.0f;
		Geometry.attributes[2].height = 1.0f;
		const eclipse::SkinTable Skin(3, eclipse::SkinWeights::single(1));
		EclipseBridge::FHost Host;
		Host.Transform = FTransform::Identity;
		FMeshDescription Description;
		EclipseBridge::BuildMeshDescription(Geometry, Host, true, TestSlot, Description, &Skin);
		if (Reference)
		{
			FSkeletalMeshAttributes Attributes(Description);
			for (int32 Bone = 0; Bone < Reference->GetRawBoneNum(); ++Bone)
			{
				const FBoneID Id = Attributes.CreateBone();
				Attributes.GetBoneNames().Set(Id, Reference->GetBoneName(Bone));
				Attributes.GetBoneParentIndices().Set(Id, Reference->GetParentIndex(Bone));
				Attributes.GetBonePoses().Set(Id, Reference->GetRawRefBonePose()[Bone]);
			}
		}
		return Description;
	}

	bool InitializeTemplate(USkeletalMesh* Mesh, const TCHAR* ChildName, const FVector& ChildTranslation)
	{
		USkeleton* Skeleton = NewObject<USkeleton>(GetTransientPackage(), NAME_None, RF_Transient);
		{
			FReferenceSkeletonModifier Modifier(Skeleton);
			Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
			Modifier.Add(FMeshBoneInfo(ChildName, ChildName, 0), FTransform(FQuat::Identity, ChildTranslation));
		}
		Mesh->SetSkeleton(Skeleton);
		Mesh->SetPhysicsAsset(NewObject<UPhysicsAsset>(GetTransientPackage(), NAME_None, RF_Transient));
		Mesh->SetShadowPhysicsAsset(NewObject<UPhysicsAsset>(GetTransientPackage(), NAME_None, RF_Transient));
		FMeshDescription Description = TestDescription(&Skeleton->GetReferenceSkeleton());
		TArray<const FMeshDescription*> Descriptions = {&Description};
		TArray<FSkeletalMaterial> Materials = {FSkeletalMaterial(UMaterial::GetDefaultMaterial(MD_Surface), TestSlot, TestSlot)};
		FStaticToSkeletalMeshConverter::FInitializationParams Params;
		Params.Materials = Materials;
		return FStaticToSkeletalMeshConverter::InitializeSkeletalMeshFromMeshDescriptions(
			Mesh, Descriptions, Skeleton->GetReferenceSkeleton(), Params);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEclipseSkeletalColorDataTest,
	"Infection.Editor.SkeletalMesh.PreservesCorruptionChannels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEclipseSkeletalColorDataTest::RunTest(const FString& Parameters)
{
	const FSkeletalMeshImportData ImportData = FSkeletalMeshImportData::CreateFromMeshDescription(TestDescription());
	if (!TestEqual(TEXT("The skeletal importer receives three vertices"), ImportData.Wedges.Num(), 3))
	{
		return false;
	}
	for (const SkeletalMeshImportData::FVertex& Wedge : ImportData.Wedges)
	{
		TestEqual(TEXT("Infection reaches the skeletal importer unchanged"), Wedge.Color.R, TestData.R);
		TestEqual(TEXT("Glow reaches the skeletal importer unchanged"), Wedge.Color.G, TestData.G);
		TestEqual(TEXT("Cracks reach the skeletal importer unchanged"), Wedge.Color.B, TestData.B);
		TestEqual(TEXT("Plate tint reaches the skeletal importer unchanged"), Wedge.Color.A, TestData.A);
		TestEqual(TEXT("Soft tissue reaches the skeletal importer in UV 1"), Wedge.UVs[1].X, TestFlesh);
	}
	TestTrue(TEXT("The importer receives both UV channels"), ImportData.NumTexCoords >= 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEclipseSkeletalRegrowTemplateTest,
	"Infection.Editor.SkeletalMesh.RegrowRefreshesHostSkeleton",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEclipseSkeletalRegrowTemplateTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<USkeletalMesh> TemplateA(NewObject<USkeletalMesh>(GetTransientPackage(), NAME_None, RF_Transient));
	TStrongObjectPtr<USkeletalMesh> TemplateB(NewObject<USkeletalMesh>(GetTransientPackage(), NAME_None, RF_Transient));
	if (!TestTrue(TEXT("The first skeletal fixture builds"), InitializeTemplate(TemplateA.Get(), TEXT("joint_a"), FVector(25.0, 0.0, 0.0))) ||
		!TestTrue(TEXT("The replacement skeletal fixture builds"), InitializeTemplate(TemplateB.Get(), TEXT("joint_b"), FVector(0.0, 50.0, 0.0))))
	{
		return false;
	}

	// Each run gets an in-memory package; no test assets are saved to disk.
	const FString Folder = TEXT("/Temp/InfectionPluginTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString AssetName = TEXT("SK_Eclipse_Regrow");
	FText Error;
	auto Write = [&](USkeletalMesh* Template)
	{
		return EclipseBridge::WriteSkeletalMesh(Template, Folder, AssetName, TestDescription(),
			UMaterial::GetDefaultMaterial(MD_Surface), TestSlot, Error);
	};
	USkeletalMesh* First = Write(TemplateA.Get());
	if (!TestNotNull(TEXT("The first grow creates a skeletal output"), First))
	{
		AddError(Error.ToString());
		return false;
	}
	const FMatrix44f FirstInverse = First->GetRefBasesInvMatrix()[1];
	USkeletalMesh* Regrown = Write(TemplateB.Get());
	if (!TestNotNull(TEXT("Growing again on another host mesh succeeds"), Regrown))
	{
		AddError(Error.ToString());
		return false;
	}
	TestTrue(TEXT("Re-growing preserves the generated asset identity"), Regrown == First);
	TestTrue(TEXT("The output uses the current host skeleton"), Regrown->GetSkeleton() == TemplateB->GetSkeleton());
	TestEqual(TEXT("The output uses the current host's bone names"), Regrown->GetRefSkeleton().GetBoneName(1), FName(TEXT("joint_b")));
	TestTrue(TEXT("The output uses the current host's reference pose"),
		Regrown->GetRefSkeleton().GetRawRefBonePose()[1].Equals(TemplateB->GetRefSkeleton().GetRawRefBonePose()[1]));
	TestTrue(TEXT("The output uses the current host physics asset"), Regrown->GetPhysicsAsset() == TemplateB->GetPhysicsAsset());
	TestTrue(TEXT("The output uses the current host shadow physics asset"), Regrown->GetShadowPhysicsAsset() == TemplateB->GetShadowPhysicsAsset());
	TestFalse(TEXT("Changing the host changes the inverse bind matrix"), Regrown->GetRefBasesInvMatrix()[1].Equals(FirstInverse));
	TestTrue(TEXT("The output inverse bind matrix matches the current host"),
		Regrown->GetRefBasesInvMatrix()[1].Equals(TemplateB->GetRefBasesInvMatrix()[1]));
	FSkeletalMeshConstAttributes OutputAttributes(*Regrown->GetMeshDescription(0));
	TestEqual(TEXT("The saved geometry contains the current host's bone name"), OutputAttributes.GetBoneNames().Get(FBoneID(1)), FName(TEXT("joint_b")));

	// A pose can change without changing the skeleton or its bone count.
	{
		FReferenceSkeletonModifier Modifier(TemplateB->GetRefSkeleton(), TemplateB->GetSkeleton());
		Modifier.UpdateRefPoseTransform(1, FTransform(FQuat::Identity, FVector(0.0, 75.0, 0.0)));
	}
	TemplateB->GetRefBasesInvMatrix().Reset();
	TemplateB->CalculateInvRefMatrices();
	const FMatrix44f PreviousInverse = Regrown->GetRefBasesInvMatrix()[1];
	USkeletalMesh* Reposed = Write(TemplateB.Get());
	if (!TestNotNull(TEXT("Re-growing after a reference pose edit succeeds"), Reposed))
	{
		AddError(Error.ToString());
		return false;
	}
	TestTrue(TEXT("Reference pose edits preserve the generated asset identity"), Reposed == First);
	TestFalse(TEXT("The inverse bind matrix is rebuilt even with the same bone count"), Reposed->GetRefBasesInvMatrix()[1].Equals(PreviousInverse));
	TestTrue(TEXT("The edited host's inverse bind matrix reaches the output"),
		Reposed->GetRefBasesInvMatrix()[1].Equals(TemplateB->GetRefBasesInvMatrix()[1]));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEclipseStaticLODCollisionTest,
	"Infection.Editor.StaticMesh.LODsAndCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEclipseStaticLODCollisionTest::RunTest(const FString& Parameters)
{
	const FString Folder = TEXT("/Temp/InfectionPluginTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	auto Description = []
	{
		eclipse::Mesh Geometry;
		Geometry.positions = {{0.0f, 0.0f, 0.0f}, {0.1f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.1f}, {0.1f, 0.0f, 0.1f}};
		Geometry.triangles = {{0, 2, 1}, {1, 2, 3}};
		Geometry.normals.resize(4, eclipse::Vec3(0.0f, 1.0f, 0.0f));
		Geometry.attributes.resize(4);
		EclipseBridge::FHost Host;
		Host.Transform = FTransform::Identity;
		FMeshDescription Out;
		EclipseBridge::BuildMeshDescription(Geometry, Host, false, TestSlot, Out);
		return Out;
	};
	FText Error;
	EclipseBridge::FOutputOptions Options;
	Options.bNanite = false;
	Options.LODCount = 4;
	Options.LODReduction = 0.5f;
	Options.bCollision = true;
	UStaticMesh* Mesh = EclipseBridge::WriteStaticMesh(Folder, TEXT("SM_Eclipse_LODs"), Description(),
		UMaterial::GetDefaultMaterial(MD_Surface), TestSlot, Options, Error);
	if (!TestNotNull(TEXT("The static output is written"), Mesh))
	{
		AddError(Error.ToString());
		return false;
	}
	TestEqual(TEXT("Without Nanite it carries the LODs asked for"), Mesh->GetNumSourceModels(), 4);
	TestEqual(TEXT("LOD 0 is not reduced"), Mesh->GetSourceModel(0).ReductionSettings.PercentTriangles, 1.0f);
	TestEqual(TEXT("Each LOD keeps half of the one before"), Mesh->GetSourceModel(3).ReductionSettings.PercentTriangles, 0.125f);
	TestTrue(TEXT("Collision uses the corruption's own triangles"),
		Mesh->GetBodySetup() && Mesh->GetBodySetup()->CollisionTraceFlag == CTF_UseComplexAsSimple);

	Options.bNanite = true;
	Options.bCollision = false;
	Mesh = EclipseBridge::WriteStaticMesh(Folder, TEXT("SM_Eclipse_LODs"), Description(),
		UMaterial::GetDefaultMaterial(MD_Surface), TestSlot, Options, Error);
	if (!TestNotNull(TEXT("The static output is rewritten"), Mesh))
	{
		AddError(Error.ToString());
		return false;
	}
	TestEqual(TEXT("A Nanite rewrite drops back to one LOD"), Mesh->GetNumSourceModels(), 1);
	TestTrue(TEXT("Without collision the trace flag is the default"),
		Mesh->GetBodySetup() && Mesh->GetBodySetup()->CollisionTraceFlag == CTF_UseDefault);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEclipseSkeletalLODTest,
	"Infection.Editor.SkeletalMesh.GeneratesReducedLODs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEclipseSkeletalLODTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<USkeletalMesh> Template(NewObject<USkeletalMesh>(GetTransientPackage(), NAME_None, RF_Transient));
	if (!TestTrue(TEXT("The skeletal fixture builds"), InitializeTemplate(Template.Get(), TEXT("joint"), FVector(25.0, 0.0, 0.0))))
	{
		return false;
	}
	const FString Folder = TEXT("/Temp/InfectionPluginTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	FText Error;
	EclipseBridge::FOutputOptions Options;
	Options.LODCount = 3;
	USkeletalMesh* Mesh = EclipseBridge::WriteSkeletalMesh(Template.Get(), Folder, TEXT("SK_Eclipse_LODs"), TestDescription(),
		UMaterial::GetDefaultMaterial(MD_Surface), TestSlot, Error, Options);
	if (!TestNotNull(TEXT("The skeletal output is written"), Mesh))
	{
		AddError(Error.ToString());
		return false;
	}
	TestEqual(TEXT("It carries the LODs asked for"), Mesh->GetLODNum(), 3);
	TestEqual(TEXT("LOD 2 keeps a quarter of LOD 0"), Mesh->GetLODInfo(2)->ReductionSettings.NumOfTrianglesPercentage, 0.25f);
	TestEqual(TEXT("reduced from LOD 0"), Mesh->GetLODInfo(2)->ReductionSettings.BaseLOD, 0);

	Options.LODCount = 1;
	Mesh = EclipseBridge::WriteSkeletalMesh(Template.Get(), Folder, TEXT("SK_Eclipse_LODs"), TestDescription(),
		UMaterial::GetDefaultMaterial(MD_Surface), TestSlot, Error, Options);
	TestTrue(TEXT("Growing again with fewer LODs removes the rest"), Mesh && Mesh->GetLODNum() == 1);
	return true;
}

#endif
