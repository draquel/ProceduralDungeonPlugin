#include "DungeonCoreModule.h"

DEFINE_LOG_CATEGORY(LogDungeonCore);

#define LOCTEXT_NAMESPACE "FDungeonCoreModule"

void FDungeonCoreModule::StartupModule()
{
}

void FDungeonCoreModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDungeonCoreModule, DungeonCore)
