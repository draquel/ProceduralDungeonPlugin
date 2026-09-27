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

/** What an opening in a wall plane is: decides whether a door leaf may hang in it (E3). */
UENUM(BlueprintType)
enum class EDungeonOpeningKind : uint8
{
	/** A Door cell's framed face toward another open cell. The only kind that takes a leaf. */
	Doorway,
	/** A ramp entering a room cell directly (framed like a doorway); a leaf would block the ramp. */
	StairEntry,
	/** An Entrance cell's framed face; a leaf would block the passage into the dungeon. */
	EntranceOpening,
};

/**
 * One framed opening in a wall plane, emitted by the mapper alongside the frame instance (or where
 * the frame WOULD go when the slot is unset). Gameplay (VoxelWorldPOI's door actors) hangs leaves
 * from these; the tiles stay visual-only.
 */
USTRUCT(BlueprintType)
struct DUNGEONOUTPUT_API FDungeonOpening
{
	GENERATED_BODY()

	/** The cell whose face carries the frame (the Door / Entrance cell, or the room cell a ramp enters). */
	UPROPERTY(BlueprintReadOnly, Category = "Dungeon")
	FIntVector Cell = FIntVector::ZeroValue;

	/** The face, as a unit step from Cell to the neighbour across the opening. */
	UPROPERTY(BlueprintReadOnly, Category = "Dungeon")
	int32 FaceDX = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Dungeon")
	int32 FaceDY = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Dungeon")
	EDungeonOpeningKind Kind = EDungeonOpeningKind::Doorway;

	/**
	 * World transform of the opening: origin at the face centre on the CELL FLOOR (the plane the
	 * frame is placed against), local +X across the face into the neighbour, uniform scale =
	 * cell / WallProfile.ReferenceCellSize.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Dungeon")
	FTransform Frame;

	/**
	 * Where a door leaf hangs: origin on the hinge line at floor level, on the finished-face plane
	 * (Frame moved in by WallProfile.FaceInset, out by HingeInset), local +X across the face into
	 * the neighbour and local +Y along the wall toward the opening centre. The leaf spans
	 * [0, LeafWidth] along +Y when closed; swinging it about local Z toward -X lays it into this
	 * cell. Scale as Frame.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Dungeon")
	FTransform LeafHinge;

	/** Door leaf size in world units (profile DoorLeafWidth / Height scaled to the cell). */
	UPROPERTY(BlueprintReadOnly, Category = "Dungeon")
	float LeafWidth = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Dungeon")
	float LeafHeight = 0.0f;
};

/** What a wall fixture is (E3: wall lights; braziers and decor ride other paths). */
UENUM(BlueprintType)
enum class EDungeonFixtureKind : uint8
{
	WallLight,
};

/**
 * A fixture mounted on a finished wall face, emitted by the mapper from the tileset's
 * FDungeonFixtureRules. Gameplay (torch actors) is spawned from these; nothing is instanced.
 */
USTRUCT(BlueprintType)
struct DUNGEONOUTPUT_API FDungeonFixture
{
	GENERATED_BODY()

	/** The open cell whose wall face carries the fixture. */
	UPROPERTY(BlueprintReadOnly, Category = "Dungeon")
	FIntVector Cell = FIntVector::ZeroValue;

	/** The walled face, as a unit step from Cell toward the rock behind it. */
	UPROPERTY(BlueprintReadOnly, Category = "Dungeon")
	int32 FaceDX = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Dungeon")
	int32 FaceDY = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Dungeon")
	EDungeonFixtureKind Kind = EDungeonFixtureKind::WallLight;

	/**
	 * World mount transform: origin ON the finished wall face (inset from the cell plane by the
	 * profile) at the rule's mount height, local +X pointing off the wall INTO the cell, uniform
	 * scale = cell / WallProfile.ReferenceCellSize.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Dungeon")
	FTransform Anchor;
};

/**
 * Result of mapping a dungeon grid to tile instance transforms.
 * Indexed by EDungeonTileType — each slot holds transforms for one HISMC.
 */
struct DUNGEONOUTPUT_API FDungeonTileMapResult
{
	static constexpr int32 TypeCount = static_cast<int32>(EDungeonTileType::COUNT);

	TArray<FTransform> Transforms[TypeCount];

	/** Framed openings (doorways, ramp entries, entrance openings), for door leaves. */
	TArray<FDungeonOpening> Openings;

	/** Wall-mounted fixtures (wall lights), placed per the tileset's FDungeonFixtureRules. */
	TArray<FDungeonFixture> Fixtures;

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

	/**
	 * Deterministic identity of an interactable placed on a cell face — the key of the per-dungeon
	 * state record gameplay keeps across stream-out / stream-in (and, later, saves). Depends only
	 * on the grid position, the face and the kind, never on spawn order or actor names.
	 * @param Cell   The grid cell (opening or fixture cell).
	 * @param FaceDX Face step from the cell (-1, 0, +1).
	 * @param FaceDY Face step from the cell (-1, 0, +1).
	 * @param Kind   A small kind discriminator: InteractableKindDoor / InteractableKindFixture plus
	 *               the EDungeonOpeningKind / EDungeonFixtureKind value, so a door and a fixture on
	 *               one face never collide.
	 * @return A 32-bit id, stable across runs and machines.
	 */
	static uint32 MakeInteractableId(const FIntVector& Cell, int32 FaceDX, int32 FaceDY, uint8 Kind);

	/** Kind namespaces for MakeInteractableId. */
	static constexpr uint8 InteractableKindDoor = 0x10;
	static constexpr uint8 InteractableKindFixture = 0x20;

private:
	// Boundary decisions (wall / floor / ceiling between two cells) are NOT implemented here.
	// They live in FDungeonBoundaryRules (DungeonCore) and are shared with the voxel stamper;
	// re-implementing them per backend is how doorways ended up walled shut.
};
