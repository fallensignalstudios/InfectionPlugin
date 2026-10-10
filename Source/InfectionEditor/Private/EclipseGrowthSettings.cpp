#include "EclipseGrowthSettings.h"

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
