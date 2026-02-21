#pragma once

#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogDungeonVoxelIntegration, Log, All);

class FDungeonVoxelIntegrationModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TUniquePtr<FAutoConsoleCommand> TestStampCommand;
};
