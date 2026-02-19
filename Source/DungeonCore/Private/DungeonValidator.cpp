// DungeonValidator.cpp — Validates dungeon generation results for structural correctness
#include "DungeonValidator.h"
#include "DungeonConfig.h"

// ---------------------------------------------------------------------------
// FDungeonValidationResult
// ---------------------------------------------------------------------------

FString FDungeonValidationResult::GetSummary() const
{
	if (bPassed)
	{
		return TEXT("Validation passed");
	}

	FString Summary = FString::Printf(TEXT("Validation FAILED with %d issue(s):"), Issues.Num());
	for (const FDungeonValidationIssue& Issue : Issues)
	{
		Summary += FString::Printf(TEXT("\n  [%s] %s"), *Issue.Category, *Issue.Description);
	}
	return Summary;
}

// ---------------------------------------------------------------------------
// ValidateAll
// ---------------------------------------------------------------------------

FDungeonValidationResult FDungeonValidator::ValidateAll(const FDungeonResult& Result, const UDungeonConfiguration& Config)
{
	FDungeonValidationResult Validation;

	ValidateEntrance(Result, Validation.Issues);
	ValidateMetrics(Result, Validation.Issues);
	ValidateCellBounds(Result, Validation.Issues);
	ValidateNoRoomOverlap(Result, Validation.Issues);
	ValidateRoomBuffer(Result, Config, Validation.Issues);
	ValidateRoomConnectivity(Result, Validation.Issues);
	ValidateStaircaseHeadroom(Result, Validation.Issues);
	ValidateReachability(Result, Validation.Issues);
	ValidateRoomSemantics(Result, Config, Validation.Issues);

	Validation.bPassed = Validation.Issues.Num() == 0;
	return Validation;
}

// ---------------------------------------------------------------------------
// ValidateEntrance
// ---------------------------------------------------------------------------

void FDungeonValidator::ValidateEntrance(const FDungeonResult& Result, TArray<FDungeonValidationIssue>& OutIssues)
{
	if (Result.EntranceRoomIndex < 0)
	{
		OutIssues.Add(FDungeonValidationIssue(
			TEXT("Entrance"), TEXT("No entrance room designated (EntranceRoomIndex < 0)")));
		return;
	}

	if (Result.EntranceRoomIndex >= Result.Rooms.Num())
	{
		OutIssues.Add(FDungeonValidationIssue(
			TEXT("Entrance"),
			FString::Printf(TEXT("EntranceRoomIndex %d out of range (only %d rooms)"),
				Result.EntranceRoomIndex, Result.Rooms.Num())));
		return;
	}

	if (!Result.Grid.IsInBounds(Result.EntranceCell))
	{
		OutIssues.Add(FDungeonValidationIssue(
			TEXT("Entrance"),
			FString::Printf(TEXT("EntranceCell (%d,%d,%d) out of grid bounds"),
				Result.EntranceCell.X, Result.EntranceCell.Y, Result.EntranceCell.Z),
			Result.EntranceCell));
		return;
	}

	const FDungeonCell& Cell = Result.Grid.GetCell(Result.EntranceCell);
	if (Cell.CellType != EDungeonCellType::Entrance)
	{
		OutIssues.Add(FDungeonValidationIssue(
			TEXT("Entrance"),
			FString::Printf(TEXT("EntranceCell (%d,%d,%d) has type %d, expected Entrance (%d)"),
				Result.EntranceCell.X, Result.EntranceCell.Y, Result.EntranceCell.Z,
				static_cast<int32>(Cell.CellType), static_cast<int32>(EDungeonCellType::Entrance)),
			Result.EntranceCell));
	}
}

// ---------------------------------------------------------------------------
// ValidateMetrics
// ---------------------------------------------------------------------------

