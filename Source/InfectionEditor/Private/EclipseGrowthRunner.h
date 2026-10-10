#pragma once

#include "CoreMinimal.h"

class UEclipseGrowthSettings;

// The panel's actions. Each returns a line for the panel's status bar.
namespace EclipseGrowthRunner
{
	// Grows corruption over every selected static or skeletal mesh actor,
	// from the enabled Eclipse Seeds near it. Static hosts get static mesh
	// actors attached to them; skeletal hosts get Eclipse Follower
	// components that play the host's animation.
	FText GrowOnSelection(const UEclipseGrowthSettings& Settings);

	// Drops a seed on top of each selected mesh, to start from.
	FText AddSeedOnSelection();

	// Regenerates the master material and its instances from code.
	FText RebuildMaterials(const UEclipseGrowthSettings& Settings);
}
