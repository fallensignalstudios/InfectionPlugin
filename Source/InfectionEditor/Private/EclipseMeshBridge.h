#pragma once

#include "CoreMinimal.h"

#include "eclipse/mesh.h"

class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
struct FMeshDescription;

// Moves meshes between Unreal (centimetres, +Z up) and the Eclipse core
// (metres, +Y up). The two frames differ by a rotation only, so winding is
// preserved; whether a mesh's triangles face outward is decided from its own
// normals rather than assumed.
namespace EclipseBridge
{
	eclipse::Vec3 ToEclipse(const FVector& Centimetres);
	FVector ToUnreal(const eclipse::Vec3& Metres);
	eclipse::Vec3 DirectionToEclipse(const FVector& Direction);
	FVector DirectionToUnreal(const eclipse::Vec3& Direction);

	struct FHost
	{
		eclipse::Mesh Mesh;          // world space relative to Origin, metres, +Y up
		FVector Origin = FVector::ZeroVector;
		// Unreal's triangle order faces the opposite way to Eclipse's (decided
		// from the asset itself). Pass to BuildMeshDescription when writing.
		bool bFlippedWinding = false;
	};

	// Reads LOD 0 of the component's mesh in world space (its full transform
	// applied), relative to the component's location.
	bool ReadHost(UStaticMeshComponent* Component, FHost& Out, FText& OutError);

	// One vertex instance per vertex. Colours and UV 0 carry the corruption
	// attributes as described in the README's output contract.
	void BuildMeshDescription(const eclipse::Mesh& Mesh, bool bFlipWinding, FName SlotName, FMeshDescription& Out);

	// Creates the asset, or rebuilds it in place if it already exists, with a
	// single material slot. The package is left dirty for the user to save.
	UStaticMesh* WriteStaticMesh(const FString& PackagePath, const FString& AssetName, FMeshDescription&& Description,
		UMaterialInterface* Material, FName SlotName, bool bNanite, FText& OutError);

	// Writes a bool or enum UPROPERTY by name. Used for engine fields whose C++
	// access has changed between engine versions; the reflected name has not.
	bool SetBoolProperty(UObject* Object, FName Property, bool bValue);
	bool SetEnumProperty(UObject* Object, FName Property, int64 Value);
}
