#include "DungeonTypes.h"

// ============================================================================
// FDungeonGrid
// ============================================================================

void FDungeonGrid::Initialize(const FIntVector& InGridSize)
{
	GridSize = InGridSize;
	Cells.SetNum(GridSize.X * GridSize.Y * GridSize.Z);
}

FDungeonCell& FDungeonGrid::GetCell(int32 X, int32 Y, int32 Z)
{
	checkf(IsInBounds(X, Y, Z), TEXT("Grid access out of bounds: (%d,%d,%d) in grid (%d,%d,%d)"),
		X, Y, Z, GridSize.X, GridSize.Y, GridSize.Z);
	return Cells[CellIndex(X, Y, Z)];
}

const FDungeonCell& FDungeonGrid::GetCell(int32 X, int32 Y, int32 Z) const
{
	checkf(IsInBounds(X, Y, Z), TEXT("Grid access out of bounds: (%d,%d,%d) in grid (%d,%d,%d)"),
		X, Y, Z, GridSize.X, GridSize.Y, GridSize.Z);
	return Cells[CellIndex(X, Y, Z)];
}

FDungeonCell& FDungeonGrid::GetCell(const FIntVector& Coord)
{
	return GetCell(Coord.X, Coord.Y, Coord.Z);
}

const FDungeonCell& FDungeonGrid::GetCell(const FIntVector& Coord) const
{
	return GetCell(Coord.X, Coord.Y, Coord.Z);
}

bool FDungeonGrid::IsInBounds(int32 X, int32 Y, int32 Z) const
{
	return X >= 0 && X < GridSize.X
		&& Y >= 0 && Y < GridSize.Y
		&& Z >= 0 && Z < GridSize.Z;
}

bool FDungeonGrid::IsInBounds(const FIntVector& Coord) const
{
	return IsInBounds(Coord.X, Coord.Y, Coord.Z);
}

// ============================================================================
// FDungeonResult
// ============================================================================

const FDungeonRoom* FDungeonResult::FindRoomByType(EDungeonRoomType Type) const
{
	for (const FDungeonRoom& Room : Rooms)
	{
		if (Room.RoomType == Type)
		{
			return &Room;
		}
	}
	return nullptr;
}

const FDungeonRoom* FDungeonResult::GetEntranceRoom() const
{
	if (EntranceRoomIndex >= 0 && EntranceRoomIndex < Rooms.Num())
	{
		return &Rooms[EntranceRoomIndex];
	}
	return nullptr;
}

FVector FDungeonResult::GridToWorld(const FIntVector& GridCoord) const
{
	return FVector(
		GridCoord.X * CellWorldSize,
		GridCoord.Y * CellWorldSize,
		GridCoord.Z * CellWorldSize
	);
}

FIntVector FDungeonResult::WorldToGrid(const FVector& WorldPos) const
{
	return FIntVector(
		FMath::FloorToInt32(WorldPos.X / CellWorldSize),
		FMath::FloorToInt32(WorldPos.Y / CellWorldSize),
		FMath::FloorToInt32(WorldPos.Z / CellWorldSize)
	);
}
