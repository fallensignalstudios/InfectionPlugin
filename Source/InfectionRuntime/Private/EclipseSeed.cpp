#include "EclipseSeed.h"

#include "Components/BillboardComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Texture2D.h"
#include "UObject/ConstructorHelpers.h"

AEclipseSeed::AEclipseSeed()
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	bIsEditorOnlyActor = true;

#if WITH_EDITORONLY_DATA
	Sprite = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	if (Sprite)
	{
		static ConstructorHelpers::FObjectFinderOptional<UTexture2D> Icon(TEXT("/Engine/EditorResources/S_Emitter"));
		Sprite->Sprite = Icon.Get();
		Sprite->SetupAttachment(RootComponent);
		Sprite->bIsScreenSizeScaled = true;
	}
#endif
}
