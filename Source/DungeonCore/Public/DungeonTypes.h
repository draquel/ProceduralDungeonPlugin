#pragma once

#include "CoreMinimal.h"
#include "DungeonTypes.generated.h"

// ============================================================================
// Enums
// ============================================================================

/** What occupies a single grid cell. */
UENUM(BlueprintType)
enum class EDungeonCellType : uint8
{
	Empty,
	Room,
	RoomWall,
	Hallway,
	Staircase,
	StaircaseHead,
	Door,
	Entrance,
	/**
	 * Generation-time keep-out: the entrance approach volume (shaft column / tunnel corridor)
	 * that rooms, hallways and staircases must not occupy. Solid for every rule that asks.
	 * Never present in a finished FDungeonResult — the generator clears it back to Empty
	 * before output (the validator reports any that leaks).
	 */
	Reserved,
};

/** Semantic meaning of a room. Affects placement rules and connectivity. */
UENUM(BlueprintType)
enum class EDungeonRoomType : uint8
{
	Generic,
	Entrance,
	Boss,
	Treasure,
	Spawn,
	Rest,
	Secret,
	Corridor,
	Stairwell,
	Custom,
};

/**
 * Legacy entrance selection: which of the ALREADY-PLACED rooms becomes the entrance. Used only
 * when FDungeonEntranceSpec::Approach is None; nothing is reserved around the chosen room.
 */
UENUM(BlueprintType)
enum class EDungeonEntrancePlacement : uint8
{
	/** Room on the RoomBuffer line of a grid edge (placement never reaches coordinate 0). */
	BoundaryEdge,
	/** Room reaches the top floor — the column above it is empty by construction. */
	TopFloor,
	/** Room on floor 0. */
	BottomFloor,
	/** No positional constraint. */
	Any,
};

/**
 * How the world reaches the entrance room. Drives entrance-first placement and the reserved
 * approach volume that the rest of the layout must keep clear.
 */
UENUM(BlueprintType)
enum class EDungeonEntranceApproach : uint8
{
	/** No external approach: legacy post-placement selection by EntrancePlacement, no reservation. */
	None,
	/** A vertical passage (shaft, trapdoor, cave mouth) drops onto the entrance room's lid. */
	FromAbove,
	/** A vertical passage rises into the entrance room's floor. */
	FromBelow,
	/** A horizontal tunnel enters through a grid-boundary face of the entrance room. */
	FromSide,
};

/** Which floor the entrance room is placed on. */
UENUM(BlueprintType)
enum class EDungeonEntranceFloor : uint8
{
	Any,
	/** Highest floor the room fits on. For FromAbove this leaves nothing to reserve. */
	Top,
	/** Floor 0. For FromBelow this leaves nothing to reserve. */
	Bottom,
	/** FDungeonEntranceSpec::ExplicitFloor. Unsatisfiable when the room cannot fit there. */
	Explicit,
};

/** A grid-boundary face (FromSide approaches). */
UENUM(BlueprintType)
enum class EDungeonGridFace : uint8
{
	Any,
	MinX,
	MaxX,
	MinY,
	MaxY,
};

/**
 * FDungeonEntranceSpec
 * Declares BEFORE generation how the entrance will be approached, so the generator can place the
 * entrance room to suit it and keep the approach volume free of rooms, hallways and staircases.
 * Approach None = legacy behaviour (entrance selected after placement, nothing reserved).
 */
USTRUCT(BlueprintType)
struct DUNGEONCORE_API FDungeonEntranceSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Entrance")
	EDungeonEntranceApproach Approach = EDungeonEntranceApproach::None;

	/** Floor preference for the entrance room. Top is the natural choice for FromAbove. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Entrance", meta=(EditCondition="Approach != EDungeonEntranceApproach::None"))
	EDungeonEntranceFloor Floor = EDungeonEntranceFloor::Any;

	/** Floor index used when Floor == Explicit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Entrance", meta=(ClampMin="0", EditCondition="Floor == EDungeonEntranceFloor::Explicit"))
	int32 ExplicitFloor = 0;

	/** FromSide only: which boundary face the tunnel enters through. Any = drawn from the seed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Entrance", meta=(EditCondition="Approach == EDungeonEntranceApproach::FromSide"))
	EDungeonGridFace Face = EDungeonGridFace::Any;

	/** Extra clear cells around the approach column / corridor (0 = the passage cell itself). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Entrance", meta=(ClampMin="0", ClampMax="2", EditCondition="Approach != EDungeonEntranceApproach::None"))
	int32 Clearance = 0;

	/**
	 * FromAbove / FromBelow: force the entrance room to a single floor so the opening is the room's
	 * own lid / floor and the passage does not have to cross the room's upper airspace.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Entrance", meta=(EditCondition="Approach == EDungeonEntranceApproach::FromAbove || Approach == EDungeonEntranceApproach::FromBelow"))
	bool bSingleFloorEntranceRoom = true;
};

/**
 * FDungeonEntranceApproachInfo
 * The resolved approach geometry recorded on FDungeonResult, so every backend (tile mapper,
 * voxel stitcher, POI subsystem) reads one description instead of re-deriving it.
 */
