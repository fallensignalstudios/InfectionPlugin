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
			"AnimationCore",
			"AssetRegistry",
			"InputCore",
			"MaterialEditor",
			"MeshDescription",
			"PhysicsCore",
			"PropertyEditor",
			"RHI",
			"RenderCore",
			"SkeletalMeshDescription",
			"SkeletalMeshUtilitiesCommon",
			"Slate",
			"SlateCore",
			"StaticMeshDescription",
			"ToolMenus",
			"UnrealEd",
		});
	}
}
