using UnrealBuildTool;

public class InfectionRuntime : ModuleRules
{
	public InfectionRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
		PrivateDependencyModuleNames.Add("NetCore");  // replication of the spread component
	}
}
