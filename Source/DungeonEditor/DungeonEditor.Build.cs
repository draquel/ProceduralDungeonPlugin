using UnrealBuildTool;

public class DungeonEditor : ModuleRules
{
	public DungeonEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"UnrealEd",
			"PropertyEditor",
			"ToolMenus",
			"AssetRegistry",
			"ContentBrowser",
			"DataValidation",
			"DungeonCore",
			"DungeonOutput",
		});
	}
}