USTRUCT(BlueprintType)
struct DUNGEONCORE_API FDungeonEntranceApproachInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	EDungeonEntranceApproach Approach = EDungeonEntranceApproach::None;

	/** False when the requested approach could not be satisfied and the entrance was placed unconstrained. */
	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	bool bSatisfied = false;

	/**
	 * The cell whose lid (FromAbove), floor (FromBelow) or boundary wall (FromSide) is the opening.
	 * Same column as EntranceCell; equals it for single-floor rooms on vertical approaches.
	 */
	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	FIntVector OpeningCell = FIntVector::ZeroValue;

	/** FromSide: the resolved face (never Any once satisfied). */
	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	EDungeonGridFace Face = EDungeonGridFace::Any;

	/** Inclusive cell box guaranteed Empty in the final grid. Empty box when nothing needed reserving. */
	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	FIntVector KeepOutMin = FIntVector::ZeroValue;

	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	FIntVector KeepOutMax = FIntVector(-1, -1, -1);

	bool HasKeepOut() const
	{
		return KeepOutMax.X >= KeepOutMin.X && KeepOutMax.Y >= KeepOutMin.Y && KeepOutMax.Z >= KeepOutMin.Z;
	}
};

// ============================================================================
// Plain Structs (not USTRUCT — performance-critical dense storage)
// ============================================================================

/** Single grid cell. 8 bytes. */
struct DUNGEONCORE_API FDungeonCell
{
	EDungeonCellType CellType = EDungeonCellType::Empty;
	uint8 RoomIndex = 0;
	uint8 HallwayIndex = 0;
	uint8 FloorIndex = 0;
	uint8 MaterialHint = 0;
	uint8 StaircaseDirection = 0;
	uint8 Flags = 0;
	uint8 Reserved = 0;
};

static_assert(sizeof(FDungeonCell) == 8, "FDungeonCell must be exactly 8 bytes");

/** 3D grid holding all cell data. Indexed as [X + Y*SizeX + Z*SizeX*SizeY]. */
struct DUNGEONCORE_API FDungeonGrid
{
	FIntVector GridSize = FIntVector::ZeroValue;
	TArray<FDungeonCell> Cells;

	void Initialize(const FIntVector& InGridSize);

	FORCEINLINE int32 CellIndex(int32 X, int32 Y, int32 Z) const
	{
		return X + Y * GridSize.X + Z * GridSize.X * GridSize.Y;
	}

	FORCEINLINE int32 CellIndex(const FIntVector& Coord) const
	{
		return CellIndex(Coord.X, Coord.Y, Coord.Z);
	}

	FDungeonCell& GetCell(int32 X, int32 Y, int32 Z);
	const FDungeonCell& GetCell(int32 X, int32 Y, int32 Z) const;
	FDungeonCell& GetCell(const FIntVector& Coord);
	const FDungeonCell& GetCell(const FIntVector& Coord) const;

	bool IsInBounds(int32 X, int32 Y, int32 Z) const;
	bool IsInBounds(const FIntVector& Coord) const;
};

// ============================================================================
// USTRUCTs (Blueprint-visible data)
// ============================================================================

/** A placed room in the dungeon. */
USTRUCT(BlueprintType)
struct DUNGEONCORE_API FDungeonRoom
{
	GENERATED_BODY()

	/** Unique ID within the dungeon (1-255, 0 reserved for "no room"). */
	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	uint8 RoomIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	EDungeonRoomType RoomType = EDungeonRoomType::Generic;

