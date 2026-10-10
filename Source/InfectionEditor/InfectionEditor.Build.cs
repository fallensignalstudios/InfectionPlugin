using UnrealBuildTool;

public class InfectionEditor : ModuleRules
{
	public InfectionEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"EclipseCore",
			"InfectionRuntime",
			"AssetRegistry",
			"InputCore",
			"MaterialEditor",
			"MeshDescription",
			"PropertyEditor",
			"Slate",
			"SlateCore",
			"StaticMeshDescription",
			"ToolMenus",
			"UnrealEd",
		});
	}
}
