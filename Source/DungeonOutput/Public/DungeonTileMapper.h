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
	 * Tile slab thickness as a fraction of the cell size: floors/ceilings/walls are scaled to
	 * CellWorldSize * this along their thin axis. The SINGLE source of truth for tile metrics —
	 * consumers placing content against tile geometry (walkable floor top = cell bottom +
	 * TileThickness, wall inner face = cell edge - TileThickness) derive from here rather than
	 * assuming a constant.
	 */
	static constexpr float TileThicknessFraction = 0.2f;

	/** Tile slab thickness (world units) for a given cell size. */
	static float TileThickness(float CellWorldSize) { return CellWorldSize * TileThicknessFraction; }

	/**
	 * Map a dungeon result to tile instance transforms.
	 * @param Result      The generated dungeon grid data.
	 * @param TileSet     Mesh mapping (used to determine which slots are active).
	 * @param WorldOffset World-space offset applied to all transforms (typically actor location).
	 * @param bOpenEntranceCeiling Skip the ceiling tile of the DESIGNATED entrance cell
	 *        (Result.EntranceCell) so a vertical passage stitched from above (shaft/trapdoor)
	 *        can drop into the entrance room. Off by default: a standalone dungeon keeps its
	 *        ceiling closed.
	 * @return Per-tile-type arrays of instance transforms.
	 */
	static FDungeonTileMapResult MapToTiles(
		const FDungeonResult& Result,
		const UDungeonTileSet& TileSet,
		const FVector& WorldOffset,
		bool bOpenEntranceCeiling = false);

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
