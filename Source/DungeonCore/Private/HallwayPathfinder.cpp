#include "HallwayPathfinder.h"
#include "DungeonTypes.h"
#include "DungeonConfig.h"

namespace
{
	// 4 cardinal directions on the XZ plane (Phase 1: 2D only)
	static const FIntVector Directions[] = {
		FIntVector( 1, 0,  0), // +X
		FIntVector(-1, 0,  0), // -X
		FIntVector( 0, 0,  1), // +Z
		FIntVector( 0, 0, -1), // -Z
	};

	float GetCellCost(
		const FDungeonGrid& Grid,
		const FIntVector& Coord,
		const UDungeonConfiguration& Config,
		uint8 SourceRoomIdx,
		uint8 DestRoomIdx)
	{
		const FDungeonCell& Cell = Grid.GetCell(Coord);

		switch (Cell.CellType)
		{
		case EDungeonCellType::Empty:
			return 1.0f;

		case EDungeonCellType::Hallway:
			return Config.HallwayMergeCostMultiplier;

		case EDungeonCellType::Door:
			return Config.HallwayMergeCostMultiplier;

		case EDungeonCellType::Room:
			if (Cell.RoomIndex == SourceRoomIdx || Cell.RoomIndex == DestRoomIdx)
			{
				return 0.0f; // Free to traverse source/dest rooms
			}
			return Config.RoomPassthroughCostMultiplier;

		case EDungeonCellType::RoomWall:
			return 5.0f;

		default:
			return -1.0f; // Blocked
		}
	}

	float Heuristic(const FIntVector& A, const FIntVector& B)
	{
		// Manhattan distance on XZ plane
		return static_cast<float>(FMath::Abs(A.X - B.X) + FMath::Abs(A.Z - B.Z));
	}
}

bool FHallwayPathfinder::FindPath(
	const FDungeonGrid& Grid,
	const FIntVector& Start,
	const FIntVector& End,
	const UDungeonConfiguration& Config,
	uint8 SourceRoomIdx,
	uint8 DestRoomIdx,
	TArray<FIntVector>& OutPath)
{
	OutPath.Reset();

	if (!Grid.IsInBounds(Start) || !Grid.IsInBounds(End))
	{
		return false;
	}

	if (Start == End)
	{
		OutPath.Add(Start);
		return true;
	}

	const int32 TotalCells = Grid.GridSize.X * Grid.GridSize.Y * Grid.GridSize.Z;

	// Flat arrays for O(1) lookup (indexed by grid cell index)
	TArray<float> GScore;
	GScore.SetNum(TotalCells);
	for (int32 i = 0; i < TotalCells; ++i)
	{
		GScore[i] = MAX_flt;
	}

	TArray<int32> CameFrom;
	CameFrom.SetNum(TotalCells);
	for (int32 i = 0; i < TotalCells; ++i)
	{
		CameFrom[i] = -1;
	}

	TArray<bool> ClosedSet;
	ClosedSet.SetNumZeroed(TotalCells);

	// Open set as min-heap
	struct FNode
	{
		float FScore;
		int32 CellIdx;

		bool operator>(const FNode& Other) const { return FScore > Other.FScore; }
	};

	const int32 StartIdx = Grid.CellIndex(Start);
	const int32 EndIdx = Grid.CellIndex(End);

	GScore[StartIdx] = 0.0f;

	TArray<FNode> OpenSet;
	OpenSet.HeapPush(
		FNode{Heuristic(Start, End), StartIdx},
		[](const FNode& A, const FNode& B) { return A.FScore > B.FScore; });

	while (OpenSet.Num() > 0)
	{
		FNode Current;
		OpenSet.HeapPop(Current,
			[](const FNode& A, const FNode& B) { return A.FScore > B.FScore; });

		if (Current.CellIdx == EndIdx)
		{
			// Reconstruct path
			int32 Idx = EndIdx;
			while (Idx != -1)
			{
				// Convert flat index back to 3D coordinate
				const int32 X = Idx % Grid.GridSize.X;
				const int32 Y = (Idx / Grid.GridSize.X) % Grid.GridSize.Y;
				const int32 Z = Idx / (Grid.GridSize.X * Grid.GridSize.Y);
				OutPath.Add(FIntVector(X, Y, Z));
				Idx = CameFrom[Idx];
			}

			Algo::Reverse(OutPath);
			return true;
		}

		if (ClosedSet[Current.CellIdx])
		{
			continue;
		}
		ClosedSet[Current.CellIdx] = true;

		// Decode current position
		const int32 CurX = Current.CellIdx % Grid.GridSize.X;
		const int32 CurY = (Current.CellIdx / Grid.GridSize.X) % Grid.GridSize.Y;
		const int32 CurZ = Current.CellIdx / (Grid.GridSize.X * Grid.GridSize.Y);
		const FIntVector CurCoord(CurX, CurY, CurZ);

		// Explore neighbors
		for (const FIntVector& Dir : Directions)
		{
			const FIntVector NeighborCoord = CurCoord + Dir;

			if (!Grid.IsInBounds(NeighborCoord))
			{
				continue;
			}

			const int32 NeighborIdx = Grid.CellIndex(NeighborCoord);

			if (ClosedSet[NeighborIdx])
			{
				continue;
			}

			const float MoveCost = GetCellCost(Grid, NeighborCoord, Config, SourceRoomIdx, DestRoomIdx);
			if (MoveCost < 0.0f)
			{
				continue; // Blocked
			}

			const float TentativeG = GScore[Current.CellIdx] + FMath::Max(MoveCost, 0.001f);

			if (TentativeG < GScore[NeighborIdx])
			{
				GScore[NeighborIdx] = TentativeG;
				CameFrom[NeighborIdx] = Current.CellIdx;

				const float F = TentativeG + Heuristic(NeighborCoord, End);
				OpenSet.HeapPush(
					FNode{F, NeighborIdx},
					[](const FNode& A, const FNode& B) { return A.FScore > B.FScore; });
			}
		}
	}

	return false; // No path found
}

