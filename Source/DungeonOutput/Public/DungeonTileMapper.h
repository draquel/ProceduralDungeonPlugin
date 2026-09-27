#pragma once

#include "CoreMinimal.h"
#include "DungeonTileMapper.generated.h"

class UDungeonTileSet;
struct FDungeonResult;
struct FDungeonGrid;
struct FDungeonCell;

/** Identifies each type of tile geometry placed in the dungeon. */
UENUM(BlueprintType)
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
	/**
	 * Wall on a face shared by TWO OPEN cells (room beside corridor, landing beside a ramp flank),
	 * placed once, by the owning side (FDungeonBoundaryRules::OwnsSharedFace), and seen from both
	 * sides: author it two-faced and symmetric about the face plane. Rock-backed faces keep
	 * WallSegment (which may extrude outward into the rock). Falls back to WallSegment when unset.
	 */
	WallPartition,
	/**
	 * Post where two walled faces of one open cell meet (a room or corridor corner). Anchor: the
	 * corner point on the cell floor pulled onto BOTH wall faces (in by WallProfile.FaceInset from
	 * a rock-backed face, by half PartitionThickness from a partition), local +X along the diagonal
	 * INTO the cell. Author the post centred on the anchor: it then straddles the face, half of it
	 * proud. Placed once per corner point. Modules scale uniformly by cell / ReferenceCellSize; a
	 * single mesh scales uniformly by cell / WallProfile.ReferenceCellSize (no per-axis fit). Optional.
	 */
	WallCornerInner,
	/**
	 * Post where a wall run ENDS at an opening that carries no frame: the mouth of a side corridor,
	 * a room wall stopping at an open face. Anchor: the corner point pulled onto the ending wall's
	 * face (same depths as WallCornerInner), +X along the wall's direction of travel into the open
	 * face. Never placed beside a door / entrance frame (the frame's jambs cover that corner). Optional.
	 */
	WallCornerOuter,
	COUNT UMETA(Hidden)
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
	 * @param bOpenEntranceCeiling Leave the entrance OPENING unbuilt so a stitched passage can
	 *        enter the entrance room. Which tile is skipped follows Result.EntranceApproach:
	 *        the lid of the opening cell (FromAbove, and legacy results with no approach), its
	 *        floor (FromBelow), or its wall on the approach face (FromSide). Off by default: a
	 *        standalone dungeon stays sealed.
	 * @return Per-tile-type arrays of instance transforms.
	 */
	static FDungeonTileMapResult MapToTiles(
		const FDungeonResult& Result,
		const UDungeonTileSet& TileSet,
		const FVector& WorldOffset,
		bool bOpenEntranceCeiling = false);

private:
	// Boundary decisions (wall / floor / ceiling between two cells) are NOT implemented here.
	// They live in FDungeonBoundaryRules (DungeonCore) and are shared with the voxel stamper;
	// re-implementing them per backend is how doorways ended up walled shut.
};
