#include "EclipseGrowthRunner.h"

#include "Components/StaticMeshComponent.h"
#include "EclipseGrowthSettings.h"
#include "EclipseMaterials.h"
#include "EclipseMeshBridge.h"
#include "EclipseSeed.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "MeshDescription.h"
#include "Misc/ScopedSlowTask.h"
#include "ObjectTools.h"
#include "ScopedTransaction.h"

#include "eclipse/corruption.h"
#include "eclipse/growths.h"
#include "eclipse/mesh.h"

#define LOCTEXT_NAMESPACE "EclipseGrowthRunner"

namespace
{
	const FName GeneratedTag(TEXT("EclipseGenerated"));
	const FName SurfaceSlot(TEXT("EclipseSurface"));
	const FName GrowthsSlot(TEXT("EclipseGrowths"));
	constexpr float OverlayLift = 0.0015f;  // metres the shell floats above the host
	constexpr float WeldTolerance = 0.0001f;

	struct FHostTarget
	{
		AActor* Actor = nullptr;
		UStaticMeshComponent* Component = nullptr;
	};

	TArray<FHostTarget> SelectedHosts()
	{
		TArray<FHostTarget> Hosts;
		USelection* Selection = GEditor->GetSelectedActors();
		for (FSelectionIterator It(*Selection); It; ++It)
		{
			AActor* Actor = Cast<AActor>(*It);
			// Never grow on our own output.
			if (!Actor || Actor->Tags.Contains(GeneratedTag))
			{
				continue;
			}
			UStaticMeshComponent* Component = Actor->FindComponentByClass<UStaticMeshComponent>();
			if (Component && Component->GetStaticMesh())
			{
				Hosts.Add({Actor, Component});
			}
		}
		return Hosts;
	}

	eclipse::GrowthParams ToGrowthParams(const UEclipseGrowthSettings& S)
	{
		eclipse::GrowthParams P;
		P.seed = static_cast<uint32_t>(S.Seed);
		P.coverage = S.Coverage;
		P.gravity = S.Gravity;
		P.noiseStrength = S.FrontIrregularity;
		P.cavityBias = S.CreaseBias;
		P.veinReach = S.VeinReach;
		P.veinMinFlow = S.VeinThreshold * 1e-4f;  // cm^2 to m^2
		P.plateSize = S.PlateSize * 0.01f;
		P.plateStretch = S.PlateStretch;
		P.plateThickness = S.PlateThickness * 0.01f;
		P.seamGlow = S.SeamGlow;
		return P;
	}

	eclipse::FeatureParams ToFeatureParams(const UEclipseGrowthSettings& S)
	{
		eclipse::FeatureParams F;
		F.enabled = S.bGrowths;
		F.scale = S.FeatureScale;
		F.veinTubes = S.bVeinTubes;
		F.spikeLength = S.ThornLength * 0.01f;
		F.spikeSpacing = S.ThornSpacing * 0.01f;
		F.noduleChance = S.EyeChance;
		F.strandLength = S.StrandLength * 0.01f;
		F.bridgeMaxGap = S.BridgeMaxGap * 0.01f;
		F.membraneChance = S.MembraneChance;
		F.membraneHoles = S.MembraneHoles;
		return F;
	}

	// Enabled seeds within reach of the host, in a stable order so the same
	// level always grows the same way.
	std::vector<eclipse::Vec3> SeedsNear(UWorld* World, const UStaticMeshComponent* Component, const FVector& Origin)
	{
		const FBox Reach = Component->Bounds.GetBox().ExpandBy(100.0);
		TArray<AEclipseSeed*> Found;
		for (TActorIterator<AEclipseSeed> It(World); It; ++It)
		{
			if (It->bEnabled && Reach.IsInside(It->GetActorLocation()))
			{
				Found.Add(*It);
			}
		}
		Found.Sort([](const AEclipseSeed& A, const AEclipseSeed& B) { return A.GetName() < B.GetName(); });
		std::vector<eclipse::Vec3> Seeds;
		for (const AEclipseSeed* Seed : Found)
		{
			Seeds.push_back(EclipseBridge::ToEclipse(Seed->GetActorLocation() - Origin));
		}
		return Seeds;
	}