void FHallwayPathfinder::CarveHallway(
	FDungeonGrid& Grid,
	const TArray<FIntVector>& Path,
	uint8 HallwayIndex,
	uint8 SourceRoomIdx,
	uint8 DestRoomIdx)
{
	for (int32 i = 0; i < Path.Num(); ++i)
	{
		const FIntVector& Coord = Path[i];
		if (!Grid.IsInBounds(Coord))
		{
			continue;
		}

		FDungeonCell& Cell = Grid.GetCell(Coord);

		// Skip cells that already belong to source/dest rooms
		if (Cell.CellType == EDungeonCellType::Room &&
			(Cell.RoomIndex == SourceRoomIdx || Cell.RoomIndex == DestRoomIdx))
		{
			// Check if this room cell is adjacent to a hallway cell — mark as Door
			bool bAdjacentToHallway = false;
			if (i > 0)
			{
				const FDungeonCell& PrevCell = Grid.GetCell(Path[i - 1]);
				if (PrevCell.CellType == EDungeonCellType::Hallway)
				{
					bAdjacentToHallway = true;
				}
			}
			if (i < Path.Num() - 1)
			{
				const FDungeonCell& NextCell = Grid.GetCell(Path[i + 1]);
				if (NextCell.CellType == EDungeonCellType::Hallway ||
					NextCell.CellType == EDungeonCellType::Empty)
				{
					// Next cell will become hallway — this is a transition point
					if (NextCell.CellType == EDungeonCellType::Empty)
					{
						bAdjacentToHallway = true;
					}
				}
			}

			if (bAdjacentToHallway)
			{
				Cell.CellType = EDungeonCellType::Door;
				Cell.HallwayIndex = HallwayIndex;
			}
			continue;
		}

		// Skip existing hallways (reuse them, don't overwrite index)
		if (Cell.CellType == EDungeonCellType::Hallway)
		{
			continue;
		}

		// Carve this cell as hallway
		if (Cell.CellType == EDungeonCellType::Empty)
		{
			Cell.CellType = EDungeonCellType::Hallway;
			Cell.HallwayIndex = HallwayIndex;
		}
	}

	// Second pass: mark door cells at room<->hallway transitions
	for (int32 i = 1; i < Path.Num() - 1; ++i)
	{
		const FIntVector& Coord = Path[i];
		FDungeonCell& Cell = Grid.GetCell(Coord);

		if (Cell.CellType != EDungeonCellType::Room)
		{
			continue;
		}

		// Check if previous or next cell in path is a hallway
		const FDungeonCell& Prev = Grid.GetCell(Path[i - 1]);
		const FDungeonCell& Next = Grid.GetCell(Path[i + 1]);

		const bool bPrevIsHallway = (Prev.CellType == EDungeonCellType::Hallway);
		const bool bNextIsHallway = (Next.CellType == EDungeonCellType::Hallway);

		if (bPrevIsHallway || bNextIsHallway)
		{
			Cell.CellType = EDungeonCellType::Door;
			Cell.HallwayIndex = HallwayIndex;
		}
	}
}