void FDungeonValidator::ValidateMetrics(const FDungeonResult& Result, TArray<FDungeonValidationIssue>& OutIssues)
{
	int32 RoomCells = 0;
	int32 HallwayCells = 0;
	int32 StaircaseCells = 0;

	for (const FDungeonCell& Cell : Result.Grid.Cells)
	{
		switch (Cell.CellType)
		{
		case EDungeonCellType::Room:
		case EDungeonCellType::Door:
		case EDungeonCellType::Entrance:
			RoomCells++;
			break;
		case EDungeonCellType::Hallway:
			HallwayCells++;
			break;
		case EDungeonCellType::Staircase:
		case EDungeonCellType::StaircaseHead:
			StaircaseCells++;
			break;
		default:
			break;
		}
	}

	if (RoomCells != Result.TotalRoomCells)
	{
		OutIssues.Add(FDungeonValidationIssue(
			TEXT("Metrics"),
			FString::Printf(TEXT("TotalRoomCells mismatch: reported %d, actual %d"),
				Result.TotalRoomCells, RoomCells)));
	}
	if (HallwayCells != Result.TotalHallwayCells)
	{
		OutIssues.Add(FDungeonValidationIssue(
			TEXT("Metrics"),
			FString::Printf(TEXT("TotalHallwayCells mismatch: reported %d, actual %d"),
				Result.TotalHallwayCells, HallwayCells)));
	}
	if (StaircaseCells != Result.TotalStaircaseCells)
	{
		OutIssues.Add(FDungeonValidationIssue(
			TEXT("Metrics"),
			FString::Printf(TEXT("TotalStaircaseCells mismatch: reported %d, actual %d"),
				Result.TotalStaircaseCells, StaircaseCells)));
	}
}

// ---------------------------------------------------------------------------
// ValidateCellBounds
// ---------------------------------------------------------------------------

void FDungeonValidator::ValidateCellBounds(const FDungeonResult& Result, TArray<FDungeonValidationIssue>& OutIssues)
{
	for (int32 i = 0; i < Result.Rooms.Num(); ++i)
	{
		const FDungeonRoom& Room = Result.Rooms[i];

		if (!Result.Grid.IsInBounds(Room.Position))
		{
			OutIssues.Add(FDungeonValidationIssue(
				TEXT("Bounds"),
				FString::Printf(TEXT("Room %d origin (%d,%d,%d) out of bounds"),
					i, Room.Position.X, Room.Position.Y, Room.Position.Z),
				Room.Position, i));
		}

		const FIntVector MaxCorner = Room.Position + Room.Size - FIntVector(1, 1, 1);
		if (!Result.Grid.IsInBounds(MaxCorner))
		{
			OutIssues.Add(FDungeonValidationIssue(
				TEXT("Bounds"),
				FString::Printf(TEXT("Room %d max corner (%d,%d,%d) out of bounds (size %d,%d,%d from %d,%d,%d)"),
					i, MaxCorner.X, MaxCorner.Y, MaxCorner.Z,
					Room.Size.X, Room.Size.Y, Room.Size.Z,
					Room.Position.X, Room.Position.Y, Room.Position.Z),
				MaxCorner, i));
		}
	}

	for (int32 i = 0; i < Result.Staircases.Num(); ++i)
	{
		const FDungeonStaircase& Staircase = Result.Staircases[i];
		for (const FIntVector& Cell : Staircase.OccupiedCells)
		{
			if (!Result.Grid.IsInBounds(Cell))
			{
				OutIssues.Add(FDungeonValidationIssue(
					TEXT("Bounds"),
					FString::Printf(TEXT("Staircase %d occupied cell (%d,%d,%d) out of bounds"),
						i, Cell.X, Cell.Y, Cell.Z),
					Cell));
			}
		}
	}
}

// ---------------------------------------------------------------------------
// ValidateNoRoomOverlap
// ---------------------------------------------------------------------------

