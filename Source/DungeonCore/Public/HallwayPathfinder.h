#pragma once

#include "CoreMinimal.h"

struct FDungeonGrid;
class UDungeonConfiguration;

/**
 * FHallwayPathfinder
 * Modified A* that carves hallways between rooms on the dungeon grid.
 * Phase 1: 2D only (XZ plane, single floor). Staircase support added in Phase 2.
 */
struct DUNGEONCORE_API FHallwayPathfinder
{
	/**
	 * Find a path between two grid cells using A*.
	 * @param Grid            The dungeon grid (read for costs, not modified).
	 * @param Start           Starting cell (room A center).
	 * @param End             Ending cell (room B center).
	 * @param Config          Generation config (cost multipliers).
	 * @param SourceRoomIdx   RoomIndex (1-based) of the source room — free to traverse.
	 * @param DestRoomIdx     RoomIndex (1-based) of the destination room — free to traverse.
	 * @param OutPath         Ordered cells from Start to End.
	 * @return true if a path was found.
	 */
	static bool FindPath(
		const FDungeonGrid& Grid,
		const FIntVector& Start,
		const FIntVector& End,
		const UDungeonConfiguration& Config,
		uint8 SourceRoomIdx,
		uint8 DestRoomIdx,
		TArray<FIntVector>& OutPath);

	/**
	 * Carve a found path into the grid. Marks non-room cells as Hallway, and
	 * transition cells (hallway adjacent to room) as Door.
	 */
	static void CarveHallway(
		FDungeonGrid& Grid,
		const TArray<FIntVector>& Path,
		uint8 HallwayIndex,
		uint8 SourceRoomIdx,
		uint8 DestRoomIdx);
};
