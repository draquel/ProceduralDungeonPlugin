#pragma once

#include "CoreMinimal.h"

struct FDungeonGrid;
struct FDungeonRoom;
struct FDungeonSeed;
class UDungeonConfiguration;

/**
 * FRoomPlacement
 * Places rooms randomly on the grid with non-overlap and buffer constraints.
 */
struct DUNGEONCORE_API FRoomPlacement
{
	/**
	 * Place rooms into the grid.
	 * @return true if at least 2 rooms were placed (minimum for a dungeon).
	 */
	static bool PlaceRooms(
		FDungeonGrid& Grid,
		const UDungeonConfiguration& Config,
		FDungeonSeed& Seed,
		TArray<FDungeonRoom>& OutRooms);

private:
	static bool DoesRoomOverlap(
		const FIntVector& Position,
		const FIntVector& Size,
		const TArray<FDungeonRoom>& ExistingRooms,
		int32 Buffer);

	static void StampRoomToGrid(
		FDungeonGrid& Grid,
		const FDungeonRoom& Room);
};
