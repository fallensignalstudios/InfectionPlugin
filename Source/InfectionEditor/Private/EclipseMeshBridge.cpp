#include "EclipseMeshBridge.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"

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

		FStaticMeshConstAttributes Attributes(*Description);
		TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();
		TVertexInstanceAttributesConstRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();

		const FTransform Transform = Component->GetComponentTransform();
		Out = FHost();
		Out.Origin = Transform.GetLocation();

		TArray<int32> Remap;
		Remap.Init(INDEX_NONE, Description->Vertices().GetArraySize());
		for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
		{
			Remap[Vertex.GetValue()] = static_cast<int32>(Out.Mesh.positions.size());
			const FVector World = Transform.TransformPosition(FVector(Positions[Vertex]));
			Out.Mesh.positions.push_back(ToEclipse(World - Out.Origin));
		}

		// Count how many triangles, as wound, face the way the mesh's own
		// normals say they should; flip them all if most disagree.
		int64 Agree = 0, Disagree = 0;
		for (const FTriangleID Triangle : Description->Triangles().GetElementIDs())
		{
			TArrayView<const FVertexInstanceID> Corners = Description->GetTriangleVertexInstances(Triangle);
			std::array<uint32_t, 3> Indices{};
			FVector NormalSum = FVector::ZeroVector;
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				Indices[Corner] = static_cast<uint32_t>(Remap[Description->GetVertexInstanceVertex(Corners[Corner]).GetValue()]);
				NormalSum += FVector(Normals[Corners[Corner]]);
			}
			const eclipse::Vec3& A = Out.Mesh.positions[Indices[0]];
			const eclipse::Vec3& B = Out.Mesh.positions[Indices[1]];
			const eclipse::Vec3& C = Out.Mesh.positions[Indices[2]];
			const eclipse::Vec3 Wound = eclipse::cross(B - A, C - A);
			const eclipse::Vec3 Stated = DirectionToEclipse(Transform.TransformVectorNoScale(NormalSum));
			(eclipse::dot(Wound, Stated) >= 0.0f ? Agree : Disagree) += 1;
			Out.Mesh.triangles.push_back(Indices);
		}
		Out.bFlippedWinding = Disagree > Agree;
		if (Out.bFlippedWinding)
		{
			for (auto& Triangle : Out.Mesh.triangles)
			{
				std::swap(Triangle[1], Triangle[2]);
			}
		}
		return true;
	}

	void BuildMeshDescription(const eclipse::Mesh& Mesh, bool bFlipWinding, FName SlotName, FMeshDescription& Out)
	{
		FStaticMeshAttributes Attributes(Out);
		Attributes.Register();
		TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector4f> Colors = Attributes.GetVertexInstanceColors();
		TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
		UVs.SetNumChannels(1);

		const int32 VertexCount = static_cast<int32>(Mesh.positions.size());
		const int32 TriangleCount = static_cast<int32>(Mesh.triangles.size());
		Out.ReserveNewVertices(VertexCount);
		Out.ReserveNewVertexInstances(VertexCount);
		Out.ReserveNewTriangles(TriangleCount);

		const FPolygonGroupID Group = Out.CreatePolygonGroup();
		Attributes.GetPolygonGroupMaterialSlotNames()[Group] = SlotName;

		const bool bHasNormals = Mesh.normals.size() == Mesh.positions.size();
		const bool bHasAttributes = Mesh.attributes.size() == Mesh.positions.size();
		// The mesh build stores colours as sRGB bytes and the material reads
		// those bytes back raw, so hand it the linear value whose sRGB byte is
		// the number we want the material to see.
		auto Channel = [](float Value) { return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Value * 255.0f), 0, 255)); };

		TArray<FVertexInstanceID> Instances;
		Instances.SetNumUninitialized(VertexCount);
		for (int32 Index = 0; Index < VertexCount; ++Index)
		{
			const FVertexID Vertex = Out.CreateVertex();
			Positions[Vertex] = FVector3f(ToUnreal(Mesh.positions[Index]));
			const FVertexInstanceID Instance = Out.CreateVertexInstance(Vertex);
			if (bHasNormals)
			{
				Normals[Instance] = FVector3f(DirectionToUnreal(Mesh.normals[Index]).GetSafeNormal());
			}
			if (bHasAttributes)
			{
				const eclipse::CorruptionAttributes& A = Mesh.attributes[Index];
				const FColor Bytes(Channel(A.infection), Channel(A.glow), Channel(A.crack), Channel(A.plateTint));
				Colors[Instance] = FVector4f(FLinearColor(Bytes));
				UVs.Set(Instance, 0, FVector2f(A.arrival, A.height));
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

	UStaticMesh* WriteStaticMesh(const FString& PackagePath, const FString& AssetName, FMeshDescription&& Description,
		UMaterialInterface* Material, FName SlotName, bool bNanite, FText& OutError)
	{
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

		if (Mesh->GetNumSourceModels() == 0)
		{
			Mesh->AddSourceModel();
		}
		FStaticMeshSourceModel& Source = Mesh->GetSourceModel(0);
		Source.BuildSettings.bRecomputeNormals = false;
		Source.BuildSettings.bRecomputeTangents = true;
		Source.BuildSettings.bUseMikkTSpace = true;
		Source.BuildSettings.bGenerateLightmapUVs = false;
		Source.BuildSettings.bRemoveDegenerates = false;
		Source.BuildSettings.bUseFullPrecisionUVs = true;  // UV 0 carries arrival times, not texture coordinates

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

		Mesh->Build(/*bInSilent*/ true);
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
