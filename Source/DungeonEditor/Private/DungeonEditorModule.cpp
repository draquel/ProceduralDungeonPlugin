#include "DungeonEditorModule.h"
#include "DungeonActorDetails.h"
#include "PropertyEditorModule.h"

#define LOCTEXT_NAMESPACE "FDungeonEditorModule"

DEFINE_LOG_CATEGORY(LogDungeonEditor);

void FDungeonEditorModule::StartupModule()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	PropertyModule.RegisterCustomClassLayout(
		"DungeonActor",
		FOnGetDetailCustomizationInstance::CreateStatic(&FDungeonActorDetails::MakeInstance));

	UE_LOG(LogDungeonEditor, Log, TEXT("DungeonEditor module started — registered detail customization for ADungeonActor"));
}

void FDungeonEditorModule::ShutdownModule()
{
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomClassLayout("DungeonActor");
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDungeonEditorModule, DungeonEditor)
