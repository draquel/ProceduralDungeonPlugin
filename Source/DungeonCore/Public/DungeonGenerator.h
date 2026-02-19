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

	/**
	 * Get world-space positions for all grid cells of a given type.
	 * Useful for debug visualization (spawn cubes/spheres at each position).
	 * @param Result    The generation result containing the grid.
	 * @param CellType  Which cell type to collect.
	 * @return Array of world-space center positions for matching cells.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="Dungeon|Debug", meta=(DisplayName="Get Cell Positions By Type"))
	static TArray<FVector> GetCellWorldPositionsByType(const FDungeonResult& Result, EDungeonCellType CellType);
};
