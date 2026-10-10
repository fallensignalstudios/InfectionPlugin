#include "EclipseFollowerComponent.h"

UEclipseFollowerComponent::UEclipseFollowerComponent()
{
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
}

void UEclipseFollowerComponent::OnRegister()
{
	Super::OnRegister();
	// The leader is not saved with the component, so find it again each time
	// the component comes alive: in the editor, on load and at runtime.
	if (USkinnedMeshComponent* Leader = Cast<USkinnedMeshComponent>(GetAttachParent()))
	{
		SetLeaderPoseComponent(Leader);
	}
}