	/** Grid-space origin (min corner). */
	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	FIntVector Position = FIntVector::ZeroValue;

	/** Grid-space dimensions (X width, Y depth, Z height). */
	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	FIntVector Size = FIntVector::ZeroValue;

	/** Cached center point for graph algorithms. */
	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	FIntVector Center = FIntVector::ZeroValue;

	// -- Connectivity (C++ only, not UPROPERTY) --
	TArray<uint8> ConnectedRoomIndices;
	bool bOnMainPath = false;
	int32 GraphDistanceFromEntrance = -1;

	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	int32 FloorLevel = 0;

	uint8 MaterialHint = 0;

	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	FString CustomTag;
};

/** A carved path between two rooms. */
USTRUCT(BlueprintType)
struct DUNGEONCORE_API FDungeonHallway
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	uint8 HallwayIndex = 0;

	/** Array index of starting room in FDungeonResult::Rooms. */
	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	uint8 RoomA = 0;

	/** Array index of ending room in FDungeonResult::Rooms. */
	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	uint8 RoomB = 0;

	/** Ordered cells along the path (C++ only). */
	TArray<FIntVector> PathCells;

	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	bool bHasStaircase = false;

	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	bool bIsFromMST = true;
};

/** Vertical connection carved by the pathfinder. */
USTRUCT(BlueprintType)
struct DUNGEONCORE_API FDungeonStaircase
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	FIntVector BottomCell = FIntVector::ZeroValue;

	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	FIntVector TopCell = FIntVector::ZeroValue;

	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	uint8 Direction = 0;

	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	int32 RiseRunRatio = 2;

	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	int32 HeadroomCells = 2;

	TArray<FIntVector> OccupiedCells;
};

/** Complete immutable output of the dungeon generator. */
USTRUCT(BlueprintType)
struct DUNGEONCORE_API FDungeonResult
{
	GENERATED_BODY()

	// -- Configuration snapshot --

	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	int64 Seed = 0;

	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	FIntVector GridSize = FIntVector::ZeroValue;

	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	float CellWorldSize = 400.0f;

	// -- Grid data (C++ only — too large for Blueprint) --
	FDungeonGrid Grid;

	// -- Structural elements --

	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	TArray<FDungeonRoom> Rooms;

	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	TArray<FDungeonHallway> Hallways;

	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	TArray<FDungeonStaircase> Staircases;

	// -- Graph data (C++ only — TPair not UPROPERTY-safe) --
	TArray<TPair<uint8, uint8>> DelaunayEdges;
	TArray<TPair<uint8, uint8>> MSTEdges;
	TArray<TPair<uint8, uint8>> FinalEdges;

	// -- Entrance --

	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	int32 EntranceRoomIndex = -1;

	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	FIntVector EntranceCell = FIntVector::ZeroValue;

	/** Resolved entrance approach (see FDungeonEntranceSpec). Approach None when none was requested. */
	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	FDungeonEntranceApproachInfo EntranceApproach;

	// -- Metrics --

	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	double GenerationTimeMs = 0.0;

	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	int32 TotalRoomCells = 0;

	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	int32 TotalHallwayCells = 0;

	UPROPERTY(BlueprintReadOnly, Category="Dungeon")
	int32 TotalStaircaseCells = 0;

	// -- Query methods --

	const FDungeonRoom* FindRoomByType(EDungeonRoomType Type) const;
	const FDungeonRoom* GetEntranceRoom() const;

	/**
	 * The cell whose LID a passage entering from above opens: the topmost cell of the entrance
	 * room directly above EntranceCell. EntranceCell is the walkable ground-floor cell of the
	 * entrance room; for a room taller than one floor the room's ceiling sits above the cells
	 * stacked on it, so a shaft must stop at (and the tile mapper must open) the top cell of that
	 * column, not the floor cell. Equals EntranceCell for single-floor rooms or when there is no
	 * entrance.
	 */
	FIntVector GetEntranceOpeningCell() const;

	/** Convert grid coordinate to world position (Z-up, direct mapping). */
	FVector GridToWorld(const FIntVector& GridCoord) const;

	/** Convert world position to grid coordinate (Z-up, direct mapping). */
	FIntVector WorldToGrid(const FVector& WorldPos) const;
};
