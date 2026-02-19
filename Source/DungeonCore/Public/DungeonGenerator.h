#pragma once

#include "CoreMinimal.h"
#include "DungeonTypes.h"
#include "DungeonGenerator.generated.h"

class UDungeonConfiguration;

/**
 * UDungeonGenerator
 * Main generation orchestrator. Runs the full pipeline and produces FDungeonResult.
 */
UCLASS(BlueprintType, Blueprintable)
class DUNGEONCORE_API UDungeonGenerator : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Generate a dungeon from the given configuration and seed.
	 * @param Config  Generation parameters (grid size, room count, etc.).
	 * @param Seed    Random seed. 0 = use current time.
	 * @return Complete dungeon result (grid, rooms, hallways, graph data).
	 */
	UFUNCTION(BlueprintCallable, Category="Dungeon|Generation")
	FDungeonResult Generate(UDungeonConfiguration* Config, int64 Seed);
};
