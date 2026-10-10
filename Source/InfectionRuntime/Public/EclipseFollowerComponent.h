#pragma once

#include "Components/SkeletalMeshComponent.h"
#include "CoreMinimal.h"

#include "EclipseFollowerComponent.generated.h"

// A skeletal mesh that copies the pose of the skinned mesh it is attached to.
// Holds the Eclipse corruption grown over a character: attach it under the
// character's mesh, set its Skeletal Mesh Asset to the generated mesh, and it
// moves with every animation the character plays.
UCLASS(ClassGroup = (Eclipse), meta = (BlueprintSpawnableComponent, DisplayName = "Eclipse Follower"))
class INFECTIONRUNTIME_API UEclipseFollowerComponent : public USkeletalMeshComponent
{
	GENERATED_BODY()

public:
	UEclipseFollowerComponent();

protected:
	virtual void OnRegister() override;
};
