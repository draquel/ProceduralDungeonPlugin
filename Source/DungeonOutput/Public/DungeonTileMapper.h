#pragma once

#include "CoreMinimal.h"

class UDungeonTileSet;
struct FDungeonResult;

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
	/** Returns true if the given cell coordinate is solid (OOB, Empty, or RoomWall). */
	static bool IsSolid(const FDungeonResult& Result, int32 X, int32 Y, int32 Z);
};
