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

/** Where the dungeon entrance room is placed. */
UENUM(BlueprintType)
enum class EDungeonEntrancePlacement : uint8
{
	BoundaryEdge,
	TopFloor,
	BottomFloor,
	Any,
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

	/** Grid-space dimensions (width, height, depth). */
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

	/** Convert grid coordinate to world position. Grid Y (floor) maps to World Z (up). */
	FVector GridToWorld(const FIntVector& GridCoord) const;

	/** Convert world position to grid coordinate. */
	FIntVector WorldToGrid(const FVector& WorldPos) const;
};
