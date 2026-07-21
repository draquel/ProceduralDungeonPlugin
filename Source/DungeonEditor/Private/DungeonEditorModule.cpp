#include "DungeonEditorModule.h"
#include "DungeonActorDetails.h"
#include "DungeonModuleTools.h"
#include "PropertyEditorModule.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "FDungeonEditorModule"

DEFINE_LOG_CATEGORY(LogDungeonEditor);

void FDungeonEditorModule::StartupModule()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	PropertyModule.RegisterCustomClassLayout(
		"DungeonActor",
		FOnGetDetailCustomizationInstance::CreateStatic(&FDungeonActorDetails::MakeInstance));

	// Menus may not exist yet at module load — defer to the ToolMenus startup callback.
	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FDungeonEditorModule::RegisterMenus));

	UE_LOG(LogDungeonEditor, Log, TEXT("DungeonEditor module started — detail customization + module authoring menu"));
}

void FDungeonEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomClassLayout("DungeonActor");
	}
}

void FDungeonEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	// Right-click on selected level actors → Dungeon section → Create Dungeon Module from Selection.
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.ActorContextMenu");
	if (!Menu)
	{
		return;
	}

	FToolMenuSection& Section = Menu->FindOrAddSection("Dungeon", LOCTEXT("DungeonSection", "Dungeon"));
	Section.AddMenuEntry(
		"CreateDungeonModuleFromSelection",
		LOCTEXT("CreateModuleLabel", "Create Dungeon Module from Selection"),
		LOCTEXT("CreateModuleTooltip",
			"Capture the selected static-mesh actors (arranged around the world origin = the tile anchor) "
			"into a new Dungeon Tile Module asset. Assign it to a tileset slot via TileModules."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			UDungeonModuleTools::CreateModuleFromSelection(400.0f);
		})));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDungeonEditorModule, DungeonEditor)
