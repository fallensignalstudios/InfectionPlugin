#include "EclipseMeshBridge.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "BoneWeights.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Engine/StaticMesh.h"
#include "LODUtilities.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "PhysicsEngine/BodySetup.h"
#include "SkeletalMeshAttributes.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

#include <algorithm>
#include <vector>

#define LOCTEXT_NAMESPACE "EclipseMeshBridge"

namespace EclipseBridge
{
	// Unreal (X forward, Y right, Z up) to Eclipse (X, Y up, Z): a pure rotation.
	eclipse::Vec3 DirectionToEclipse(const FVector& D)
	{
		return eclipse::Vec3(static_cast<float>(D.X), static_cast<float>(D.Z), static_cast<float>(-D.Y));
	}

	FVector DirectionToUnreal(const eclipse::Vec3& D)
	{
		return FVector(D.x, -D.z, D.y);
	}

	eclipse::Vec3 ToEclipse(const FVector& Centimetres)
	{
		return DirectionToEclipse(Centimetres * 0.01);
	}

	FVector ToUnreal(const eclipse::Vec3& Metres)
	{
		return DirectionToUnreal(Metres) * 100.0;
	}

	// Positions in world space relative to the transform's location, and
	// triangles facing outward. Vertices come out in the description's
	// element order, which ReadSkeletalHost relies on to line up weights.
	static void ReadDescription(const FMeshDescription& Description, const FTransform& Transform, FHost& Out)
	{
		FStaticMeshConstAttributes Attributes(Description);
		TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();
		TVertexInstanceAttributesConstRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();

		Out = FHost();
		Out.Transform = Transform;
		Out.Origin = Transform.GetLocation();

		TArray<int32> Remap;
		Remap.Init(INDEX_NONE, Description.Vertices().GetArraySize());
		for (const FVertexID Vertex : Description.Vertices().GetElementIDs())
		{
			Remap[Vertex.GetValue()] = static_cast<int32>(Out.Mesh.positions.size());
			const FVector World = Transform.TransformPosition(FVector(Positions[Vertex]));
			Out.Mesh.positions.push_back(ToEclipse(World - Out.Origin));
		}

		// Decide, in the mesh's own local space, whether Unreal's triangle
		// order faces the opposite way to Eclipse's: count triangles whose
		// winding agrees with the mesh's stated normals. Local space keeps a
		// mirroring (negative-scale) transform out of the vote.
		int64 Agree = 0, Disagree = 0;
		for (const FTriangleID Triangle : Description.Triangles().GetElementIDs())
		{
			TArrayView<const FVertexInstanceID> Corners = Description.GetTriangleVertexInstances(Triangle);
			std::array<uint32_t, 3> Indices{};
			FVector3f Local[3];
			FVector3f NormalSum = FVector3f::ZeroVector;
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				const FVertexID Vertex = Description.GetVertexInstanceVertex(Corners[Corner]);
				Indices[Corner] = static_cast<uint32_t>(Remap[Vertex.GetValue()]);
				Local[Corner] = Positions[Vertex];
				NormalSum += Normals[Corners[Corner]];
			}
			const eclipse::Vec3 A = DirectionToEclipse(FVector(Local[0]));
			const eclipse::Vec3 Wound = eclipse::cross(DirectionToEclipse(FVector(Local[1])) - A, DirectionToEclipse(FVector(Local[2])) - A);
			(eclipse::dot(Wound, DirectionToEclipse(FVector(NormalSum))) >= 0.0f ? Agree : Disagree) += 1;
			Out.Mesh.triangles.push_back(Indices);
		}
		Out.bFlippedWinding = Disagree > Agree;

