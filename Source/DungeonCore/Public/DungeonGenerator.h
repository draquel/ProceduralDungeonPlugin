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
	 * @param Seed    Random seed. A non-zero value is always used as-is. 0 = no seed supplied: the
	 *                config's FixedSeed is used when bUseFixedSeed is on, otherwise a clock-derived seed
	 *                (see UDungeonConfiguration::ResolveSeed). FDungeonResult::Seed records the seed used.
	 * @return Complete dungeon result (grid, rooms, hallways, graph data).
	 */
	UFUNCTION(BlueprintCallable, Category="Dungeon|Generation")
	FDungeonResult Generate(UDungeonConfiguration* Config, int64 Seed);

	/**
	 * Generate with an entrance spec that REPLACES Config->Entrance. For callers that decide the
	 * approach at runtime (the POI subsystem derives it from the POI type's entrance style), so one
	 * dungeon config can serve several entrance styles without duplicating assets.
	 * @param Config           Generation parameters.
	 * @param Seed             As for Generate.
	 * @param EntranceOverride The entrance approach to honour (Approach None = legacy selection).
	 * @return Complete dungeon result; EntranceApproach records the outcome.
	 */
	UFUNCTION(BlueprintCallable, Category="Dungeon|Generation")
	FDungeonResult GenerateWithEntrance(UDungeonConfiguration* Config, int64 Seed, const FDungeonEntranceSpec& EntranceOverride);

private:
	FDungeonResult GenerateInternal(UDungeonConfiguration* Config, int64 Seed, const FDungeonEntranceSpec& EntranceSpec);

public:

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