	void RemovePreviousOutput(UWorld* World, const FName& HostTag)
	{
		TArray<AActor*> Old;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->Tags.Contains(GeneratedTag) && It->Tags.Contains(HostTag))
			{
				Old.Add(*It);
			}
		}
		for (AActor* Actor : Old)
		{
			World->EditorDestroyActor(Actor, true);
		}
	}

	AStaticMeshActor* PlaceOutput(UWorld* World, AActor* Host, const FVector& Origin, UStaticMesh* Mesh,
		const FName& HostTag, const FString& Label)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transactional;
		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Origin, FRotator::ZeroRotator, Params);
		if (!Actor)
		{
			return nullptr;
		}
		UStaticMeshComponent* Component = Actor->GetStaticMeshComponent();
		// Movable: the meshes carry growth data, not lightmap UVs.
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetStaticMesh(Mesh);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Actor->Tags.Add(GeneratedTag);
		Actor->Tags.Add(HostTag);
		Actor->SetActorLabel(Label);
		Actor->AttachToActor(Host, FAttachmentTransformRules::KeepWorldTransform);
		return Actor;
	}
}

namespace EclipseGrowthRunner
{
	FText GrowOnSelection(const UEclipseGrowthSettings& Settings)
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World)
		{
			return LOCTEXT("NoWorld", "No editor world is open.");
		}
		const TArray<FHostTarget> Hosts = SelectedHosts();
		if (Hosts.Num() == 0)
		{
			return LOCTEXT("NoSelection", "Select one or more static mesh actors to grow on.");
		}

		FEclipseMaterials Materials;
		FText Error;
		const FString Folder = Settings.OutputPackagePath();
		if (!FEclipseMaterials::Ensure(Folder, false, Materials, Error))
		{
			return Error;
		}

		const eclipse::GrowthParams Params = ToGrowthParams(Settings);
		const eclipse::FeatureParams Features = ToFeatureParams(Settings);
		const bool bOverlay = Settings.OutputMode == EEclipseOutputMode::Overlay;

		FScopedTransaction Transaction(LOCTEXT("GrowTransaction", "Grow Eclipse Corruption"));
		FScopedSlowTask Task(static_cast<float>(Hosts.Num() * 4), LOCTEXT("Growing", "Growing Eclipse corruption"));
		Task.MakeDialog(/*bShowCancelButton*/ true);

		int32 Grown = 0;
		TArray<FString> Problems;
		for (const FHostTarget& Host : Hosts)
		{
			if (Task.ShouldCancel())
			{
				break;
			}
			const FString Label = Host.Actor->GetActorLabel();
			Task.EnterProgressFrame(1.0f, FText::Format(LOCTEXT("Preparing", "Preparing {0}"), FText::FromString(Label)));

			EclipseBridge::FHost Source;
			if (!EclipseBridge::ReadHost(Host.Component, Source, Error))
			{
				Problems.Add(FString::Printf(TEXT("%s: %s"), *Label, *Error.ToString()));
				continue;
			}
			const std::vector<eclipse::Vec3> Seeds = SeedsNear(World, Host.Component, Source.Origin);
			if (Seeds.empty())
			{
				Problems.Add(FString::Printf(TEXT("%s: no enabled Eclipse Seed within 1 m of it"), *Label));
				continue;
			}
			eclipse::weldVertices(Source.Mesh, WeldTolerance);
			if (!eclipse::subdivideToEdgeLength(Source.Mesh, Settings.Resolution * 0.01f, static_cast<size_t>(Settings.MaxVertices)))
			{
				Problems.Add(FString::Printf(TEXT("%s: more than %d vertices at %.1f cm resolution; raise Resolution"),
					*Label, Settings.MaxVertices, Settings.Resolution));
				continue;
			}

			Task.EnterProgressFrame(2.0f, FText::Format(LOCTEXT("GrowingOne", "Growing over {0} ({1} vertices)"),
				FText::FromString(Label), FText::AsNumber(static_cast<int64>(Source.Mesh.vertexCount()))));
			eclipse::Growth Result = eclipse::grow(Source.Mesh, Seeds, Params, Features);
			const eclipse::Mesh Surface = bOverlay ? eclipse::extractShell(Result.surface, OverlayLift) : std::move(Result.surface);

			Task.EnterProgressFrame(1.0f, LOCTEXT("Writing", "Writing meshes"));
			const FString Base = TEXT("SM_Eclipse_") + ObjectTools::SanitizeObjectName(Label);
			const FName HostTag(*(TEXT("EclipseHost_") + Host.Actor->GetName()));
			RemovePreviousOutput(World, HostTag);

			auto Write = [&](const eclipse::Mesh& Mesh, const FString& Suffix, UMaterialInterface* Material, FName Slot) -> bool
			{
				if (Mesh.triangleCount() == 0)
				{
					return true;  // nothing of this kind grew; not an error
				}
				FMeshDescription Description;
				EclipseBridge::BuildMeshDescription(Mesh, Source.bFlippedWinding, Slot, Description);
				UStaticMesh* Asset = EclipseBridge::WriteStaticMesh(Folder / TEXT("Generated"), Base + Suffix,
					MoveTemp(Description), Material, Slot, Settings.bEnableNanite, Error);
				if (!Asset)
				{
					Problems.Add(FString::Printf(TEXT("%s: %s"), *Label, *Error.ToString()));
					return false;
				}
				PlaceOutput(World, Host.Actor, Source.Origin, Asset, HostTag, Label + TEXT(" Eclipse") + Suffix);
				return true;
			};
			const bool bOk = Write(Surface, bOverlay ? TEXT("_Shell") : TEXT("_Surface"),
								 bOverlay ? Materials.Overlay : Materials.Replace, SurfaceSlot) &&
							 Write(Result.growths, TEXT("_Growths"), Materials.Growths, GrowthsSlot);
			if (!bOk)
			{
				continue;
			}
			if (!bOverlay)
			{
				// The corrupted surface stands in for the host entirely.
				Host.Component->Modify();
				Host.Component->SetVisibility(false);
			}
			++Grown;
		}

		FString Message = FString::Printf(TEXT("Grew on %d of %d meshes."), Grown, Hosts.Num());
		for (const FString& Problem : Problems)
		{
			Message += TEXT("\n") + Problem;
		}
		if (Grown > 0)
		{
			Message += TEXT("\nNew assets are unsaved; use Save All to keep them.");
		}
		return FText::FromString(Message);
	}

	FText AddSeedOnSelection()
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		const TArray<FHostTarget> Hosts = SelectedHosts();
		if (!World || Hosts.Num() == 0)
		{
			return LOCTEXT("NoSeedTarget", "Select a static mesh actor to put a seed on.");
		}
		FScopedTransaction Transaction(LOCTEXT("SeedTransaction", "Add Eclipse Seed"));
		for (const FHostTarget& Host : Hosts)
		{
			const FBox Box = Host.Component->Bounds.GetBox();
			const FVector Top(Box.GetCenter().X, Box.GetCenter().Y, Box.Max.Z);
			FActorSpawnParameters Params;
			Params.ObjectFlags |= RF_Transactional;
			if (AEclipseSeed* Seed = World->SpawnActor<AEclipseSeed>(Top, FRotator::ZeroRotator, Params))
			{
				Seed->SetActorLabel(Host.Actor->GetActorLabel() + TEXT(" Seed"));
			}
		}
		return FText::Format(LOCTEXT("SeedsAdded", "Added {0} seed(s). Move them to where the corruption should start."),
			FText::AsNumber(Hosts.Num()));
	}

	FText RebuildMaterials(const UEclipseGrowthSettings& Settings)
	{
		FEclipseMaterials Materials;
		FText Error;
		if (!FEclipseMaterials::Ensure(Settings.OutputPackagePath(), true, Materials, Error))
		{
			return Error;
		}
		return LOCTEXT("Rebuilt", "Rebuilt M_EclipseBiomass and its instances.");
	}
}

#undef LOCTEXT_NAMESPACE
