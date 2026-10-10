#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "UObject/Object.h"

#include "EclipseGrowthSettings.generated.h"

UENUM()
enum class EEclipseOutputMode : uint8
{
	// A shell of corruption laid just over the untouched host, which keeps its
	// own materials wherever the corruption has not reached. Best for real assets.
	Overlay,
	// The whole host replaced by the corrupted surface, its clean areas shaded
	// with the material's Host Color. For untextured or throwaway hosts.
	Replace,
};

// Every setting of the Eclipse Growth panel, remembered per user and project.
// Lengths are in centimetres, like the rest of the editor; the generator
// works in metres and converts.
UCLASS(config = EditorPerProjectUserSettings)
class INFECTIONEDITOR_API UEclipseGrowthSettings : public UObject
{
	GENERATED_BODY()

public:
	UEclipseGrowthSettings();

	// The output folder as a long package path, e.g. /Game/Eclipse.
	FString OutputPackagePath() const;

	// --- Spread -------------------------------------------------------------

	// Same seed, same settings, same mesh: same growth, every time.
	UPROPERTY(EditAnywhere, Config, Category = "Spread")
	int32 Seed = 1;

	// Share of the surface overtaken.
	UPROPERTY(EditAnywhere, Config, Category = "Spread", meta = (ClampMin = "0", ClampMax = "1"))
	float Coverage = 0.6f;

	// Above 0 the corruption climbs more easily; below 0 it pools downward.
	UPROPERTY(EditAnywhere, Config, Category = "Spread", meta = (ClampMin = "-1", ClampMax = "1"))
	float Gravity = 0.0f;

	// How fingered and uneven the advancing front is.
	UPROPERTY(EditAnywhere, Config, Category = "Spread", meta = (ClampMin = "0", ClampMax = "3"))
	float FrontIrregularity = 1.3f;

	// Preference for creases and hollows over ridges.
	UPROPERTY(EditAnywhere, Config, Category = "Spread", meta = (ClampMin = "0", ClampMax = "2"))
	float CreaseBias = 0.6f;

	// --- Veins --------------------------------------------------------------

	// How far veins run ahead of the front, as a share of its reach.
	UPROPERTY(EditAnywhere, Config, Category = "Veins", meta = (ClampMin = "0", ClampMax = "1"))
	float VeinReach = 0.35f;

	// Area, in square centimetres, that must drain through a path before it
	// becomes a vein. Lower makes more veins.
	UPROPERTY(EditAnywhere, Config, Category = "Veins", meta = (ClampMin = "1", ClampMax = "500"))
	float VeinThreshold = 40.0f;

	// --- Carapace -----------------------------------------------------------

	UPROPERTY(EditAnywhere, Config, Category = "Carapace", meta = (ClampMin = "1", ClampMax = "100", Units = "cm"))
	float PlateSize = 8.5f;

	// How far plates stretch along the direction of growth into sinew strands.
	UPROPERTY(EditAnywhere, Config, Category = "Carapace", meta = (ClampMin = "1", ClampMax = "6"))
	float PlateStretch = 3.5f;

	UPROPERTY(EditAnywhere, Config, Category = "Carapace", meta = (ClampMin = "0", ClampMax = "10", Units = "cm"))
	float PlateThickness = 0.9f;

	// How many of the seams between plates leak light.
	UPROPERTY(EditAnywhere, Config, Category = "Carapace", meta = (ClampMin = "0", ClampMax = "1"))
	float SeamGlow = 0.08f;

	// --- Growths ------------------------------------------------------------

	UPROPERTY(EditAnywhere, Config, Category = "Growths")
	bool bGrowths = true;

	// Multiplies the size and spacing of every growth: 1 suits a human, about 2 a room.
	UPROPERTY(EditAnywhere, Config, Category = "Growths", meta = (ClampMin = "0.1", ClampMax = "10", EditCondition = "bGrowths"))
	float FeatureScale = 1.0f;

	// How much the tissue swells up around the base of each growth, so it
	// grows out of the surface instead of sitting on it. 0 turns it off.
	UPROPERTY(EditAnywhere, Config, Category = "Growths", meta = (ClampMin = "0", ClampMax = "3", EditCondition = "bGrowths"))
	float Fusion = 1.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Growths", meta = (EditCondition = "bGrowths"))
	bool bVeinTubes = true;

	// Ropes laid over the surface that run in toward the cores, spiralling around what they climb.
	UPROPERTY(EditAnywhere, Config, Category = "Growths", meta = (EditCondition = "bGrowths"))
	bool bRoots = true;

