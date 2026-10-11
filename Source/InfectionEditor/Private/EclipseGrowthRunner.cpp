#include "EclipseGrowthRunner.h"

#include <algorithm>

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EclipseFollowerComponent.h"
#include "EclipseInfectionMap.h"
#include "EclipseMask.h"
#include "EclipseSpreadComponent.h"
#include "EclipseGrowthSettings.h"
#include "EclipseMaterials.h"
#include "EclipseMeshBridge.h"
#include "EclipseSeed.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "MeshDescription.h"
#include "Misc/ScopedSlowTask.h"
#include "UObject/Package.h"
#include "ObjectTools.h"
#include "ScopedTransaction.h"

#include "eclipse/corruption.h"
#include "eclipse/growths.h"
#include "eclipse/presets.h"
#include "eclipse/scene.h"
#include "eclipse/mesh.h"
#include "eclipse/skin.h"

#define LOCTEXT_NAMESPACE "EclipseGrowthRunner"

namespace
{
	const FName GeneratedTag(TEXT("EclipseGenerated"));
	// On a host mesh component that Replace mode hid, so it can be shown again.
	const FName HiddenByEclipseTag(TEXT("EclipseHidden"));
	const FName SurfaceSlot(TEXT("EclipseSurface"));
	const FName GrowthsSlot(TEXT("EclipseGrowths"));
	const FName FleshSlot(TEXT("EclipseFlesh"));
	constexpr float OverlayLift = 0.0015f;  // metres the shell floats above the host
	constexpr float WeldTolerance = 0.0001f;

