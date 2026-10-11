#pragma once

#include "CoreMinimal.h"

#include "eclipse/mesh.h"
#include "eclipse/skin.h"

class UMaterialInterface;
class USkeletalMesh;
class USkeletalMeshComponent;
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
		FTransform Transform;        // the component's, which Mesh was read through
		// Unreal's triangle order faces the opposite way to Eclipse's (decided
		// from the asset itself). Pass to BuildMeshDescription when writing.
		bool bFlippedWinding = false;
	};

	// Reads LOD 0 of the component's mesh in world space (its full transform
	// applied), relative to the component's location.
	bool ReadHost(UStaticMeshComponent* Component, FHost& Out, FText& OutError);

	// The same for a skeletal mesh in its reference pose, plus each vertex's
	// bone influences (the asset's bone indices, strongest eight kept).
	bool ReadSkeletalHost(USkeletalMeshComponent* Component, FHost& Out, eclipse::SkinTable& OutSkin, FText& OutError);

	// One vertex instance per vertex. Colours and UV 0 carry the corruption
	// attributes as described in the README's output contract. bMeshLocal
	// maps the geometry back into the host component's own space (needed for
	// skinning); otherwise it stays world space relative to Host.Origin. With
	// Skin, the description carries bone weights for a skeletal mesh.
	void BuildMeshDescription(const eclipse::Mesh& Mesh, const FHost& Host, bool bMeshLocal, FName SlotName,
		FMeshDescription& Out, const eclipse::SkinTable* Skin = nullptr);

	// How a generated mesh is built.
	struct FOutputOptions
	{
		bool bNanite = true;
		// Levels of detail, LOD 0 included; the rest are reduced by the engine
		// at build time, each keeping LODReduction of the triangles before it.
		// Nanite static meshes keep one.
		int32 LODCount = 1;
		float LODReduction = 0.5f;
		// Collide against the mesh's own triangles (complex as simple).
		bool bCollision = false;
	};

	// The share of LOD 0's triangles that LOD Index keeps.
	float LODTriangleShare(int32 Index, float Reduction);

	// Creates the asset, or rebuilds it in place if it already exists, with a
	// single material slot. The package is left dirty for the user to save.
	UStaticMesh* WriteStaticMesh(const FString& PackagePath, const FString& AssetName, FMeshDescription&& Description,
		UMaterialInterface* Material, FName SlotName, const FOutputOptions& Options, FText& OutError);

	// A skeletal mesh on the template's skeleton, created as a copy of the
	// template (or rebuilt in place) with one material and only its own
	// LODs: LOD 0 from Description, the rest reduced from it (LODCount and
	// LODReduction of Options; the rest of Options does not apply).
	USkeletalMesh* WriteSkeletalMesh(USkeletalMesh* Template, const FString& PackagePath, const FString& AssetName,
		FMeshDescription&& Description, UMaterialInterface* Material, FName SlotName, FText& OutError,
		const FOutputOptions& Options = FOutputOptions());

	// Writes a bool or enum UPROPERTY by name. Used for engine fields whose C++
	// access has changed between engine versions; the reflected name has not.
	bool SetBoolProperty(UObject* Object, FName Property, bool bValue);
	bool SetEnumProperty(UObject* Object, FName Property, int64 Value);
}
