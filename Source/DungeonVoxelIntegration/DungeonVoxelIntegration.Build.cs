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
		});

		// VoxelWorlds is reached from Private/ only (no public header of this module includes a voxel header). The automation
		// tests stream a small voxel world (VoxelTesting::FTestChunkWorld: the distance-band LOD strategy and a recording mesh
		// renderer).
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"VoxelCore",
			"VoxelGeneration",
			"VoxelStreaming",
			"VoxelLOD",
			"VoxelRendering",
		});
	}
}
