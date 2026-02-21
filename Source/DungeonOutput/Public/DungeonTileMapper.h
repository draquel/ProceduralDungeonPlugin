#pragma once

#include "CoreMinimal.h"

class UDungeonTileSet;
struct FDungeonResult;
struct FDungeonGrid;
struct FDungeonCell;

/** Identifies each type of tile geometry placed in the dungeon. */
enum class EDungeonTileType : uint8
{
	RoomFloor,
	HallwayFloor,
	RoomCeiling,
	HallwayCeiling,
	WallSegment,
	DoorFrame,
	EntranceFrame,
	StaircaseMesh,
	// Hallway floor connectivity variants
	HallwayFloorStraight,   // 2 opposite neighbors
	HallwayFloorCorner,     // 2 adjacent neighbors
	HallwayFloorTJunction,  // 3 neighbors
	HallwayFloorCrossroad,  // 4 neighbors
	HallwayFloorEndCap,     // 1 neighbor
	// Hallway ceiling connectivity variants
	HallwayCeilingStraight,
	HallwayCeilingCorner,
	HallwayCeilingTJunction,
	HallwayCeilingCrossroad,
	HallwayCeilingEndCap,
	COUNT
};

/**
 * Result of mapping a dungeon grid to tile instance transforms.
 * Indexed by EDungeonTileType — each slot holds transforms for one HISMC.
 */
struct DUNGEONOUTPUT_API FDungeonTileMapResult
{
	static constexpr int32 TypeCount = static_cast<int32>(EDungeonTileType::COUNT);

	TArray<FTransform> Transforms[TypeCount];

	int32 GetTotalInstanceCount() const;
	void Reset();
};

/**
 * Pure-function static utility that converts FDungeonResult grid data
 * into per-tile-type arrays of FTransform for HISMC placement.
 * No UObjects created, no side effects — fully testable.
 */
struct DUNGEONOUTPUT_API FDungeonTileMapper
{
	/**
	 * Map a dungeon result to tile instance transforms.
	 * @param Result      The generated dungeon grid data.
	 * @param TileSet     Mesh mapping (used to determine which slots are active).
	 * @param WorldOffset World-space offset applied to all transforms (typically actor location).
	 * @return Per-tile-type arrays of instance transforms.
	 */
	static FDungeonTileMapResult MapToTiles(
		const FDungeonResult& Result,
		const UDungeonTileSet& TileSet,
		const FVector& WorldOffset);

private:
	/**
	 * Returns true if a wall is needed on the current cell's face toward the horizontal neighbor.
	 * Walls are placed when the neighbor is solid, OOB, or belongs to a different logical space
	 * (different room, different hallway, room↔hallway boundary).
	 * Returns false for Door/Entrance neighbors — those cells handle their own frames.
	 */
	static bool NeedsWall(const FDungeonGrid& Grid, const FDungeonCell& Current, int32 NX, int32 NY, int32 NZ);

	/**
	 * Returns true if a floor/ceiling boundary is needed between the current cell and a vertical neighbor.
	 * A boundary is needed when the neighbor is solid, OOB, or belongs to a different logical space
	 * (different room, different hallway, or different space type).
	 */
	static bool NeedsVerticalBoundary(const FDungeonGrid& Grid, const FDungeonCell& Current, int32 NX, int32 NY, int32 NZ);
};
