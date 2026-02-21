using UnrealBuildTool;

public class DungeonVoxelIntegration : ModuleRules
{
	public DungeonVoxelIntegration(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DungeonCore",
			"VoxelCore",
			"VoxelGeneration",
			"VoxelStreaming",
		});
	}
}
