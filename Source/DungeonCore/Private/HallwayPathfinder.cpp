#include "HallwayPathfinder.h"
#include "DungeonTypes.h"
#include "DungeonConfig.h"

DEFINE_LOG_CATEGORY_STATIC(LogDungeonPathfinder, Log, All);

namespace
{
	// 4 cardinal directions on the XY plane (Z is up)
	struct FHDir { int32 DX, DY; };
	static const FHDir HorizontalDirs[] = {
		{ 1,  0}, // +X
		{-1,  0}, // -X
		{ 0,  1}, // +Y
		{ 0, -1}, // -Y
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
		case EDungeonCellType::Door:
			return Config.HallwayMergeCostMultiplier;
		case EDungeonCellType::Room:
			if (Cell.RoomIndex == SourceRoomIdx || Cell.RoomIndex == DestRoomIdx)
			{
				return 0.0f;
			}
			return Config.RoomPassthroughCostMultiplier;
		case EDungeonCellType::RoomWall:
			return 5.0f;
		default:
			return -1.0f; // Blocked (Staircase, StaircaseHead, Entrance)
		}
	}

	bool IsCellAvailableForStaircase(const FDungeonGrid& Grid, const FIntVector& Coord)
	{
		if (!Grid.IsInBounds(Coord)) return false;
		const EDungeonCellType Type = Grid.GetCell(Coord).CellType;
		return Type == EDungeonCellType::Empty || Type == EDungeonCellType::Hallway;
	}

	float Heuristic(const FIntVector& A, const FIntVector& B, int32 RiseToRun)
	{
		const float Horizontal = static_cast<float>(FMath::Abs(A.X - B.X) + FMath::Abs(A.Y - B.Y));
		const float Vertical = static_cast<float>(FMath::Abs(A.Z - B.Z)) * static_cast<float>(RiseToRun + 1);
		return Horizontal + Vertical;
	}
}

// ============================================================================
// Staircase validation
// ============================================================================

bool FHallwayPathfinder::CanBuildStaircase(
	const FDungeonGrid& Grid,
	const FIntVector& Entry,
	int32 DirX, int32 DirY,
	int32 Rise,
	int32 RiseToRun,
	int32 HeadroomCells,
	FIntVector& OutExit)
{
	// Body cells are on the lower floor, headroom above them (Z is up)
	const int32 LowerZ = (Rise > 0) ? Entry.Z : Entry.Z - 1;
	const int32 UpperZ = LowerZ + 1;

	if (LowerZ < 0 || UpperZ >= Grid.GridSize.Z)
	{
		return false;
	}

	// Check body cells (RiseToRun cells on the lower floor, in the horizontal direction)
	for (int32 i = 1; i <= RiseToRun; ++i)
	{
		const FIntVector BodyCell(Entry.X + DirX * i, Entry.Y + DirY * i, LowerZ);
		if (!IsCellAvailableForStaircase(Grid, BodyCell))
		{
			return false;
		}
	}

	// Check headroom cells above each body cell
	for (int32 i = 1; i <= RiseToRun; ++i)
	{
		for (int32 h = 1; h <= HeadroomCells; ++h)
		{
			const FIntVector HeadCell(Entry.X + DirX * i, Entry.Y + DirY * i, LowerZ + h);
			if (!Grid.IsInBounds(HeadCell))
			{
				// Headroom out of bounds on higher floors is OK (open sky)
				continue;
			}
			if (!IsCellAvailableForStaircase(Grid, HeadCell))
			{
				return false;
			}
		}
	}

	// Check exit cell (one step past the staircase on the target floor)
	const int32 TargetZ = (Rise > 0) ? UpperZ : LowerZ;
	OutExit = FIntVector(
		Entry.X + DirX * (RiseToRun + 1),
		Entry.Y + DirY * (RiseToRun + 1),
		TargetZ);

	if (!Grid.IsInBounds(OutExit))
	{
		return false;
	}

	// Exit cell must be available (Empty, Hallway, or dest room)
	const FDungeonCell& ExitCell = Grid.GetCell(OutExit);
	if (ExitCell.CellType != EDungeonCellType::Empty &&
		ExitCell.CellType != EDungeonCellType::Hallway &&
		ExitCell.CellType != EDungeonCellType::Room)
	{
		return false;
	}

	return true;
}