	// A mesh to grow on: a static mesh, or a skeletal mesh whose corruption
	// must follow its animation.
	struct FHostTarget
	{
		AActor* Actor = nullptr;
		UMeshComponent* Component = nullptr;
		UStaticMeshComponent* Static = nullptr;
		USkeletalMeshComponent* Skeletal = nullptr;
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
			// A character's skeletal mesh takes precedence over any props on
			// it. Our own followers are skeletal components too: skip them, or
			// a second grow would mistake one for the host.
			TInlineComponentArray<USkeletalMeshComponent*> SkeletalComponents(Actor);
			USkeletalMeshComponent* Skeletal = nullptr;
			for (USkeletalMeshComponent* Candidate : SkeletalComponents)
			{
				if (Candidate->GetSkeletalMeshAsset() && !Candidate->IsA<UEclipseFollowerComponent>())
				{
					Skeletal = Candidate;
					break;
				}
			}
			if (Skeletal)
			{
				Hosts.Add({Actor, Skeletal, nullptr, Skeletal});
				continue;
			}
			UStaticMeshComponent* Static = Actor->FindComponentByClass<UStaticMeshComponent>();
			if (Static && Static->GetStaticMesh())
			{
				Hosts.Add({Actor, Static, Static, nullptr});
			}
		}
		return Hosts;
	}

	// The preset's full set of values, before the panel's own settings are
	// laid over them: it also supplies those the panel does not show.
	void PresetBaseline(const UEclipseGrowthSettings& S, eclipse::GrowthParams& P, eclipse::FeatureParams& F)
	{
		eclipse::applyPreset(static_cast<eclipse::Preset>(S.Preset), P, F);
	}

	eclipse::GrowthParams ToGrowthParams(const UEclipseGrowthSettings& S)
	{
		eclipse::GrowthParams P;
		eclipse::FeatureParams Unused;
		PresetBaseline(S, P, Unused);
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
		P.bruising = S.Bruising;
		P.haloReach = S.HaloReach;
		return P;
	}

	eclipse::FeatureParams ToFeatureParams(const UEclipseGrowthSettings& S)
	{
		eclipse::GrowthParams Unused;
		eclipse::FeatureParams F;
		PresetBaseline(S, Unused, F);
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
		F.tendrils = S.bRoots;
		F.tendrilDensity = S.RootDensity;
		F.tendrilLength = S.RootLength * 0.01f;
		F.hearts = S.bHearts;
		F.heartRadius = S.HeartSize * 0.01f;
		F.mawCoverage = S.MawCoverage;
		F.vertebrae = S.bVertebrae;
		F.fusion = S.Fusion;
		F.roughness = S.Roughness;
		F.tendrilBranching = S.RootBranching;
		F.spikeBarbs = S.HornBarbs;
		F.slime = S.bSlime;
		F.slimeChance = S.SlimeChance;
		F.sacs = S.bSacs;
		F.sacChance = S.SacChance;
		F.spires = S.bSpires;
		F.spireHeight = S.SpireHeight * 0.01f;
		F.ribcages = S.bRibcages;
		F.ribcageLength = S.RibcageLength * 0.01f;
		F.pods = S.bPods;
		F.podSize = S.PodSize * 0.01f;
		F.shelves = S.bShelves;
		F.shelfSize = S.ShelfSize * 0.01f;
		return F;
	}

	FEclipseMaterialOptions ToMaterialOptions(const UEclipseGrowthSettings& S)
	{
		FEclipseMaterialOptions O;
		O.bDisplacement = S.bEnableNanite && S.bNaniteDisplacement;
		O.DisplacementDepth = S.DisplacementDepth;
		O.HaloReach = S.HaloReach;
		return O;
	}

	// Enabled seeds within reach of the host, in a stable order so the same
	// level always grows the same way.
	std::vector<eclipse::Vec3> SeedsNear(UWorld* World, const UPrimitiveComponent* Component, const FVector& Origin)
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

	// Every enabled Eclipse Mask in the level, in the frame of a growth
	// whose geometry is relative to Origin.
	std::vector<eclipse::MaskVolume> MasksIn(UWorld* World, const FVector& Origin)
	{
		std::vector<eclipse::MaskVolume> Masks;
		for (TActorIterator<AEclipseMask> It(World); It; ++It)
		{
			if (!It->bEnabled)
			{
				continue;
			}
			FTransform Frame;
			FVector HalfExtents;
			It->GetWorldShape(Frame, HalfExtents);
			eclipse::MaskVolume Mask;
			Mask.mode = It->Mode == EEclipseMaskMode::Block ? eclipse::MaskVolume::Mode::Block
					  : It->Mode == EEclipseMaskMode::Bare ? eclipse::MaskVolume::Mode::Bare
														   : eclipse::MaskVolume::Mode::Dense;
			Mask.shape = It->Shape == EEclipseMaskShape::Sphere ? eclipse::MaskVolume::Shape::Sphere : eclipse::MaskVolume::Shape::Box;
			Mask.centre = EclipseBridge::ToEclipse(Frame.GetLocation() - Origin);
			Mask.axes[0] = EclipseBridge::DirectionToEclipse(Frame.GetUnitAxis(EAxis::X));
			Mask.axes[1] = EclipseBridge::DirectionToEclipse(Frame.GetUnitAxis(EAxis::Y));
			Mask.axes[2] = EclipseBridge::DirectionToEclipse(Frame.GetUnitAxis(EAxis::Z));
			Mask.halfExtents = {static_cast<float>(HalfExtents.X) * 0.01f, static_cast<float>(HalfExtents.Y) * 0.01f,
				static_cast<float>(HalfExtents.Z) * 0.01f};
			Mask.falloff = It->Falloff * 0.01f;
			Mask.strength = It->Strength;
			Masks.push_back(Mask);
		}
		return Masks;
	}

	// Followers and the spread component from an earlier grow.
	void RemovePreviousInstanceComponents(AActor* Host)
	{
		TArray<UActorComponent*> Old;
		for (UActorComponent* Component : Host->GetInstanceComponents())
		{
			if (Component && Component->ComponentTags.Contains(GeneratedTag))
			{
				Old.Add(Component);
			}
		}
		if (Old.Num() > 0)
		{
			Host->Modify();
		}
		for (UActorComponent* Component : Old)
		{
			Component->Modify();
			Host->RemoveInstanceComponent(Component);
			Component->DestroyComponent();
		}
	}

	// The sparse where-and-when record the spread component answers gameplay
	// queries from, in the host actor's space.
	UEclipseInfectionMap* WriteInfectionMap(const FString& PackagePath, const FString& AssetName,
		const std::vector<eclipse::InfectionSample>& Samples, const FVector& Origin, const FTransform& ActorTransform,
		float SpacingCm)
	{
		UPackage* Package = CreatePackage(*(PackagePath / AssetName));
		Package->FullyLoad();
		if (UObject* Existing = FindObject<UObject>(Package, *AssetName); Existing && !Existing->IsA<UEclipseInfectionMap>())
		{
			return nullptr;  // the name is taken by something else; growth works without a map
		}
		UEclipseInfectionMap* Map = FindObject<UEclipseInfectionMap>(Package, *AssetName);
		const bool bCreated = Map == nullptr;
		if (bCreated)
		{
			Map = NewObject<UEclipseInfectionMap>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		}
		else
		{
			Map->Modify();
		}
		TArray<FVector3f> Points;
		TArray<float> Arrival, Infection;
		Points.Reserve(static_cast<int32>(Samples.size()));
		for (const eclipse::InfectionSample& Sample : Samples)
		{
			const FVector World = EclipseBridge::ToUnreal(Sample.position) + Origin;
			Points.Add(FVector3f(ActorTransform.InverseTransformPosition(World)));
			Arrival.Add(Sample.arrival);
			Infection.Add(Sample.infection);
		}
		// Samples were spaced in world centimetres before their positions were
		// transformed. Keep a conservative actor-space spacing for the map grid.
		const float MinScale = FMath::Max(static_cast<float>(ActorTransform.GetScale3D().GetAbsMin()), SMALL_NUMBER);
		Map->SetSamples(MoveTemp(Points), MoveTemp(Arrival), MoveTemp(Infection), SpacingCm / MinScale);
		if (bCreated)
		{
			FAssetRegistryModule::AssetCreated(Map);
		}
		Map->MarkPackageDirty();
		return Map;
	}

	void PlaceSpread(AActor* Host, UEclipseInfectionMap* Map, float FullGrowth)
	{
		Host->Modify();
		const FName Unique = MakeUniqueObjectName(Host, UEclipseSpreadComponent::StaticClass(), TEXT("EclipseSpread"));
		UEclipseSpreadComponent* Spread = NewObject<UEclipseSpreadComponent>(Host, Unique, RF_Transactional);
		Spread->InfectionMap = Map;
		Spread->FullGrowth = FullGrowth;
		Spread->Growth = FullGrowth;
		Spread->ComponentTags.Add(GeneratedTag);
		Host->AddInstanceComponent(Spread);
		Spread->RegisterComponent();
	}

	// Replace mode hides the host behind its corrupted surface; Overlay and
	// removal show it again, but only if it was Eclipse that hid it.
	void SetHostHidden(UMeshComponent* Component, bool bHidden)
	{
		if (bHidden == Component->ComponentTags.Contains(HiddenByEclipseTag))
		{
			return;
		}
		Component->Modify();
		Component->SetVisibility(!bHidden);
		if (bHidden)
		{
			Component->ComponentTags.Add(HiddenByEclipseTag);
		}
		else
		{
			Component->ComponentTags.Remove(HiddenByEclipseTag);
		}
	}

	void PlaceFollower(AActor* Host, USkeletalMeshComponent* Leader, USkeletalMesh* Mesh, const FString& Name)
	{
		Host->Modify();
		const FName Unique = MakeUniqueObjectName(Host, UEclipseFollowerComponent::StaticClass(), FName(*Name));
		UEclipseFollowerComponent* Follower = NewObject<UEclipseFollowerComponent>(Host, Unique, RF_Transactional);
		Follower->SetSkeletalMeshAsset(Mesh);
		Follower->ComponentTags.Add(GeneratedTag);
		Follower->SetupAttachment(Leader);
		Host->AddInstanceComponent(Follower);
		Follower->RegisterComponent();
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
			return LOCTEXT("NoSelection", "Select one or more static or skeletal mesh actors to grow on.");
		}

		FEclipseMaterials Materials;
		FText Error;
		const FString Folder = Settings.OutputPackagePath();
		if (!FEclipseMaterials::Ensure(Folder, false, Materials, Error, ToMaterialOptions(Settings)))
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
		const FString Generated = Folder / TEXT("Generated");

		// A static mesh asset for one part of the output, placed as an actor
		// attached to HostActor. Geometry is relative to Frame.Origin.
		auto WriteStatic = [&](const eclipse::Mesh& Mesh, const EclipseBridge::FHost& Frame, const FString& AssetName,
							   UMaterialInterface* Material, FName Slot, AActor* HostActor, const FName& HostTag,
							   const FString& ActorLabel) -> bool
		{
			if (Mesh.triangleCount() == 0)
			{
				return true;  // nothing of this kind grew; not an error
			}
			FMeshDescription Description;
			EclipseBridge::BuildMeshDescription(Mesh, Frame, /*bMeshLocal*/ false, Slot, Description);
			UStaticMesh* Asset = EclipseBridge::WriteStaticMesh(Generated, AssetName, MoveTemp(Description), Material, Slot,
				Settings.bEnableNanite, Error);
			if (!Asset)
			{
				Problems.Add(FString::Printf(TEXT("%s: %s"), *ActorLabel, *Error.ToString()));
				return false;
			}
			PlaceOutput(World, HostActor, Frame.Origin, Asset, HostTag, ActorLabel);
			return true;
		};

		// Static meshes grow in groups: all of them as one surface, or one by
		// one. The first of a group carries its growths and soft tissue.
		auto GrowStaticGroup = [&](const TArray<FHostTarget>& Group)
		{
			struct FPiece
			{
				const FHostTarget* Host = nullptr;
				EclipseBridge::FHost Source;
				FString Label;
			};
			TArray<FPiece> Pieces;
			size_t TotalVertices = 0;
			for (const FHostTarget& Host : Group)
			{
				FPiece Piece;
				Piece.Host = &Host;
				Piece.Label = Host.Actor->GetActorLabel();
				Task.EnterProgressFrame(1.0f, FText::Format(LOCTEXT("Preparing", "Preparing {0}"), FText::FromString(Piece.Label)));
				if (!EclipseBridge::ReadHost(Host.Static, Piece.Source, Error))
				{
					Problems.Add(FString::Printf(TEXT("%s: %s"), *Piece.Label, *Error.ToString()));
					continue;
				}
				eclipse::weldVertices(Piece.Source.Mesh, WeldTolerance);
				if (!eclipse::subdivideToEdgeLength(Piece.Source.Mesh, Settings.Resolution * 0.01f, static_cast<size_t>(Settings.MaxVertices)))
				{
					Problems.Add(FString::Printf(TEXT("%s: more than %d vertices at %.1f cm resolution; raise Resolution"),
						*Piece.Label, Settings.MaxVertices, Settings.Resolution));
					continue;
				}
				TotalVertices += Piece.Source.Mesh.vertexCount();
				Pieces.Add(MoveTemp(Piece));
			}
			if (Pieces.Num() == 0)
			{
				return;
			}
			if (TotalVertices > static_cast<size_t>(Settings.MaxVertices))
			{
				Problems.Add(FString::Printf(TEXT("%d meshes together: more than %d vertices at %.1f cm resolution; raise Resolution or Max Vertices"),
					Pieces.Num(), Settings.MaxVertices, Settings.Resolution));
				return;
			}

			// One frame for the group: everything relative to the first piece.
			const FVector Origin = Pieces[0].Source.Origin;
			std::vector<eclipse::Mesh> Meshes;
			std::vector<eclipse::Vec3> Seeds;
			for (const FPiece& Piece : Pieces)
			{
				eclipse::Mesh Mesh = Piece.Source.Mesh;
				const eclipse::Vec3 Shift = EclipseBridge::ToEclipse(Piece.Source.Origin - Origin);
				for (eclipse::Vec3& P : Mesh.positions)
				{
					P += Shift;
				}
				Meshes.push_back(std::move(Mesh));
				for (const eclipse::Vec3& Seed : SeedsNear(World, Piece.Host->Component, Origin))
				{
					const bool bKnown = std::any_of(Seeds.begin(), Seeds.end(),
						[&](const eclipse::Vec3& Other) { return eclipse::length(Other - Seed) < 1e-4f; });
					if (!bKnown)
					{
						Seeds.push_back(Seed);
					}
				}
			}
			FString Labels = Pieces[0].Label;
			for (int32 Index = 1; Index < Pieces.Num(); ++Index)
			{
				Labels += TEXT(", ") + Pieces[Index].Label;
			}
			if (Seeds.empty())
			{
				Problems.Add(FString::Printf(TEXT("%s: no enabled Eclipse Seed within 1 m"), *Labels));
				return;
			}
			std::vector<const eclipse::Mesh*> Pointers;
			for (const eclipse::Mesh& Mesh : Meshes)
			{
				Pointers.push_back(&Mesh);
			}

			Task.EnterProgressFrame(2.0f, FText::Format(LOCTEXT("GrowingGroup", "Growing over {0} ({1} vertices)"),
				FText::FromString(Labels), FText::AsNumber(static_cast<int64>(TotalVertices))));
			const eclipse::SceneGrowth Scene = eclipse::growScene(Pointers, Seeds, Params, Features,
				Settings.bGrowAsOne ? Settings.ContactGap * 0.01f : 0.0f, MasksIn(World, Origin));
			const eclipse::Growth& Result = Scene.growth;

			// Each piece's surface, split back out of the whole.
			std::vector<uint32_t> ShellSources, ShellOwner;
			eclipse::Mesh Shell;
			if (bOverlay)
			{
				Shell = eclipse::extractShell(Result.surface, OverlayLift, &ShellSources, Params.haloReach);
				for (uint32_t Source : ShellSources)
				{
					ShellOwner.push_back(Scene.owner[Source]);
				}
			}
			const eclipse::Mesh& Surfaces = bOverlay ? Shell : Result.surface;
			const std::vector<uint32_t>& SurfaceOwner = bOverlay ? ShellOwner : Scene.owner;

			Task.EnterProgressFrame(1.0f, LOCTEXT("Writing", "Writing meshes"));
			FName PrimaryTag;
			for (int32 Index = 0; Index < Pieces.Num(); ++Index)
			{
				const FPiece& Piece = Pieces[Index];
				AActor* HostActor = Piece.Host->Actor;
				const FString Base = TEXT("SM_Eclipse_") + ObjectTools::SanitizeObjectName(Piece.Label);
				const FName HostTag(*(TEXT("EclipseHost_") + HostActor->GetName()));
				if (Index == 0)
				{
					PrimaryTag = HostTag;
				}
				RemovePreviousOutput(World, HostTag);
				RemovePreviousInstanceComponents(HostActor);

				EclipseBridge::FHost Frame = Piece.Source;
				Frame.Origin = Origin;  // the group's geometry is relative to its first piece
				const eclipse::Mesh Part = eclipse::subMesh(Surfaces, SurfaceOwner, static_cast<uint32_t>(Index));
				if (!WriteStatic(Part, Frame, Base + (bOverlay ? TEXT("_Shell") : TEXT("_Surface")),
						bOverlay ? Materials.Overlay : Materials.Replace, SurfaceSlot, HostActor, HostTag,
						Piece.Label + (bOverlay ? TEXT(" Eclipse_Shell") : TEXT(" Eclipse_Surface"))))
				{
					continue;
				}
				const float Spacing = 0.05f * Settings.FeatureScale;
				const std::vector<eclipse::InfectionSample> Samples =
					eclipse::sampleInfection(eclipse::subMesh(Result.surface, Scene.owner, static_cast<uint32_t>(Index)), Spacing);
				if (!Samples.empty())
				{
					UEclipseInfectionMap* Map = WriteInfectionMap(Generated, TEXT("IM_Eclipse_") + ObjectTools::SanitizeObjectName(Piece.Label),
						Samples, Origin, HostActor->GetActorTransform(), Spacing * 100.0f);
					// Everything grown, including veins that ran ahead of the front and the
					// growths that appear just after their tissue, fully revealed.
					PlaceSpread(HostActor, Map, 1.0f + Settings.VeinReach + 0.15f);
				}
				// In Replace mode the corrupted surface stands in for the host entirely.
				SetHostHidden(Piece.Host->Component, !bOverlay);
				++Grown;
			}

			// Growths and soft tissue for the whole group, carried by its first piece.
			const FString Base = TEXT("SM_Eclipse_") + ObjectTools::SanitizeObjectName(Pieces[0].Label);
			WriteStatic(Result.growths, Pieces[0].Source, Base + TEXT("_Growths"), Materials.Growths, GrowthsSlot,
				Pieces[0].Host->Actor, PrimaryTag, Pieces[0].Label + TEXT(" Eclipse_Growths"));
			WriteStatic(Result.flesh, Pieces[0].Source, Base + TEXT("_Flesh"), Materials.Flesh, FleshSlot,
				Pieces[0].Host->Actor, PrimaryTag, Pieces[0].Label + TEXT(" Eclipse_Flesh"));
		};

		TArray<FHostTarget> Statics;
		for (const FHostTarget& Host : Hosts)
		{
			if (!Host.Skeletal)
			{
				Statics.Add(Host);
			}
		}
		if (Settings.bGrowAsOne)
		{
			if (Statics.Num() > 0)
			{
				GrowStaticGroup(Statics);
			}
		}
		else
		{
			for (const FHostTarget& Host : Statics)
			{
				if (Task.ShouldCancel())
				{
					break;
				}
				GrowStaticGroup({Host});
			}
		}

		// Characters, one by one: their corruption follows their own skeleton.
		for (const FHostTarget& Host : Hosts)
		{
			if (!Host.Skeletal)
			{
				continue;
			}
			if (Task.ShouldCancel())
			{
				break;
			}
			const FString Label = Host.Actor->GetActorLabel();
			Task.EnterProgressFrame(1.0f, FText::Format(LOCTEXT("Preparing", "Preparing {0}"), FText::FromString(Label)));

			EclipseBridge::FHost Source;
			eclipse::SkinTable Skin;  // skeletal hosts only
			const bool bSkeletal = Host.Skeletal != nullptr;
			const bool bRead = bSkeletal ? EclipseBridge::ReadSkeletalHost(Host.Skeletal, Source, Skin, Error)
										 : EclipseBridge::ReadHost(Host.Static, Source, Error);
			if (!bRead)
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
			// Weld and refine, carrying bone weights along for skeletal hosts.
			std::vector<uint32_t> Remap;
			eclipse::weldVertices(Source.Mesh, WeldTolerance, &Remap);
			std::vector<std::array<uint32_t, 2>> Parents;
			if (!eclipse::subdivideToEdgeLength(Source.Mesh, Settings.Resolution * 0.01f, static_cast<size_t>(Settings.MaxVertices), &Parents))
			{
				Problems.Add(FString::Printf(TEXT("%s: more than %d vertices at %.1f cm resolution; raise Resolution"),
					*Label, Settings.MaxVertices, Settings.Resolution));
				continue;
			}
			if (bSkeletal)
			{
				Skin = eclipse::skinAfterSubdivision(eclipse::skinAfterWeld(Skin, Remap, Source.Mesh.vertexCount() - Parents.size()), Parents);
			}

			Task.EnterProgressFrame(2.0f, FText::Format(LOCTEXT("GrowingOne", "Growing over {0} ({1} vertices)"),
				FText::FromString(Label), FText::AsNumber(static_cast<int64>(Source.Mesh.vertexCount()))));
			eclipse::GrowthGuide Guide;
			eclipse::applyMaskVolumes(Source.Mesh, MasksIn(World, Source.Origin), Guide);
			eclipse::Growth Result = eclipse::grow(Source.Mesh, Seeds, Params, Features, &Guide);
			std::vector<uint32_t> ShellSources;
			const eclipse::Mesh Surface = bOverlay ? eclipse::extractShell(Result.surface, OverlayLift, &ShellSources, Params.haloReach) : std::move(Result.surface);
			eclipse::SkinTable SurfaceSkin, GrowthSkin, FleshSkin;
			if (bSkeletal)
			{
				SurfaceSkin = bOverlay ? eclipse::skinFromSources(Skin, ShellSources) : Skin;
				GrowthSkin = eclipse::bindToNearest(Source.Mesh.positions, Skin, Result.growths.positions);
				FleshSkin = eclipse::bindToNearest(Source.Mesh.positions, Skin, Result.flesh.positions);
			}

			Task.EnterProgressFrame(1.0f, LOCTEXT("Writing", "Writing meshes"));
			const FString Base = (bSkeletal ? TEXT("SK_Eclipse_") : TEXT("SM_Eclipse_")) + ObjectTools::SanitizeObjectName(Label);
			const FName HostTag(*(TEXT("EclipseHost_") + Host.Actor->GetName()));
			RemovePreviousOutput(World, HostTag);
			RemovePreviousInstanceComponents(Host.Actor);

			auto Write = [&](const eclipse::Mesh& Mesh, const eclipse::SkinTable& MeshSkin, const FString& Suffix,
							 UMaterialInterface* Material, FName Slot) -> bool
			{
				if (Mesh.triangleCount() == 0)
				{
					return true;  // nothing of this kind grew; not an error
				}
				FMeshDescription Description;
				if (bSkeletal)
				{
					if (MeshSkin.size() != Mesh.vertexCount())
					{
						Problems.Add(FString::Printf(TEXT("%s: bone weights did not line up with the generated mesh"), *Label));
						return false;
					}
					EclipseBridge::BuildMeshDescription(Mesh, Source, /*bMeshLocal*/ true, Slot, Description, &MeshSkin);
					USkeletalMesh* Asset = EclipseBridge::WriteSkeletalMesh(Host.Skeletal->GetSkeletalMeshAsset(), Generated,
						Base + Suffix, MoveTemp(Description), Material, Slot, Error);
					if (!Asset)
					{
						Problems.Add(FString::Printf(TEXT("%s: %s"), *Label, *Error.ToString()));
						return false;
					}
					PlaceFollower(Host.Actor, Host.Skeletal, Asset, TEXT("Eclipse") + Suffix);
					return true;
				}
				EclipseBridge::BuildMeshDescription(Mesh, Source, /*bMeshLocal*/ false, Slot, Description);
				UStaticMesh* Asset = EclipseBridge::WriteStaticMesh(Generated, Base + Suffix,
					MoveTemp(Description), Material, Slot, Settings.bEnableNanite, Error);
				if (!Asset)
				{
					Problems.Add(FString::Printf(TEXT("%s: %s"), *Label, *Error.ToString()));
					return false;
				}
				PlaceOutput(World, Host.Actor, Source.Origin, Asset, HostTag, Label + TEXT(" Eclipse") + Suffix);
				return true;
			};
			const bool bOk = Write(Surface, SurfaceSkin, bOverlay ? TEXT("_Shell") : TEXT("_Surface"),
								 bOverlay ? Materials.Overlay : Materials.Replace, SurfaceSlot) &&
							 Write(Result.growths, GrowthSkin, TEXT("_Growths"), Materials.Growths, GrowthsSlot) &&
							 Write(Result.flesh, FleshSkin, TEXT("_Flesh"), Materials.Flesh, FleshSlot);
			if (!bOk)
			{
				continue;
			}
			{
				// In Replace mode the full surface was moved into Surface.
				const eclipse::Mesh& Full = bOverlay ? Result.surface : Surface;
				const float Spacing = 0.05f * Settings.FeatureScale;
				UEclipseInfectionMap* Map = WriteInfectionMap(Folder / TEXT("Generated"), TEXT("IM_Eclipse_") + ObjectTools::SanitizeObjectName(Label),
					eclipse::sampleInfection(Full, Spacing), Source.Origin, Host.Actor->GetActorTransform(), Spacing * 100.0f);
				// Everything grown, including veins that ran ahead of the front and the
				// growths that appear just after their tissue, fully revealed.
				PlaceSpread(Host.Actor, Map, 1.0f + Settings.VeinReach + 0.15f);
			}
			if (!bOverlay && bSkeletal)
			{
				// Hidden, the leader must still animate for its followers.
				Host.Component->Modify();
				Host.Skeletal->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			}
			// In Replace mode the corrupted surface stands in for the host entirely.
			SetHostHidden(Host.Component, !bOverlay);
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

	FText RemoveFromSelection()
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World)
		{
			return LOCTEXT("NoWorldRemove", "No editor world is open.");
		}
		// The selected hosts, or the hosts of selected output.
		TArray<AActor*> Targets;
		USelection* Selection = GEditor->GetSelectedActors();
		for (FSelectionIterator It(*Selection); It; ++It)
		{
			AActor* Actor = Cast<AActor>(*It);
			while (Actor && Actor->Tags.Contains(GeneratedTag))
			{
				Actor = Actor->GetAttachParentActor();
			}
			if (Actor)
			{
				Targets.AddUnique(Actor);
			}
		}
		if (Targets.Num() == 0)
		{
			return LOCTEXT("NoRemoveTarget", "Select grown meshes, or their corruption, to remove it from.");
		}
		FScopedTransaction Transaction(LOCTEXT("RemoveTransaction", "Remove Eclipse Corruption"));
		int32 Cleaned = 0;
		for (AActor* Host : Targets)
		{
			const FName HostTag(*(TEXT("EclipseHost_") + Host->GetName()));
			bool bHad = false;
			for (TActorIterator<AActor> It(World); It && !bHad; ++It)
			{
				bHad = It->Tags.Contains(GeneratedTag) && It->Tags.Contains(HostTag);
			}
			for (UActorComponent* Component : Host->GetInstanceComponents())
			{
				bHad |= Component && Component->ComponentTags.Contains(GeneratedTag);
			}
			RemovePreviousOutput(World, HostTag);
			RemovePreviousInstanceComponents(Host);
			TInlineComponentArray<UMeshComponent*> Meshes(Host);
			for (UMeshComponent* Mesh : Meshes)
			{
				bHad |= Mesh->ComponentTags.Contains(HiddenByEclipseTag);
				SetHostHidden(Mesh, false);
			}
			Cleaned += bHad ? 1 : 0;
		}
		return FText::Format(LOCTEXT("Removed",
			"Removed the corruption from {0} of {1} actor(s). Its generated assets stay in the output folder; delete them there if unused."),
			FText::AsNumber(Cleaned), FText::AsNumber(Targets.Num()));
	}

	FText AddSeedOnSelection()
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		const TArray<FHostTarget> Hosts = SelectedHosts();
		if (!World || Hosts.Num() == 0)
		{
			return LOCTEXT("NoSeedTarget", "Select a mesh actor to put a seed on.");
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
		if (!FEclipseMaterials::Ensure(Settings.OutputPackagePath(), true, Materials, Error, ToMaterialOptions(Settings)))
		{
			return Error;
		}
		return LOCTEXT("Rebuilt", "Rebuilt M_EclipseBiomass and its instances.");
	}
}

#undef LOCTEXT_NAMESPACE