void FDungeonValidator::ValidateNoRoomOverlap(const FDungeonResult& Result, TArray<FDungeonValidationIssue>& OutIssues)
{
	for (int32 i = 0; i < Result.Rooms.Num(); ++i)
	{
		for (int32 j = i + 1; j < Result.Rooms.Num(); ++j)
		{
			const FDungeonRoom& A = Result.Rooms[i];
			const FDungeonRoom& B = Result.Rooms[j];

			const bool bOverlapX = (A.Position.X < B.Position.X + B.Size.X) && (B.Position.X < A.Position.X + A.Size.X);
			const bool bOverlapY = (A.Position.Y < B.Position.Y + B.Size.Y) && (B.Position.Y < A.Position.Y + A.Size.Y);
			const bool bOverlapZ = (A.Position.Z < B.Position.Z + B.Size.Z) && (B.Position.Z < A.Position.Z + A.Size.Z);

			if (bOverlapX && bOverlapY && bOverlapZ)
			{
				OutIssues.Add(FDungeonValidationIssue(
					TEXT("Overlap"),
					FString::Printf(TEXT("Room %d (%d,%d,%d size %d,%d,%d) and Room %d (%d,%d,%d size %d,%d,%d) AABBs overlap"),
						i, A.Position.X, A.Position.Y, A.Position.Z, A.Size.X, A.Size.Y, A.Size.Z,
						j, B.Position.X, B.Position.Y, B.Position.Z, B.Size.X, B.Size.Y, B.Size.Z),
					A.Position, i));
			}
		}
	}
}

// ---------------------------------------------------------------------------
// ValidateRoomBuffer
// ---------------------------------------------------------------------------

void FDungeonValidator::ValidateRoomBuffer(const FDungeonResult& Result, const UDungeonConfiguration& Config, TArray<FDungeonValidationIssue>& OutIssues)
{
	const int32 Buffer = Config.RoomBuffer;
	if (Buffer <= 0)
	{
		return; // No buffer to enforce
	}

	for (int32 i = 0; i < Result.Rooms.Num(); ++i)
	{
		for (int32 j = i + 1; j < Result.Rooms.Num(); ++j)
		{
			const FDungeonRoom& A = Result.Rooms[i];
			const FDungeonRoom& B = Result.Rooms[j];

			// Expand AABBs by buffer on X/Y only (skip Z, matching RoomPlacement convention)
			const bool bOverlapX = (A.Position.X - Buffer < B.Position.X + B.Size.X) && (B.Position.X - Buffer < A.Position.X + A.Size.X);
			const bool bOverlapY = (A.Position.Y - Buffer < B.Position.Y + B.Size.Y) && (B.Position.Y - Buffer < A.Position.Y + A.Size.Y);
			const bool bOverlapZ = (A.Position.Z < B.Position.Z + B.Size.Z) && (B.Position.Z < A.Position.Z + A.Size.Z);

			if (bOverlapX && bOverlapY && bOverlapZ)
			{
				OutIssues.Add(FDungeonValidationIssue(
					TEXT("Buffer"),
					FString::Printf(TEXT("Room %d and Room %d violate buffer distance of %d cells"),
						i, j, Buffer),
					A.Position, i));
			}
		}
	}
}

// ---------------------------------------------------------------------------
// ValidateRoomConnectivity
// ---------------------------------------------------------------------------

void FDungeonValidator::ValidateRoomConnectivity(const FDungeonResult& Result, TArray<FDungeonValidationIssue>& OutIssues)
{
	if (Result.Rooms.Num() <= 1)
	{
		return;
	}
	if (Result.EntranceRoomIndex < 0 || Result.EntranceRoomIndex >= Result.Rooms.Num())
	{
		return; // Entrance validation handles this
	}

	// Build adjacency from hallways
	TMultiMap<int32, int32> Adjacency;
	for (const FDungeonHallway& Hallway : Result.Hallways)
	{
		Adjacency.Add(static_cast<int32>(Hallway.RoomA), static_cast<int32>(Hallway.RoomB));
		Adjacency.Add(static_cast<int32>(Hallway.RoomB), static_cast<int32>(Hallway.RoomA));
	}

	// BFS from entrance room
	TSet<int32> Visited;
	TArray<int32> Queue;
	Queue.Add(Result.EntranceRoomIndex);
	Visited.Add(Result.EntranceRoomIndex);

	int32 QueueHead = 0;
	while (QueueHead < Queue.Num())
	{
		const int32 Current = Queue[QueueHead++];

		TArray<int32> Neighbors;
		Adjacency.MultiFind(Current, Neighbors);

		for (const int32 Neighbor : Neighbors)
		{
			if (!Visited.Contains(Neighbor))
			{
				Visited.Add(Neighbor);
				Queue.Add(Neighbor);
			}
		}
	}

	// Report unreached rooms
	for (int32 i = 0; i < Result.Rooms.Num(); ++i)
	{
		if (!Visited.Contains(i))
		{
			OutIssues.Add(FDungeonValidationIssue(
				TEXT("Connectivity"),
				FString::Printf(TEXT("Room %d is not connected to entrance via hallways"), i),
				Result.Rooms[i].Position, i));
		}
	}
}

