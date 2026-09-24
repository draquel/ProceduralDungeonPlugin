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
			"Slate",
			"SlateCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"PropertyEditor",
			"EditorStyle",
			"InputCore",
			"ToolMenus",
			"AssetRegistry",
			"ContentBrowser",
			"DungeonCore",
			"DungeonOutput",
		});
	}
}