		// The world-space triangles must face outward for the generator. A
		// mirroring transform reverses them once more on top of the convention.
		const bool bMirrored = Transform.GetDeterminant() < 0.0;
		if (Out.bFlippedWinding != bMirrored)
		{
			for (auto& Triangle : Out.Mesh.triangles)
			{
				std::swap(Triangle[1], Triangle[2]);
			}
		}
	}

	bool ReadHost(UStaticMeshComponent* Component, FHost& Out, FText& OutError)
	{
		UStaticMesh* StaticMesh = Component ? Component->GetStaticMesh() : nullptr;
		if (!StaticMesh)
		{
			OutError = LOCTEXT("NoMesh", "The selected actor has no static mesh.");
			return false;
		}
		const FMeshDescription* Description = StaticMesh->GetMeshDescription(0);
		if (!Description || Description->Triangles().Num() == 0)
		{
			OutError = FText::Format(LOCTEXT("NoSource", "{0} has no editable source geometry for LOD 0."),
				FText::FromString(StaticMesh->GetName()));
			return false;
		}
		ReadDescription(*Description, Component->GetComponentTransform(), Out);
		return true;
	}

	bool ReadSkeletalHost(USkeletalMeshComponent* Component, FHost& Out, eclipse::SkinTable& OutSkin, FText& OutError)
	{
		USkeletalMesh* SkeletalMesh = Component ? Component->GetSkeletalMeshAsset() : nullptr;
		if (!SkeletalMesh)
		{
			OutError = LOCTEXT("NoSkeletalMesh", "The selected actor has no skeletal mesh.");
			return false;
		}
		const FMeshDescription* Description = SkeletalMesh->GetMeshDescription(0);
		if (!Description || Description->Triangles().Num() == 0)
		{
			OutError = FText::Format(LOCTEXT("NoSkeletalSource", "{0} has no editable source geometry for LOD 0."),
				FText::FromString(SkeletalMesh->GetName()));
			return false;
		}
		// The reference (bind) pose: the generated mesh is skinned against it.
		ReadDescription(*Description, Component->GetComponentTransform(), Out);

		FSkeletalMeshConstAttributes Attributes(*Description);
		FSkinWeightsVertexAttributesConstRef Weights = Attributes.GetVertexSkinWeights();
		OutSkin.clear();
		OutSkin.reserve(Out.Mesh.positions.size());
		for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
		{
			const auto Influences = Weights.Get(Vertex);
			std::vector<std::pair<float, uint16_t>> Sorted;
			for (int32 Index = 0; Index < Influences.Num(); ++Index)
			{
				Sorted.push_back({Influences[Index].GetWeight(), static_cast<uint16_t>(Influences[Index].GetBoneIndex())});
			}
			std::sort(Sorted.begin(), Sorted.end(), [](const auto& A, const auto& B) { return A.first > B.first; });
			eclipse::SkinWeights Skin;
			float Sum = 0.0f;
			for (const auto& [Weight, Bone] : Sorted)
			{
				if (Skin.count == eclipse::SkinWeights::kMaxInfluences || Weight <= 0.0f)
				{
					break;
				}
				Skin.bone[Skin.count] = Bone;
				Skin.weight[Skin.count] = Weight;
				Sum += Weight;
				++Skin.count;
			}
			for (int32 K = 0; K < Skin.count; ++K)
			{
				Skin.weight[K] /= Sum;
			}
			if (Skin.count == 0)
			{
				Skin = eclipse::SkinWeights::single(0);  // unweighted vertices follow the root
			}
			OutSkin.push_back(Skin);
		}
		return true;
	}

	void BuildMeshDescription(const eclipse::Mesh& Mesh, const FHost& Host, bool bMeshLocal, FName SlotName,
		FMeshDescription& Out, const eclipse::SkinTable* Skin)
	{
		// Skeletal attributes include the static ones; register exactly once.
		if (Skin)
		{
			FSkeletalMeshAttributes(Out).Register();
		}
		else
		{
			FStaticMeshAttributes(Out).Register();
		}
		FStaticMeshAttributes Attributes(Out);
		TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector4f> Colors = Attributes.GetVertexInstanceColors();
		TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
		UVs.SetNumChannels(2);  // 0: arrival and height; 1: soft tissue and tone

		const int32 VertexCount = static_cast<int32>(Mesh.positions.size());
		const int32 TriangleCount = static_cast<int32>(Mesh.triangles.size());
		Out.ReserveNewVertices(VertexCount);
		Out.ReserveNewVertexInstances(VertexCount);
		Out.ReserveNewTriangles(TriangleCount);

		const FPolygonGroupID Group = Out.CreatePolygonGroup();
		Attributes.GetPolygonGroupMaterialSlotNames()[Group] = SlotName;

		const bool bHasNormals = Mesh.normals.size() == Mesh.positions.size();
		const bool bHasAttributes = Mesh.attributes.size() == Mesh.positions.size();
		check(!Skin || Skin->size() == Mesh.positions.size());
		// Static meshes encode description colours as sRGB bytes; skeletal
		// meshes quantize them directly. Both materials must read the same
		// corruption data from the resulting raw vertex-colour bytes.
		auto Channel = [](float Value) { return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Value * 255.0f), 0, 255)); };

		// Generated geometry is world space relative to the host's location.
		// A skinned mesh needs it back in the mesh's own space, where its
		// skeleton's bind pose lives.
		auto ToSpace = [&](const eclipse::Vec3& P) -> FVector
		{
			const FVector World = ToUnreal(P);
			return bMeshLocal ? Host.Transform.InverseTransformPosition(World + Host.Origin) : World;
		};
		auto NormalToSpace = [&](const eclipse::Vec3& N) -> FVector
		{
			const FVector World = DirectionToUnreal(N);
			// Normals transform by the inverse transpose: undo the rotation, then apply the scale.
			return (bMeshLocal ? Host.Transform.InverseTransformVectorNoScale(World) * Host.Transform.GetScale3D() : World)
				.GetSafeNormal();
		};
		// Mapping back through a mirroring transform reverses winding once more.
		const bool bFlipWinding = Host.bFlippedWinding != (bMeshLocal && Host.Transform.GetDeterminant() < 0.0);
		TOptional<FSkinWeightsVertexAttributesRef> SkinWeights;
		if (Skin)
		{
			SkinWeights.Emplace(FSkeletalMeshAttributes(Out).GetVertexSkinWeights());
		}

		TArray<FVertexInstanceID> Instances;
		Instances.SetNumUninitialized(VertexCount);
		for (int32 Index = 0; Index < VertexCount; ++Index)
		{
			const FVertexID Vertex = Out.CreateVertex();
			Positions[Vertex] = FVector3f(ToSpace(Mesh.positions[Index]));
			const FVertexInstanceID Instance = Out.CreateVertexInstance(Vertex);
			if (bHasNormals)
			{
				Normals[Instance] = FVector3f(NormalToSpace(Mesh.normals[Index]));
			}
			if (Skin)
			{
				const eclipse::SkinWeights& W = (*Skin)[Index];
				TArray<UE::AnimationCore::FBoneWeight, TFixedAllocator<eclipse::SkinWeights::kMaxInfluences>> Influences;
				for (int32 K = 0; K < W.count; ++K)
				{
					Influences.Add(UE::AnimationCore::FBoneWeight(static_cast<FBoneIndexType>(W.bone[K]), W.weight[K]));
				}
				SkinWeights->Set(Vertex, Influences);
			}
			if (bHasAttributes)
			{
				const eclipse::CorruptionAttributes& A = Mesh.attributes[Index];
				const FColor Bytes(Channel(A.infection), Channel(A.glow), Channel(A.crack), Channel(A.plateTint));
				Colors[Instance] = FVector4f(Skin ? Bytes.ReinterpretAsLinear() : FLinearColor(Bytes));
				UVs.Set(Instance, 0, FVector2f(A.arrival, A.height));
				UVs.Set(Instance, 1, FVector2f(A.flesh, A.tone));
			}
			Instances[Index] = Instance;
		}
		for (const auto& Triangle : Mesh.triangles)
		{
			const int32 B = bFlipWinding ? 2 : 1;
			const int32 C = bFlipWinding ? 1 : 2;
			TArray<FVertexInstanceID, TFixedAllocator<3>> Corners;
			Corners.Add(Instances[Triangle[0]]);
			Corners.Add(Instances[Triangle[B]]);
			Corners.Add(Instances[Triangle[C]]);
			Out.CreateTriangle(Group, Corners);
		}
	}

	float LODTriangleShare(int32 Index, float Reduction)
	{
		return FMath::Pow(FMath::Clamp(Reduction, 0.01f, 1.0f), static_cast<float>(FMath::Max(Index, 0)));
	}

	UStaticMesh* WriteStaticMesh(const FString& PackagePath, const FString& AssetName, FMeshDescription&& Description,
		UMaterialInterface* Material, FName SlotName, const FOutputOptions& Options, FText& OutError)
	{
		const bool bNanite = Options.bNanite;
		const FString PackageName = PackagePath / AssetName;
		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			OutError = FText::Format(LOCTEXT("NoPackage", "Could not create package {0}."), FText::FromString(PackageName));
			return nullptr;
		}
		Package->FullyLoad();

		UStaticMesh* Mesh = FindObject<UStaticMesh>(Package, *AssetName);
		const bool bCreated = Mesh == nullptr;
		if (bCreated)
		{
			Mesh = NewObject<UStaticMesh>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		}
		else
		{
			Mesh->Modify();
		}

		// LOD 0 from the description; any further LODs reduced from it when
		// the mesh builds. Nanite simplifies on its own and needs none.
		const int32 LODCount = bNanite ? 1 : FMath::Clamp(Options.LODCount, 1, 8);
		Mesh->SetNumSourceModels(LODCount);
		for (int32 LOD = 0; LOD < LODCount; ++LOD)
		{
			FStaticMeshSourceModel& Source = Mesh->GetSourceModel(LOD);
			Source.BuildSettings.bRecomputeNormals = false;
			Source.BuildSettings.bRecomputeTangents = true;
			Source.BuildSettings.bUseMikkTSpace = true;
			Source.BuildSettings.bGenerateLightmapUVs = false;
			Source.BuildSettings.bRemoveDegenerates = false;
			Source.BuildSettings.bUseFullPrecisionUVs = true;  // UV 0 carries arrival times, not texture coordinates
			Source.ReductionSettings.PercentTriangles = LODTriangleShare(LOD, Options.LODReduction);
			Source.ReductionSettings.PercentVertices = Source.ReductionSettings.PercentTriangles;
		}
		Mesh->bAutoComputeLODScreenSize = true;

		TArray<FStaticMaterial> Materials;
		Materials.Add(FStaticMaterial(Material, SlotName, SlotName));
		Mesh->SetStaticMaterials(Materials);

		FMeshDescription* Target = Mesh->CreateMeshDescription(0);
		*Target = MoveTemp(Description);
		Mesh->CommitMeshDescription(0);

		// Through reflection: the C++ accessor for these settings has moved
		// between engine versions, the property has not.
		if (FStructProperty* NaniteProperty = FindFProperty<FStructProperty>(UStaticMesh::StaticClass(), TEXT("NaniteSettings")))
		{
			if (FBoolProperty* Enabled = FindFProperty<FBoolProperty>(NaniteProperty->Struct, TEXT("bEnabled")))
			{
				Enabled->SetPropertyValue_InContainer(NaniteProperty->ContainerPtrToValuePtr<void>(Mesh), bNanite);
			}
		}

		// Collision against the corruption's own triangles: its shapes are too
		// organic for boxes and hulls. Whether anything collides with it is
		// the placed component's collision setting.
		Mesh->CreateBodySetup();
		if (UBodySetup* Body = Mesh->GetBodySetup())
		{
			Body->Modify();
			Body->CollisionTraceFlag = Options.bCollision ? CTF_UseComplexAsSimple : CTF_UseDefault;
			Body->InvalidatePhysicsData();
		}

		// PostEditChange rebuilds the render data (Nanite included) once.
		Mesh->PostEditChange();
		if (bCreated)
		{
			FAssetRegistryModule::AssetCreated(Mesh);
		}
		Mesh->MarkPackageDirty();
		return Mesh;
	}

	USkeletalMesh* WriteSkeletalMesh(USkeletalMesh* Template, const FString& PackagePath, const FString& AssetName,
		FMeshDescription&& Description, UMaterialInterface* Material, FName SlotName, FText& OutError,
		const FOutputOptions& Options)
	{
		if (Template->GetMeshClothingAssets().Num() > 0)
		{
			OutError = FText::Format(LOCTEXT("Clothing",
				"{0} has clothing assets, which cannot follow regenerated geometry. Grow on a copy without clothing."),
				FText::FromString(Template->GetName()));
			return nullptr;
		}
		const FString PackageName = PackagePath / AssetName;
		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			OutError = FText::Format(LOCTEXT("NoSkeletalPackage", "Could not create package {0}."), FText::FromString(PackageName));
			return nullptr;
		}
		Package->FullyLoad();

		// Start from a copy of the host's mesh: the same skeleton, reference
		// pose and physics asset, so bone indices carry over unchanged.
		USkeletalMesh* Mesh = FindObject<USkeletalMesh>(Package, *AssetName);
		const bool bCreated = Mesh == nullptr;
		if (bCreated)
		{
			Mesh = DuplicateObject<USkeletalMesh>(Template, Package, *AssetName);
			Mesh->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
		}
		else
		{
			Mesh->Modify();
		}

		// Re-growing keeps this asset's identity, but the host can now use a
		// different skeleton or reference pose. Its new weights use the current
		// host's bone indices, so refresh the binding and physics metadata too.
		Mesh->SetRefSkeleton(Template->GetRefSkeleton());
		Mesh->SetSkeleton(Template->GetSkeleton());
		Mesh->GetRefBasesInvMatrix().Reset();
		// CalculateInvRefMatrices otherwise reuses the cache when bone counts
		// match, even if the host's reference transforms have changed.
		Mesh->CalculateInvRefMatrices();
		Mesh->SetPhysicsAsset(Template->GetPhysicsAsset());
		Mesh->SetShadowPhysicsAsset(Template->GetShadowPhysicsAsset());

		// One LOD, no morph targets: the copy's other LODs and shapes belong
		// to the host's geometry, not this.
		while (Mesh->GetLODNum() > 1)
		{
			FSkeletalMeshUpdateContext Context;
			Context.SkeletalMesh = Mesh;
			FLODUtilities::RemoveLOD(Context, Mesh->GetLODNum() - 1);
		}
		Mesh->UnregisterAllMorphTarget();
		// Nothing inherited from the host may reshape the generated geometry.
		Mesh->SetLODSettings(nullptr);
		Mesh->GetSkinWeightProfiles().Empty();
		// The corruption data lives in vertex colours; character meshes often
		// have them switched off.
		Mesh->SetHasVertexColors(true);
		Mesh->SetVertexColorGuid(FGuid::NewGuid());

		TArray<FSkeletalMaterial> Materials;
		Materials.Add(FSkeletalMaterial(Material, SlotName, SlotName));
		Mesh->SetMaterials(Materials);
		if (FSkeletalMeshLODInfo* LOD = Mesh->GetLODInfo(0))
		{
			LOD->LODMaterialMap.Empty();
			LOD->BuildSettings.bRecomputeNormals = false;
			LOD->BuildSettings.bRecomputeTangents = true;
			LOD->BuildSettings.bUseMikkTSpace = true;
			LOD->BuildSettings.bUseFullPrecisionUVs = true;  // UV 0 carries arrival times
			LOD->ReductionSettings = FSkeletalMeshOptimizationSettings();  // never decimate LOD 0
		}

		// The skeleton, in the description too, as imported skeletal meshes
		// carry it; bone indices are the asset's, as the weights expect.
		{
			FSkeletalMeshAttributes Attributes(Description);
			const FReferenceSkeleton& Reference = Mesh->GetRefSkeleton();
			const TArray<FTransform>& Pose = Reference.GetRefBonePose();
			for (int32 Bone = 0; Bone < Reference.GetRawBoneNum(); ++Bone)
			{
				const FBoneID Id = Attributes.CreateBone();
				Attributes.GetBoneNames().Set(Id, Reference.GetBoneName(Bone));
				Attributes.GetBoneParentIndices().Set(Id, Reference.GetParentIndex(Bone));
				Attributes.GetBonePoses().Set(Id, Pose[Bone]);
			}
		}

		FMeshDescription* Target = Mesh->CreateMeshDescription(0);
		*Target = MoveTemp(Description);
		Mesh->CommitMeshDescription(0);

		// Reduced LODs, generated from LOD 0 when the mesh builds (they have
		// no source geometry of their own). Followers take the host's LOD
		// level, clamped to the LODs they have.
		const int32 LODCount = FMath::Clamp(Options.LODCount, 1, 8);
		for (int32 LOD = 1; LOD < LODCount; ++LOD)
		{
			FSkeletalMeshLODInfo& Info = Mesh->AddLODInfo();
			Info.ReductionSettings = FSkeletalMeshOptimizationSettings();
			Info.ReductionSettings.NumOfTrianglesPercentage = LODTriangleShare(LOD, Options.LODReduction);
			Info.ReductionSettings.BaseLOD = 0;
			Info.ScreenSize = FPerPlatformFloat(FMath::Pow(0.5f, static_cast<float>(LOD)));
			Info.BuildSettings = Mesh->GetLODInfo(0)->BuildSettings;
			Info.LODMaterialMap.Empty();
		}
		Mesh->PostEditChange();
		if (bCreated)
		{
			FAssetRegistryModule::AssetCreated(Mesh);
		}
		Mesh->MarkPackageDirty();
		return Mesh;
	}

	bool SetBoolProperty(UObject* Object, FName Property, bool bValue)
	{
		if (FBoolProperty* Prop = FindFProperty<FBoolProperty>(Object->GetClass(), Property))
		{
			Prop->SetPropertyValue_InContainer(Object, bValue);
			return true;
		}
		return false;
	}

	bool SetEnumProperty(UObject* Object, FName Property, int64 Value)
	{
		FProperty* Prop = FindFProperty<FProperty>(Object->GetClass(), Property);
		if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(EnumProp->ContainerPtrToValuePtr<void>(Object), Value);
			return true;
		}
		if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))  // TEnumAsByte
		{
			ByteProp->SetPropertyValue_InContainer(Object, static_cast<uint8>(Value));
			return true;
		}
		return false;
	}
}

#undef LOCTEXT_NAMESPACE
