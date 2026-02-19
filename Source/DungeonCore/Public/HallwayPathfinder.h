#pragma once

#include "CoreMinimal.h"

struct FDungeonGrid;
struct FDungeonStaircase;
class UDungeonConfiguration;

/**
 * FHallwayPathfinder
 * Modified A* that carves hallways between rooms on the dungeon grid.
 * Supports staircase moves for multi-floor pathfinding.
 */
struct DUNGEONCORE_API FHallwayPathfinder
{
	/**
	 * Find a path between two grid cells using A*.
	 * Supports same-floor cardinal moves and cross-floor staircase moves.
	 * @param Grid            The dungeon grid (read for costs, not modified).
	 * @param Start           Starting cell (room A center).
	 * @param End             Ending cell (room B center).
	 * @param Config          Generation config (cost multipliers, staircase params).
	 * @param SourceRoomIdx   RoomIndex (1-based) of the source room — free to traverse.
	 * @param DestRoomIdx     RoomIndex (1-based) of the destination room — free to traverse.
	 * @param OutPath         Ordered cells from Start to End. Staircase transitions appear as
	 *                        non-adjacent cells with a Z-coordinate change.
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
	 * Carve a found path into the grid. Marks non-room cells as Hallway,
	 * transition cells as Door, and staircase cells as Staircase/StaircaseHead.
	 * @param OutStaircases   Populated with staircase data for any floor transitions in the path.
	 */
	static void CarveHallway(
		FDungeonGrid& Grid,
		const TArray<FIntVector>& Path,
		uint8 HallwayIndex,
		uint8 SourceRoomIdx,
		uint8 DestRoomIdx,
		const UDungeonConfiguration& Config,
		TArray<FDungeonStaircase>& OutStaircases);

private:
	/** Check if all cells needed for a staircase are available (Empty or Hallway). */
	static bool CanBuildStaircase(
		const FDungeonGrid& Grid,
		const FIntVector& Entry,
		int32 DirX, int32 DirY,
		int32 Rise,
		int32 RiseToRun,
		int32 HeadroomCells,
		FIntVector& OutExit);
};
