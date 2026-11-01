#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/** DungeonCore's log category: generation, room placement and semantics, the hallway pathfinder. */
DECLARE_LOG_CATEGORY_EXTERN(LogDungeonCore, Log, All);

class FDungeonCoreModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