// ============================================================================
// A* Pathfinding
// ============================================================================

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
	const int32 RiseToRun = Config.StaircaseRiseToRun;
	const int32 HeadroomCells = Config.StaircaseHeadroom;

	// Flat arrays for O(1) lookup
	TArray<float> GScore;
	GScore.SetNum(TotalCells);
	for (float& G : GScore) G = MAX_flt;

	TArray<int32> CameFrom;
	CameFrom.SetNum(TotalCells);
	for (int32& P : CameFrom) P = -1;

	TArray<bool> ClosedSet;
	ClosedSet.SetNumZeroed(TotalCells);

	// Min-heap open set
	struct FNode
	{
		float FScore;
		int32 CellIdx;
	};
	auto HeapPred = [](const FNode& A, const FNode& B) { return A.FScore < B.FScore; };

	const int32 StartIdx = Grid.CellIndex(Start);
	const int32 EndIdx = Grid.CellIndex(End);

	GScore[StartIdx] = 0.0f;

	TArray<FNode> OpenSet;
	OpenSet.HeapPush(FNode{Heuristic(Start, End, RiseToRun), StartIdx}, HeapPred);

	while (OpenSet.Num() > 0)
	{
		FNode Current;
		OpenSet.HeapPop(Current, HeapPred);

		if (Current.CellIdx == EndIdx)
		{
			// Reconstruct path
			int32 Idx = EndIdx;
			while (Idx != -1)
			{
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

		// --- Same-floor cardinal moves (XY plane) ---
		for (const FHDir& Dir : HorizontalDirs)
		{
			const FIntVector NeighborCoord(CurX + Dir.DX, CurY + Dir.DY, CurZ);
			if (!Grid.IsInBounds(NeighborCoord)) continue;

			const int32 NeighborIdx = Grid.CellIndex(NeighborCoord);
			if (ClosedSet[NeighborIdx]) continue;

			const float MoveCost = GetCellCost(Grid, NeighborCoord, Config, SourceRoomIdx, DestRoomIdx);
			if (MoveCost < 0.0f) continue;

			const float TentativeG = GScore[Current.CellIdx] + FMath::Max(MoveCost, 0.001f);
			if (TentativeG < GScore[NeighborIdx])
			{
				GScore[NeighborIdx] = TentativeG;
				CameFrom[NeighborIdx] = Current.CellIdx;
				OpenSet.HeapPush(
					FNode{TentativeG + Heuristic(NeighborCoord, End, RiseToRun), NeighborIdx}, HeapPred);
			}
		}

		// --- Staircase moves (4 directions × up/down along Z) ---
		if (Grid.GridSize.Z > 1)
		{
			for (const FHDir& Dir : HorizontalDirs)
			{
				for (int32 Rise : {+1, -1})
				{
					FIntVector ExitCell;
					if (!CanBuildStaircase(Grid, CurCoord, Dir.DX, Dir.DY, Rise,
					                       RiseToRun, HeadroomCells, ExitCell))
					{
						continue;
					}

					const int32 ExitIdx = Grid.CellIndex(ExitCell);
					if (ClosedSet[ExitIdx]) continue;

					// Cost: traverse RiseToRun body cells + exit cell
					const float StaircaseCost = static_cast<float>(RiseToRun + 1) + 1.0f;
					const float ExitCellCost = GetCellCost(Grid, ExitCell, Config, SourceRoomIdx, DestRoomIdx);
					if (ExitCellCost < 0.0f) continue;

					const float TentativeG = GScore[Current.CellIdx] + StaircaseCost + FMath::Max(ExitCellCost, 0.001f);
					if (TentativeG < GScore[ExitIdx])
					{
						GScore[ExitIdx] = TentativeG;
						CameFrom[ExitIdx] = Current.CellIdx;
						OpenSet.HeapPush(
							FNode{TentativeG + Heuristic(ExitCell, End, RiseToRun), ExitIdx}, HeapPred);
					}
				}
			}
		}
	}

	return false;
}

// ============================================================================
// Hallway & Staircase Carving
// ============================================================================

void FHallwayPathfinder::CarveHallway(
	FDungeonGrid& Grid,
	const TArray<FIntVector>& Path,
	uint8 HallwayIndex,
	uint8 SourceRoomIdx,
	uint8 DestRoomIdx,
	const UDungeonConfiguration& Config,
	TArray<FDungeonStaircase>& OutStaircases)
{
	const int32 RiseToRun = Config.StaircaseRiseToRun;
	const int32 HeadroomCells = Config.StaircaseHeadroom;

	for (int32 i = 0; i < Path.Num(); ++i)
	{
		const FIntVector& Coord = Path[i];
		if (!Grid.IsInBounds(Coord)) continue;

		// Detect staircase transitions: non-adjacent cells with Z change
		if (i > 0)
		{
			const FIntVector& Prev = Path[i - 1];
			const int32 DZ = Coord.Z - Prev.Z;

			if (DZ != 0)
			{
				// This is a staircase transition from Prev to Coord
				const int32 DX = (Coord.X - Prev.X);
				const int32 DY = (Coord.Y - Prev.Y);
				const int32 DirX = (DX != 0) ? (DX > 0 ? 1 : -1) : 0;
				const int32 DirY = (DY != 0) ? (DY > 0 ? 1 : -1) : 0;
				const int32 Rise = (DZ > 0) ? 1 : -1;
				const int32 LowerZ = FMath::Min(Prev.Z, Coord.Z);

				FDungeonStaircase Staircase;
				Staircase.BottomCell = (Rise > 0) ? Prev : Coord;
				Staircase.TopCell = (Rise > 0) ? Coord : Prev;
				Staircase.Direction = static_cast<uint8>(
					(DirX == 1) ? 0 : (DirX == -1) ? 1 : (DirY == 1) ? 2 : 3);
				Staircase.RiseRunRatio = RiseToRun;
				Staircase.HeadroomCells = HeadroomCells;

				// Carve body cells on the lower floor
				for (int32 s = 1; s <= RiseToRun; ++s)
				{
					const FIntVector BodyCell(Prev.X + DirX * s, Prev.Y + DirY * s, LowerZ);
					if (Grid.IsInBounds(BodyCell))
					{
						FDungeonCell& Cell = Grid.GetCell(BodyCell);
						Cell.CellType = EDungeonCellType::Staircase;
						Cell.HallwayIndex = HallwayIndex;
						Cell.StaircaseDirection = Staircase.Direction;
						Staircase.OccupiedCells.Add(BodyCell);
					}
				}

				// Carve headroom cells above body (Z is up)
				for (int32 s = 1; s <= RiseToRun; ++s)
				{
					for (int32 h = 1; h <= HeadroomCells; ++h)
					{
						const FIntVector HeadCell(Prev.X + DirX * s, Prev.Y + DirY * s, LowerZ + h);
						if (Grid.IsInBounds(HeadCell))
						{
							FDungeonCell& HeadCellRef = Grid.GetCell(HeadCell);
							if (HeadCellRef.CellType == EDungeonCellType::Empty)
							{
								HeadCellRef.CellType = EDungeonCellType::StaircaseHead;
								HeadCellRef.HallwayIndex = HallwayIndex;
							}
							Staircase.OccupiedCells.Add(HeadCell);
						}
					}
				}

				OutStaircases.Add(MoveTemp(Staircase));
				// Don't skip carving the exit cell — fall through to normal carving below
			}
		}

		FDungeonCell& Cell = Grid.GetCell(Coord);

		// Skip cells belonging to source/dest rooms (but mark doors at transitions)
		if (Cell.CellType == EDungeonCellType::Room &&
			(Cell.RoomIndex == SourceRoomIdx || Cell.RoomIndex == DestRoomIdx))
		{
			continue;
		}

		// Don't overwrite existing hallways or staircases
		if (Cell.CellType == EDungeonCellType::Hallway ||
			Cell.CellType == EDungeonCellType::Staircase ||
			Cell.CellType == EDungeonCellType::StaircaseHead)
		{
			continue;
		}

		// Carve as hallway
		if (Cell.CellType == EDungeonCellType::Empty)
		{
			Cell.CellType = EDungeonCellType::Hallway;
			Cell.HallwayIndex = HallwayIndex;
		}
	}

	// Door placement pass: mark room cells adjacent to hallway cells along the path
	for (int32 i = 1; i < Path.Num() - 1; ++i)
	{
		const FIntVector& Coord = Path[i];
		if (!Grid.IsInBounds(Coord)) continue;

		FDungeonCell& Cell = Grid.GetCell(Coord);
		if (Cell.CellType != EDungeonCellType::Room) continue;
		if (Cell.RoomIndex != SourceRoomIdx && Cell.RoomIndex != DestRoomIdx) continue;

		// Check if an adjacent path cell is a hallway
		const bool bPrevIsHallway = (i > 0 && Grid.IsInBounds(Path[i - 1]) &&
			Grid.GetCell(Path[i - 1]).CellType == EDungeonCellType::Hallway);
		const bool bNextIsHallway = (i < Path.Num() - 1 && Grid.IsInBounds(Path[i + 1]) &&
			Grid.GetCell(Path[i + 1]).CellType == EDungeonCellType::Hallway);

		if (bPrevIsHallway || bNextIsHallway)
		{
			Cell.CellType = EDungeonCellType::Door;
			Cell.HallwayIndex = HallwayIndex;
		}
	}
}