// ---------------------------------------------------------------------------
// ValidateStaircaseHeadroom
// ---------------------------------------------------------------------------

void FDungeonValidator::ValidateStaircaseHeadroom(const FDungeonResult& Result, TArray<FDungeonValidationIssue>& OutIssues)
{
	for (int32 i = 0; i < Result.Staircases.Num(); ++i)
	{
		const FDungeonStaircase& Staircase = Result.Staircases[i];

		for (const FIntVector& Cell : Staircase.OccupiedCells)
		{
			// Cells above BottomCell.Z should be Staircase or StaircaseHead, not Room/RoomWall
			if (Cell.Z > Staircase.BottomCell.Z && Result.Grid.IsInBounds(Cell))
			{
				const FDungeonCell& GridCell = Result.Grid.GetCell(Cell);
				if (GridCell.CellType == EDungeonCellType::Room ||
					GridCell.CellType == EDungeonCellType::RoomWall)
				{
					OutIssues.Add(FDungeonValidationIssue(
						TEXT("Headroom"),
						FString::Printf(TEXT("Staircase %d: cell (%d,%d,%d) above bottom has type %d (Room/RoomWall), expected Staircase/StaircaseHead"),
							i, Cell.X, Cell.Y, Cell.Z, static_cast<int32>(GridCell.CellType)),
						Cell));
				}
			}
		}
	}
}

// ---------------------------------------------------------------------------
// ValidateRoomSemantics
// ---------------------------------------------------------------------------

void FDungeonValidator::ValidateRoomSemantics(const FDungeonResult& Result, const UDungeonConfiguration& Config, TArray<FDungeonValidationIssue>& OutIssues)
{
	// 1. Exactly one Entrance room exists
	int32 EntranceCount = 0;
	for (int32 i = 0; i < Result.Rooms.Num(); ++i)
	{
		if (Result.Rooms[i].RoomType == EDungeonRoomType::Entrance)
		{
			EntranceCount++;
		}
	}

	if (EntranceCount == 0)
	{
		OutIssues.Add(FDungeonValidationIssue(
			TEXT("Semantics"), TEXT("No room has RoomType=Entrance")));
	}
	else if (EntranceCount > 1)
	{
		OutIssues.Add(FDungeonValidationIssue(
			TEXT("Semantics"),
			FString::Printf(TEXT("Multiple entrance rooms found: %d (expected 1)"), EntranceCount)));
	}

	// 2. Boss guarantee
	if (Config.bGuaranteeBossRoom && Result.Rooms.Num() > 1)
	{
		bool bHasBoss = false;
		for (const FDungeonRoom& Room : Result.Rooms)
		{
			if (Room.RoomType == EDungeonRoomType::Boss)
			{
				bHasBoss = true;
				break;
			}
		}

		if (!bHasBoss)
		{
			OutIssues.Add(FDungeonValidationIssue(
				TEXT("Semantics"), TEXT("bGuaranteeBossRoom is true but no Boss room was assigned")));
		}
	}

	// 3. Rule count limits — no over-assignment
	for (const FDungeonRoomTypeRule& Rule : Config.RoomTypeRules)
	{
		if (Rule.RoomType == EDungeonRoomType::Entrance)
		{
			continue; // Entrance is handled separately
		}

		int32 TypeCount = 0;
		for (const FDungeonRoom& Room : Result.Rooms)
		{
			if (Room.RoomType == Rule.RoomType)
			{
				TypeCount++;
			}
		}

		if (TypeCount > Rule.Count)
		{
			OutIssues.Add(FDungeonValidationIssue(
				TEXT("Semantics"),
				FString::Printf(TEXT("Room type %d has %d rooms but rule allows max %d"),
					static_cast<int32>(Rule.RoomType), TypeCount, Rule.Count)));
		}
	}

	// 4. Entrance room's GraphDistanceFromEntrance == 0
	if (Result.EntranceRoomIndex >= 0 && Result.EntranceRoomIndex < Result.Rooms.Num())
	{
		if (Result.Rooms[Result.EntranceRoomIndex].GraphDistanceFromEntrance != 0)
		{
			OutIssues.Add(FDungeonValidationIssue(
				TEXT("Semantics"),
				FString::Printf(TEXT("Entrance room %d has GraphDistanceFromEntrance=%d (expected 0)"),
					Result.EntranceRoomIndex, Result.Rooms[Result.EntranceRoomIndex].GraphDistanceFromEntrance)));
		}
	}
}

