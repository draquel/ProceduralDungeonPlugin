#include "HallwayPathfinder.h"
#include "DungeonTypes.h"
#include "DungeonConfig.h"
#include "DungeonBoundaryRules.h"

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

	// Staircase climb direction lookup: 0=+X, 1=-X, 2=+Y, 3=-Y
	static constexpr int32 StairClimbDX[] = {1, -1, 0, 0};
	static constexpr int32 StairClimbDY[] = {0, 0, 1, -1};

	// Returns true if this cell is above the ground floor of its room.
	// Upper room cells are airspace (no walkable floor) and must be blocked for pathfinding.
	bool IsUpperRoomCell(const FDungeonGrid& Grid, const FIntVector& Coord, uint8 RoomIndex)
	{
		const FIntVector Below(Coord.X, Coord.Y, Coord.Z - 1);
		if (!Grid.IsInBounds(Below)) return false;
		const FDungeonCell& BelowCell = Grid.GetCell(Below);
		return (BelowCell.CellType == EDungeonCellType::Room
			|| BelowCell.CellType == EDungeonCellType::RoomWall)
			&& BelowCell.RoomIndex == RoomIndex;
	}

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
			// Block upper room cells — airspace above the ground floor has no walkable surface.
			// Ground floor is detected by checking if the cell below belongs to the same room.
			if (IsUpperRoomCell(Grid, Coord, Cell.RoomIndex))
			{
				return -1.0f;
			}
			if (Cell.RoomIndex == SourceRoomIdx || Cell.RoomIndex == DestRoomIdx)
			{
				return 0.0f;
			}
			return Config.RoomPassthroughCostMultiplier;
		case EDungeonCellType::RoomWall:
			// Block upper room walls — can't break through walls above the ground floor.
			if (IsUpperRoomCell(Grid, Coord, Cell.RoomIndex))
			{
				return -1.0f;
			}
			return 5.0f;
		default:
			return -1.0f; // Blocked (Staircase, StaircaseHead, Entrance)
		}
	}

	bool IsCellAvailableForStaircase(const FDungeonGrid& Grid, const FIntVector& Coord)
	{
		if (!Grid.IsInBounds(Coord)) return false;
		const EDungeonCellType Type = Grid.GetCell(Coord).CellType;
		// Only allow empty cells — existing hallways, staircases, and rooms are off-limits.
		// This prevents new staircases from overlapping existing corridors.
		return Type == EDungeonCellType::Empty;
	}

	// A walkable cell that must never sit on a staircase flank: it would be walled off from the
	// ramp (FDungeonBoundaryRules rule 2) and read as a corridor running into the side of an
	// incline. Rooms beside a flank are fine: they are walled anyway as a different space.
	bool IsFlankBlocker(EDungeonCellType Type)
	{
		return Type == EDungeonCellType::Hallway || Type == EDungeonCellType::Door;
	}

	float Heuristic(const FIntVector& A, const FIntVector& B, int32 RiseToRun)
	{
		const float Horizontal = static_cast<float>(FMath::Abs(A.X - B.X) + FMath::Abs(A.Y - B.Y));
		const float Vertical = static_cast<float>(FMath::Abs(A.Z - B.Z)) * static_cast<float>(RiseToRun + 1);
		return Horizontal + Vertical;
	}

	/** The two flank offsets of a staircase climbing along (DirX, DirY). */
	void FlankOffsets(int32 DirX, int32 DirY, FIntVector& OutA, FIntVector& OutB)
	{
		OutA = FIntVector(DirY, DirX, 0);
		OutB = FIntVector(-DirY, -DirX, 0);
	}

	/**
	 * Path cells that would be carved as hallway on the flank of a staircase THIS path builds.
	 * A* cannot see that while searching: the corridor cells are still Empty in the grid, and a
	 * later staircase move may be planned right beside them (or the corridor may double back
	 * beside a planned ramp). Staircases are recognised the way CarveHallway does, as Z
	 * transitions between consecutive path cells.
	 */
	TArray<FIntVector> PathFlankViolations(
		const FDungeonGrid& Grid,
		const TArray<FIntVector>& Path,
		int32 RiseToRun,
		int32 HeadroomCells,
		const FIntVector& End)
	{
		TSet<FIntVector> StairCells;
		TSet<FIntVector> FlankCells;

		for (int32 i = 1; i < Path.Num(); ++i)
		{
			const FIntVector& Prev = Path[i - 1];
			const FIntVector& Coord = Path[i];
			if (Coord.Z == Prev.Z)
			{
				continue;
			}
			const int32 DX = Coord.X - Prev.X;
			const int32 DY = Coord.Y - Prev.Y;
			const int32 DirX = (DX != 0) ? (DX > 0 ? 1 : -1) : 0;
			const int32 DirY = (DY != 0) ? (DY > 0 ? 1 : -1) : 0;
			const int32 LowerZ = FMath::Min(Prev.Z, Coord.Z);

			FIntVector FlankA, FlankB;
			FlankOffsets(DirX, DirY, FlankA, FlankB);

			for (int32 s = 1; s <= RiseToRun; ++s)
			{
				for (int32 h = 0; h <= HeadroomCells; ++h)
				{
					const FIntVector Cell(Prev.X + DirX * s, Prev.Y + DirY * s, LowerZ + h);
					StairCells.Add(Cell);
					FlankCells.Add(Cell + FlankA);
					FlankCells.Add(Cell + FlankB);
				}
			}
		}

		TArray<FIntVector> Violations;
		for (const FIntVector& Coord : Path)
		{
			if (Coord == End || StairCells.Contains(Coord) || !FlankCells.Contains(Coord))
			{
				continue;
			}
			// Only cells that will be carved as hallway matter; room cells stay walled.
			if (!Grid.IsInBounds(Coord) || Grid.GetCell(Coord).CellType == EDungeonCellType::Room)
			{
				continue;
			}
			Violations.Add(Coord);
		}
		return Violations;
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

	// Check that body cells aren't adjacent to existing staircase zones (prevents back-to-back
	// and side-by-side staircase placement), and that nothing walkable already sits on a flank
	// (a corridor beside the ramp would be walled off from it and read as running into its side).
	for (int32 i = 1; i <= RiseToRun; ++i)
	{
		const FIntVector BodyCell(Entry.X + DirX * i, Entry.Y + DirY * i, LowerZ);
		for (const FHDir& HDir : HorizontalDirs)
		{
			const FIntVector Neighbor(BodyCell.X + HDir.DX, BodyCell.Y + HDir.DY, BodyCell.Z);
			if (!Grid.IsInBounds(Neighbor)) continue;
			const EDungeonCellType NType = Grid.GetCell(Neighbor).CellType;
			if (NType == EDungeonCellType::Staircase || NType == EDungeonCellType::StaircaseHead)
			{
				return false;
			}
			const bool bFlank = (HDir.DX * DirX + HDir.DY * DirY == 0);
			if (bFlank && IsFlankBlocker(NType))
			{
				return false;
			}
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

			// Headroom cells must not be cardinally adjacent to existing staircases, nor have
			// a walkable cell on a flank (it would open onto the shaft above the ramp).
			for (const FHDir& HDir : HorizontalDirs)
			{
				const FIntVector Adj(HeadCell.X + HDir.DX, HeadCell.Y + HDir.DY, HeadCell.Z);
				if (!Grid.IsInBounds(Adj)) continue;
				const EDungeonCellType AdjType = Grid.GetCell(Adj).CellType;
				if (AdjType == EDungeonCellType::Staircase || AdjType == EDungeonCellType::StaircaseHead)
				{
					return false;
				}
				const bool bFlank = (HDir.DX * DirX + HDir.DY * DirY == 0);
				if (bFlank && IsFlankBlocker(AdjType))
				{
					return false;
				}
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

	// The exit landing becomes a hallway cell: it must not lie on another staircase's flank.
	if (FDungeonBoundaryRules::IsStairFlankCell(Grid, OutExit))
	{
		return false;
	}

	return true;
}

// ============================================================================
// A* Pathfinding
// ============================================================================

namespace
{
	/**
	 * True when Cell lies on a flank of a staircase that the came-from chain from NodeIdx already
	 * contains. Staircases are recognised as Z transitions between consecutive chain nodes, the
	 * same way CarveHallway recognises them in the finished path. This is exact per path: it
	 * blocks only the flanks of staircases this route really uses, unlike reserving the flanks
	 * of every staircase move A* ever relaxes, which starved flat movement across the grid and
	 * pushed routes into stair pairs as detours (staircase counts tripled).
	 */
	bool IsOnAncestorStairFlank(
		const FDungeonGrid& Grid,
		const TArray<int32>& CameFrom,
		int32 NodeIdx,
		const FIntVector& Cell,
		int32 RiseToRun,
		int32 HeadroomCells)
	{
		const int32 GX = Grid.GridSize.X;
		const int32 GY = Grid.GridSize.Y;
		auto Decode = [GX, GY](int32 Idx)
		{
			return FIntVector(Idx % GX, (Idx / GX) % GY, Idx / (GX * GY));
		};

		int32 Child = NodeIdx;
		for (int32 Parent = CameFrom[Child]; Parent != -1; Child = Parent, Parent = CameFrom[Parent])
		{
			const FIntVector C = Decode(Child);
			const FIntVector P = Decode(Parent);
			if (C.Z == P.Z)
			{
				continue;
			}
			// A staircase was travelled from P to C.
			const int32 DX = C.X - P.X;
			const int32 DY = C.Y - P.Y;
			const int32 DirX = (DX != 0) ? (DX > 0 ? 1 : -1) : 0;
			const int32 DirY = (DY != 0) ? (DY > 0 ? 1 : -1) : 0;
			const int32 LowerZ = FMath::Min(P.Z, C.Z);
			FIntVector FlankA, FlankB;
			FlankOffsets(DirX, DirY, FlankA, FlankB);
			for (int32 s = 1; s <= RiseToRun; ++s)
			{
				for (int32 h = 0; h <= HeadroomCells; ++h)
				{
					const FIntVector StairCell(P.X + DirX * s, P.Y + DirY * s, LowerZ + h);
					if (Cell == StairCell + FlankA || Cell == StairCell + FlankB)
					{
						return true;
					}
				}
			}
		}
		return false;
	}

	/**
	 * One A* search. BlockedCells are cells this call may not route through (flank cells of the
	 * path's own staircases found by a previous attempt).
	 */
	bool FindPathOnce(
		const FDungeonGrid& Grid,
		const FIntVector& Start,
		const FIntVector& End,
		const UDungeonConfiguration& Config,
		uint8 SourceRoomIdx,
		uint8 DestRoomIdx,
		const TArray<bool>& BlockedCells,
		TArray<FIntVector>& OutPath)
	{
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

		// Tracks cells claimed by staircase body/headroom during pathfinding.
		// Prevents a second staircase from stacking on top of an already-planned one.
		TArray<bool> StaircaseReserved;
		StaircaseReserved.SetNumZeroed(TotalCells);

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
				if (StaircaseReserved[NeighborIdx]) continue;
				if (BlockedCells[NeighborIdx]) continue;

				// Keep hallways off staircase flanks: already carved, or used by the route to this
				// node. The destination itself is exempt (it is a room cell, walled anyway).
				if (NeighborIdx != EndIdx)
				{
					if (FDungeonBoundaryRules::IsStairFlankCell(Grid, NeighborCoord)) continue;
					if (IsOnAncestorStairFlank(Grid, CameFrom, Current.CellIdx, NeighborCoord, RiseToRun, HeadroomCells)) continue;
				}

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
						if (!FHallwayPathfinder::CanBuildStaircase(Grid, CurCoord, Dir.DX, Dir.DY, Rise,
						                                           RiseToRun, HeadroomCells, ExitCell))
						{
							continue;
						}

						const int32 ExitIdx = Grid.CellIndex(ExitCell);
						if (ClosedSet[ExitIdx]) continue;
						if (StaircaseReserved[ExitIdx]) continue;
						if (BlockedCells[ExitIdx]) continue;
						if (ExitIdx != EndIdx
							&& IsOnAncestorStairFlank(Grid, CameFrom, Current.CellIdx, ExitCell, RiseToRun, HeadroomCells))
						{
							continue;
						}

						// Check that body/headroom cells don't overlap with an already-planned staircase
						const int32 StairLowerZ = (Rise > 0) ? CurZ : CurZ - 1;
						bool bOverlapsReserved = false;
						for (int32 s = 1; s <= RiseToRun && !bOverlapsReserved; ++s)
						{
							const FIntVector BodyCell(CurX + Dir.DX * s, CurY + Dir.DY * s, StairLowerZ);
							if (Grid.IsInBounds(BodyCell) && StaircaseReserved[Grid.CellIndex(BodyCell)])
							{
								bOverlapsReserved = true;
							}
							for (int32 h = 1; h <= HeadroomCells && !bOverlapsReserved; ++h)
							{
								const FIntVector HeadCell(CurX + Dir.DX * s, CurY + Dir.DY * s, StairLowerZ + h);
								if (Grid.IsInBounds(HeadCell) && StaircaseReserved[Grid.CellIndex(HeadCell)])
								{
									bOverlapsReserved = true;
								}
							}
						}
						if (bOverlapsReserved) continue;

						// Check that body AND headroom cells aren't adjacent to already-reserved cells.
						// Prevents elbow/U-staircase connections through staircase sides
						// within the same A* path (grid adjacency check only catches carved stairs).
						bool bAdjacentToReserved = false;
						for (int32 s = 1; s <= RiseToRun && !bAdjacentToReserved; ++s)
						{
							// Body cell adjacency
							const FIntVector BodyCell(CurX + Dir.DX * s, CurY + Dir.DY * s, StairLowerZ);
							for (const FHDir& AdjDir : HorizontalDirs)
							{
								const FIntVector Adj(BodyCell.X + AdjDir.DX, BodyCell.Y + AdjDir.DY, StairLowerZ);
								if (Grid.IsInBounds(Adj) && StaircaseReserved[Grid.CellIndex(Adj)])
								{
									bAdjacentToReserved = true;
									break;
								}
							}
							// Headroom cell adjacency
							for (int32 h = 1; h <= HeadroomCells && !bAdjacentToReserved; ++h)
							{
								const FIntVector HeadCell(CurX + Dir.DX * s, CurY + Dir.DY * s, StairLowerZ + h);
								for (const FHDir& AdjDir : HorizontalDirs)
								{
									const FIntVector Adj(HeadCell.X + AdjDir.DX, HeadCell.Y + AdjDir.DY, HeadCell.Z);
									if (Grid.IsInBounds(Adj) && StaircaseReserved[Grid.CellIndex(Adj)])
									{
										bAdjacentToReserved = true;
										break;
									}
								}
							}
						}
						if (bAdjacentToReserved) continue;

						// Reject a staircase whose flanks the path to this node already runs through:
						// the corridor was laid down before the ramp beside it was planned, so no
						// reservation can catch it. The came-from chain IS the path this staircase
						// would extend.
						{
							FIntVector FlankA, FlankB;
							FlankOffsets(Dir.DX, Dir.DY, FlankA, FlankB);
							TArray<int32, TInlineAllocator<32>> FlankIndices;
							for (int32 s = 1; s <= RiseToRun; ++s)
							{
								for (int32 h = 0; h <= HeadroomCells; ++h)
								{
									const FIntVector Cell(CurX + Dir.DX * s, CurY + Dir.DY * s, StairLowerZ + h);
									for (const FIntVector& Flank : { Cell + FlankA, Cell + FlankB })
									{
										if (Grid.IsInBounds(Flank))
										{
											FlankIndices.Add(Grid.CellIndex(Flank));
										}
									}
								}
							}
							bool bPathOnOwnFlank = false;
							for (int32 Ancestor = Current.CellIdx; Ancestor != -1 && !bPathOnOwnFlank; Ancestor = CameFrom[Ancestor])
							{
								bPathOnOwnFlank = FlankIndices.Contains(Ancestor);
							}
							if (bPathOnOwnFlank) continue;
						}

						// Cost: traverse RiseToRun body cells + exit cell
						const float StaircaseCost = static_cast<float>(RiseToRun + 1) * 5.0f;
						const float ExitCellCost = GetCellCost(Grid, ExitCell, Config, SourceRoomIdx, DestRoomIdx);
						if (ExitCellCost < 0.0f) continue;

						const float TentativeG = GScore[Current.CellIdx] + StaircaseCost + FMath::Max(ExitCellCost, 0.001f);
						if (TentativeG < GScore[ExitIdx])
						{
							GScore[ExitIdx] = TentativeG;
							CameFrom[ExitIdx] = Current.CellIdx;
							OpenSet.HeapPush(
								FNode{TentativeG + Heuristic(ExitCell, End, RiseToRun), ExitIdx}, HeapPred);

							// Reserve body and headroom cells for this staircase. Its flanks are NOT
							// reserved here: moves check the flanks of the staircases actually on
							// their route (IsOnAncestorStairFlank) instead.
							for (int32 s = 1; s <= RiseToRun; ++s)
							{
								for (int32 h = 0; h <= HeadroomCells; ++h)
								{
									const FIntVector Cell(CurX + Dir.DX * s, CurY + Dir.DY * s, StairLowerZ + h);
									if (Grid.IsInBounds(Cell))
									{
										StaircaseReserved[Grid.CellIndex(Cell)] = true;
									}
								}
							}
						}
					}
				}
			}
		}

		return false;
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
	TArray<bool> BlockedCells;
	BlockedCells.SetNumZeroed(TotalCells);

	// A* keeps hallways off the flanks of staircases it has already planned, but a corridor
	// laid down BEFORE a staircase move is planned beside it is invisible to the search. Check
	// the finished path, block the offending flank cells and search again, a bounded number of
	// times. If the last attempt still violates, the path is kept (rooms stay connected) and
	// FDungeonValidator reports the flank.
	constexpr int32 MaxAttempts = 4;
	for (int32 Attempt = 0; Attempt < MaxAttempts; ++Attempt)
	{
		OutPath.Reset();
		if (!FindPathOnce(Grid, Start, End, Config, SourceRoomIdx, DestRoomIdx, BlockedCells, OutPath))
		{
			return false;
		}

		const TArray<FIntVector> Violations = PathFlankViolations(
			Grid, OutPath, Config.StaircaseRiseToRun, Config.StaircaseHeadroom, End);
		if (Violations.Num() == 0)
		{
			return true;
		}

		for (const FIntVector& Cell : Violations)
		{
			BlockedCells[Grid.CellIndex(Cell)] = true;
		}
		UE_LOG(LogDungeonPathfinder, Verbose,
			TEXT("FindPath (%d,%d,%d)->(%d,%d,%d): attempt %d routed %d hallway cell(s) onto its own staircase flank; retrying"),
			Start.X, Start.Y, Start.Z, End.X, End.Y, End.Z, Attempt + 1, Violations.Num());
	}

	UE_LOG(LogDungeonPathfinder, Warning,
		TEXT("FindPath (%d,%d,%d)->(%d,%d,%d): still has hallway cells on its own staircase flanks after %d attempts; keeping the path"),
		Start.X, Start.Y, Start.Z, End.X, End.Y, End.Z, MaxAttempts);
	return true;
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
				// Direction = climb direction (BottomCell → TopCell), not travel direction.
				// When going up (Rise>0), travel == climb. When going down, invert.
				const int32 ClimbDirX = (Rise > 0) ? DirX : -DirX;
				const int32 ClimbDirY = (Rise > 0) ? DirY : -DirY;
				Staircase.Direction = static_cast<uint8>(
					(ClimbDirX == 1) ? 0 : (ClimbDirX == -1) ? 1 : (ClimbDirY == 1) ? 2 : 3);
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

				// Carve headroom cells above body (Z is up).
				// Claim empty and hallway cells as StaircaseHead to reserve the shaft
				// and prevent later staircases from overlapping.
				for (int32 s = 1; s <= RiseToRun; ++s)
				{
					for (int32 h = 1; h <= HeadroomCells; ++h)
					{
						const FIntVector HeadCell(Prev.X + DirX * s, Prev.Y + DirY * s, LowerZ + h);
						if (Grid.IsInBounds(HeadCell))
						{
							FDungeonCell& HeadCellRef = Grid.GetCell(HeadCell);
							if (HeadCellRef.CellType == EDungeonCellType::Empty
								|| HeadCellRef.CellType == EDungeonCellType::Hallway)
							{
								HeadCellRef.CellType = EDungeonCellType::StaircaseHead;
								HeadCellRef.HallwayIndex = HallwayIndex;
								HeadCellRef.StaircaseDirection = Staircase.Direction;
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

		// Check if an adjacent path cell belongs to the hallway family. Testing only for plain
		// Hallway left every connection that arrives via a staircase without a Door — and with no
		// Door, both FDungeonTileMapper and UDungeonVoxelStamper classify room↔hallway as
		// "different spaces" and wall the connection off, sealing the room from that corridor.
		auto IsHallwayFamily = [](EDungeonCellType Type)
		{
			return Type == EDungeonCellType::Hallway
				|| Type == EDungeonCellType::Staircase
				|| Type == EDungeonCellType::StaircaseHead;
		};

		const bool bPrevConnects = (i > 0 && Grid.IsInBounds(Path[i - 1]) &&
			IsHallwayFamily(Grid.GetCell(Path[i - 1]).CellType));
		const bool bNextConnects = (i < Path.Num() - 1 && Grid.IsInBounds(Path[i + 1]) &&
			IsHallwayFamily(Grid.GetCell(Path[i + 1]).CellType));

		if (bPrevConnects || bNextConnects)
		{
			Cell.CellType = EDungeonCellType::Door;
			Cell.HallwayIndex = HallwayIndex;
		}
	}
}
