// The engine-independent generator. These are the same sources the CMake
// project builds for the command-line tool and the tests. The API header only
// includes Unreal's platform definitions when UBT supplies the module exports.

using UnrealBuildTool;

public class EclipseCore : ModuleRules
{
	public EclipseCore(ReadOnlyTargetRules Target) : base(Target)
	{
		// Plain standard C++: keep Unreal's shared precompiled header and its
		// macros out of these files, and compile each file on its own.
		PCHUsage = PCHUsageMode.NoPCHs;
		bUseUnity = false;
		bEnableExceptions = true;

		PublicDependencyModuleNames.Add("Core");
	}
}
