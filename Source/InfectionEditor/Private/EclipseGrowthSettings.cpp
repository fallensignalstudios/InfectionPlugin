#include "EclipseGrowthSettings.h"

#include "eclipse/presets.h"

UEclipseGrowthSettings::UEclipseGrowthSettings()
{
	OutputFolder.Path = TEXT("/Game/Eclipse");
}

FString UEclipseGrowthSettings::OutputPackagePath() const
{
	FString Path = OutputFolder.Path;
	Path.TrimStartAndEndInline();
	Path.ReplaceInline(TEXT("\\"), TEXT("/"));
	while (Path.EndsWith(TEXT("/")))
	{
		Path.LeftChopInline(1);
	}
	if (Path.IsEmpty())
	{
		return TEXT("/Game/Eclipse");
	}
	// The folder picker can hand back a path relative to Content.
	if (!Path.StartsWith(TEXT("/")))
	{
		Path = TEXT("/Game/") + Path;
	}
	return Path;
}

void UEclipseGrowthSettings::ApplyPreset()
{
	eclipse::GrowthParams P;
	eclipse::FeatureParams F;
	eclipse::applyPreset(static_cast<eclipse::Preset>(Preset), P, F);

	// Lengths are centimetres here, metres in the generator.
	FrontIrregularity = P.noiseStrength;
	CreaseBias = P.cavityBias;
	VeinReach = P.veinReach;
	VeinThreshold = P.veinMinFlow * 1e4f;
	Bruising = P.bruising;
	HaloReach = P.haloReach;
	PlateStretch = P.plateStretch;
	PlateThickness = P.plateThickness * 100.0f;
	SeamGlow = P.seamGlow;

	Fusion = F.fusion;
	Roughness = F.roughness;
	bVeinTubes = F.veinTubes;
	bRoots = F.tendrils;
	RootDensity = F.tendrilDensity;
	RootLength = F.tendrilLength * 100.0f;
	RootBranching = F.tendrilBranching;
	bHearts = F.hearts;
	HeartSize = F.heartRadius * 100.0f;
	MawCoverage = F.mawCoverage;
	bSacs = F.sacs;
	SacChance = F.sacChance;
	bVertebrae = F.vertebrae;
	ThornLength = F.spikeLength * 100.0f;
	HornBarbs = F.spikeBarbs;
	ThornSpacing = F.spikeSpacing * 100.0f;
	EyeChance = F.noduleChance;
	StrandLength = F.strandLength * 100.0f;
	BridgeMaxGap = F.bridgeMaxGap * 100.0f;
	MembraneChance = F.membraneChance;
	MembraneHoles = F.membraneHoles;
	bSlime = F.slime;
	SlimeChance = F.slimeChance;
	bSpires = F.spires;
	SpireHeight = F.spireHeight * 100.0f;
	bRibcages = F.ribcages;
	RibcageLength = F.ribcageLength * 100.0f;
	bPods = F.pods;
	PodSize = F.podSize * 100.0f;
	bShelves = F.shelves;
	ShelfSize = F.shelfSize * 100.0f;
}

#if WITH_EDITOR
void UEclipseGrowthSettings::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
	Super::PostEditChangeProperty(Event);
	if (Event.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UEclipseGrowthSettings, Preset))
	{
		ApplyPreset();
	}
}
#endif

static_assert(static_cast<int>(EEclipsePreset::Cathedral) == static_cast<int>(eclipse::Preset::Cathedral),
	"EEclipsePreset must list the presets in eclipse::Preset's order");