	UPROPERTY(EditAnywhere, Config, Category = "Growths", meta = (ClampMin = "0", ClampMax = "1", EditCondition = "bGrowths && bRoots"))
	float RootDensity = 0.7f;

	UPROPERTY(EditAnywhere, Config, Category = "Growths", meta = (ClampMin = "1", ClampMax = "300", Units = "cm", EditCondition = "bGrowths && bRoots"))
	float RootLength = 30.0f;

	// A glowing orb gripped by claws at each seed.
	UPROPERTY(EditAnywhere, Config, Category = "Growths", meta = (EditCondition = "bGrowths"))
	bool bHearts = true;

	UPROPERTY(EditAnywhere, Config, Category = "Growths", meta = (ClampMin = "1", ClampMax = "200", Units = "cm", EditCondition = "bGrowths && bHearts"))
	float HeartSize = 7.0f;

	// Above this coverage a heart opens into a maw: a crater of swollen lips
	// around a throat that glows from deep inside, ringed with teeth.
	UPROPERTY(EditAnywhere, Config, Category = "Growths", meta = (ClampMin = "0", ClampMax = "1.01", EditCondition = "bGrowths && bHearts"))
	float MawCoverage = 0.7f;

	// Clusters of swollen, translucent pods packed into folds and corners.
	UPROPERTY(EditAnywhere, Config, Category = "Growths", meta = (EditCondition = "bGrowths"))
	bool bSacs = true;

	UPROPERTY(EditAnywhere, Config, Category = "Growths", meta = (ClampMin = "0", ClampMax = "1", EditCondition = "bGrowths && bSacs"))
	float SacChance = 0.5f;

	// Segmented ridges along the thickest vein trunks.
	UPROPERTY(EditAnywhere, Config, Category = "Growths", meta = (EditCondition = "bGrowths"))
	bool bVertebrae = true;

	// A typical horn; a rare few grow several times longer.
	UPROPERTY(EditAnywhere, Config, Category = "Growths", meta = (ClampMin = "0", ClampMax = "50", Units = "cm", EditCondition = "bGrowths"))
	float ThornLength = 7.5f;

	UPROPERTY(EditAnywhere, Config, Category = "Growths", meta = (ClampMin = "0.5", ClampMax = "100", Units = "cm", EditCondition = "bGrowths"))
	float ThornSpacing = 6.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Growths", meta = (ClampMin = "0", ClampMax = "1", EditCondition = "bGrowths"))
	float EyeChance = 0.7f;

	UPROPERTY(EditAnywhere, Config, Category = "Growths", meta = (ClampMin = "0", ClampMax = "100", Units = "cm", EditCondition = "bGrowths"))
	float StrandLength = 12.0f;

	// Widest gap a bridge or membrane may span.
	UPROPERTY(EditAnywhere, Config, Category = "Growths", meta = (ClampMin = "1", ClampMax = "300", Units = "cm", EditCondition = "bGrowths"))
	float BridgeMaxGap = 30.0f;

	UPROPERTY(EditAnywhere, Config, Category = "Growths", meta = (ClampMin = "0", ClampMax = "1", EditCondition = "bGrowths"))
	float MembraneChance = 0.45f;

	// Share of each membrane torn away.
	UPROPERTY(EditAnywhere, Config, Category = "Growths", meta = (ClampMin = "0", ClampMax = "0.9", EditCondition = "bGrowths"))
	float MembraneHoles = 0.35f;

	// --- Mesh ---------------------------------------------------------------

	// The host is subdivided until no edge is longer than this. Corruption
	// detail lives on vertices, so smaller is finer but heavier.
	UPROPERTY(EditAnywhere, Config, Category = "Mesh", meta = (ClampMin = "0.2", ClampMax = "20", Units = "cm"))
	float Resolution = 1.0f;

	// Stop instead of generating more vertices than this.
	UPROPERTY(EditAnywhere, Config, Category = "Mesh", AdvancedDisplay, meta = (ClampMin = "10000"))
	int32 MaxVertices = 3000000;

	// --- Output -------------------------------------------------------------

	UPROPERTY(EditAnywhere, Config, Category = "Output")
	EEclipseOutputMode OutputMode = EEclipseOutputMode::Overlay;

	// Where the generated meshes and the Eclipse materials are saved.
	UPROPERTY(EditAnywhere, Config, Category = "Output", meta = (ContentDir))
	FDirectoryPath OutputFolder;

	UPROPERTY(EditAnywhere, Config, Category = "Output")
	bool bEnableNanite = true;
};