// ---------------------------------------------------------------------------
// ValidateReachability
// ---------------------------------------------------------------------------

void FDungeonValidator::ValidateReachability(const FDungeonResult& Result, TArray<FDungeonValidationIssue>& OutIssues)
{
	if (!Result.Grid.IsInBounds(Result.EntranceCell))
	{
		return; // Entrance validation handles this
	}

	const FDungeonCell& StartCell = Result.Grid.GetCell(Result.EntranceCell);
	if (StartCell.CellType == EDungeonCellType::Empty)
	{
		return; // Entrance validation handles this
	}

	TSet<int32> Visited;
	FloodFill(Result.Grid, Result.EntranceCell, Visited);

	// Check all non-empty cells were visited
	const FIntVector& GS = Result.Grid.GridSize;
	for (int32 Z = 0; Z < GS.Z; ++Z)
	{
		for (int32 Y = 0; Y < GS.Y; ++Y)
		{
			for (int32 X = 0; X < GS.X; ++X)
			{
				const int32 Idx = Result.Grid.CellIndex(X, Y, Z);
				if (Result.Grid.Cells[Idx].CellType != EDungeonCellType::Empty)
				{
					if (!Visited.Contains(Idx))
					{
						OutIssues.Add(FDungeonValidationIssue(
							TEXT("Reachability"),
							FString::Printf(TEXT("Cell (%d,%d,%d) type %d is not reachable from entrance"),
								X, Y, Z, static_cast<int32>(Result.Grid.Cells[Idx].CellType)),
							FIntVector(X, Y, Z)));
					}
				}
			}
		}
	}
}

// ---------------------------------------------------------------------------
// FloodFill (private helper)
// ---------------------------------------------------------------------------

void FDungeonValidator::FloodFill(const FDungeonGrid& Grid, const FIntVector& Start, TSet<int32>& VisitedIndices)
{
	if (!Grid.IsInBounds(Start))
	{
		return;
	}

	const int32 StartIdx = Grid.CellIndex(Start);
	if (Grid.Cells[StartIdx].CellType == EDungeonCellType::Empty)
	{
		return;
	}

	TArray<FIntVector> Stack;
	Stack.Reserve(Grid.Cells.Num() / 4);
	Stack.Push(Start);
	VisitedIndices.Add(StartIdx);

	static const FIntVector Directions[] =
	{
		FIntVector( 1,  0,  0),
		FIntVector(-1,  0,  0),
		FIntVector( 0,  1,  0),
		FIntVector( 0, -1,  0),
		FIntVector( 0,  0,  1),
		FIntVector( 0,  0, -1),
	};

	while (Stack.Num() > 0)
	{
		const FIntVector Current = Stack.Pop();

		for (const FIntVector& Dir : Directions)
		{
			const FIntVector Neighbor = Current + Dir;
			if (!Grid.IsInBounds(Neighbor))
			{
				continue;
			}

			const int32 NeighborIdx = Grid.CellIndex(Neighbor);
			if (VisitedIndices.Contains(NeighborIdx))
			{
				continue;
			}
			if (Grid.Cells[NeighborIdx].CellType == EDungeonCellType::Empty)
			{
				continue;
			}

			VisitedIndices.Add(NeighborIdx);
			Stack.Push(Neighbor);
		}
	}
}
