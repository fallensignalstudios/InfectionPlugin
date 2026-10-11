#include "EclipseMask.h"

#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"

AEclipseMask::AEclipseMask()
{
	bIsEditorOnlyActor = true;

	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	Box->InitBoxExtent(FVector(100.0f));
	Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Box->SetGenerateOverlapEvents(false);
	Box->ShapeColor = FColor(170, 60, 255);
	RootComponent = Box;

	Sphere = CreateDefaultSubobject<USphereComponent>(TEXT("Sphere"));
	Sphere->InitSphereRadius(100.0f);
	Sphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Sphere->SetGenerateOverlapEvents(false);
	Sphere->ShapeColor = FColor(170, 60, 255);
	Sphere->SetupAttachment(Box);
	Sphere->SetVisibility(false);
}

void AEclipseMask::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// Show only the shape in use; colour by mode so masks read at a glance.
	const FColor Colour = Mode == EEclipseMaskMode::Block ? FColor(255, 60, 60)
						  : Mode == EEclipseMaskMode::Bare ? FColor(240, 220, 80)
														   : FColor(170, 60, 255);
	Box->ShapeColor = Colour;
	Sphere->ShapeColor = Colour;
	Box->SetVisibility(Shape == EEclipseMaskShape::Box);
	Sphere->SetVisibility(Shape == EEclipseMaskShape::Sphere);
	Sphere->SetSphereRadius(Box->GetUnscaledBoxExtent().X);
}

void AEclipseMask::GetWorldShape(FTransform& OutTransform, FVector& OutHalfExtents) const
{
	OutTransform = Box->GetComponentTransform();
	const FVector Scale = OutTransform.GetScale3D().GetAbs();
	OutTransform.SetScale3D(FVector::OneVector);
	if (Shape == EEclipseMaskShape::Sphere)
	{
		const float Radius = Box->GetUnscaledBoxExtent().X * Scale.GetMax();
		OutHalfExtents = FVector(Radius);
	}
	else
	{
		OutHalfExtents = Box->GetUnscaledBoxExtent() * Scale;
	}
}
