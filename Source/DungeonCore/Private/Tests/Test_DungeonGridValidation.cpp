// Test_DungeonGridValidation.cpp — Unit tests for FDungeonValidator with hand-built grids
#include "Misc/AutomationTest.h"
#include "DungeonTypes.h"
#include "DungeonConfig.h"
#include "DungeonValidator.h"

// ============================================================================
// Test Helpers
// ============================================================================

namespace DungeonValidationTestHelpers
{
	UDungeonConfiguration* CreateTestConfig()
	{
		UDungeonConfiguration* Config = NewObject<UDungeonConfiguration>();
		Config->AddToRoot();
		Config->GridSize = FIntVector(10, 10, 1);
		Config->RoomBuffer = 1;
		Config->RoomCount = 2;
		Config->MinRoomSize = FIntVector(3, 3, 1);
		Config->MaxRoomSize = FIntVector(5, 5, 1);
		return Config;
	}

	/**
	 * Create a simple valid result: 10x10x1 grid, 2 rooms, 1 hallway, entrance marked.
	 * Room 0: (0,0,0) 3x3, entrance at (1,1,0)
	 * Room 1: (6,6,0) 3x3
	 * Hallway: L-shaped from room 0 to room 1
	 */
	FDungeonResult CreateSimpleResult()
	{
		FDungeonResult Result;
		Result.GridSize = FIntVector(10, 10, 1);
		Result.Grid.Initialize(FIntVector(10, 10, 1));
		Result.CellWorldSize = 400.0f;
		Result.Seed = 12345;

		// Room 0
		FDungeonRoom Room0;
		Room0.RoomIndex = 1;
		Room0.RoomType = EDungeonRoomType::Entrance;
		Room0.Position = FIntVector(0, 0, 0);
		Room0.Size = FIntVector(3, 3, 1);
		Room0.Center = FIntVector(1, 1, 0);
		Room0.FloorLevel = 0;

		// Room 1
		FDungeonRoom Room1;
		Room1.RoomIndex = 2;
		Room1.RoomType = EDungeonRoomType::Generic;
		Room1.Position = FIntVector(6, 6, 0);
		Room1.Size = FIntVector(3, 3, 1);
		Room1.Center = FIntVector(7, 7, 0);
		Room1.FloorLevel = 0;

		Result.Rooms.Add(Room0);
		Result.Rooms.Add(Room1);
		Result.EntranceRoomIndex = 0;
		Result.EntranceCell = FIntVector(1, 1, 0);

		// Stamp Room 0: (0,0,0) to (2,2,0)
		for (int32 Y = 0; Y <= 2; ++Y)
		{
			for (int32 X = 0; X <= 2; ++X)
			{
				FDungeonCell& Cell = Result.Grid.GetCell(X, Y, 0);
				const bool bBoundary = (X == 0 || X == 2 || Y == 0 || Y == 2);
				Cell.CellType = bBoundary ? EDungeonCellType::RoomWall : EDungeonCellType::Room;
				Cell.RoomIndex = 1;
			}
		}
		// Mark entrance
		Result.Grid.GetCell(1, 1, 0).CellType = EDungeonCellType::Entrance;
		// Door on east wall
		Result.Grid.GetCell(2, 1, 0).CellType = EDungeonCellType::Door;

		// Stamp Room 1: (6,6,0) to (8,8,0)
		for (int32 Y = 6; Y <= 8; ++Y)
		{
			for (int32 X = 6; X <= 8; ++X)
			{
				FDungeonCell& Cell = Result.Grid.GetCell(X, Y, 0);
				const bool bBoundary = (X == 6 || X == 8 || Y == 6 || Y == 8);
				Cell.CellType = bBoundary ? EDungeonCellType::RoomWall : EDungeonCellType::Room;
				Cell.RoomIndex = 2;
			}
		}
		// Door on west wall
		Result.Grid.GetCell(6, 7, 0).CellType = EDungeonCellType::Door;

		// Hallway: (3,1) → (5,1) then (5,2) → (5,7)
		for (int32 X = 3; X <= 5; ++X)
		{
			FDungeonCell& Cell = Result.Grid.GetCell(X, 1, 0);
			Cell.CellType = EDungeonCellType::Hallway;
			Cell.HallwayIndex = 1;
		}
		for (int32 Y = 2; Y <= 7; ++Y)
		{
			FDungeonCell& Cell = Result.Grid.GetCell(5, Y, 0);
			Cell.CellType = EDungeonCellType::Hallway;
			Cell.HallwayIndex = 1;
		}

		// Hallway struct
		FDungeonHallway Hallway;
		Hallway.HallwayIndex = 1;
		Hallway.RoomA = 0;
		Hallway.RoomB = 1;
		Hallway.bIsFromMST = true;
		Hallway.bHasStaircase = false;
		Result.Hallways.Add(MoveTemp(Hallway));

		// Connectivity
		Result.Rooms[0].ConnectedRoomIndices.Add(1);
		Result.Rooms[1].ConnectedRoomIndices.Add(0);

		// Metrics: Room + Door + Entrance = TotalRoomCells
		// Room 0: 1 Entrance + 1 Door = 2 (RoomWall not counted)
		// Room 1: 1 Room + 1 Door = 2
		// Hallway: 3 horizontal + 6 vertical = 9
		Result.TotalRoomCells = 4;
		Result.TotalHallwayCells = 9;
		Result.TotalStaircaseCells = 0;

		return Result;
	}

	void CleanupConfig(UDungeonConfiguration* Config)
	{
		if (Config)
		{
			Config->RemoveFromRoot();
		}
	}
}

// ============================================================================
// BOUNDS TESTS
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonValidateBoundsValid, "Dungeon.GridValidation.Bounds.ValidGridPasses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonValidateBoundsValid::RunTest(const FString& Parameters)
{
	FDungeonResult Result = DungeonValidationTestHelpers::CreateSimpleResult();

	TArray<FDungeonValidationIssue> Issues;
	FDungeonValidator::ValidateCellBounds(Result, Issues);
	TestEqual(TEXT("No bounds issues"), Issues.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonValidateBoundsExceeding, "Dungeon.GridValidation.Bounds.RoomExceedingGridDetected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonValidateBoundsExceeding::RunTest(const FString& Parameters)
{
	FDungeonResult Result = DungeonValidationTestHelpers::CreateSimpleResult();
	// Push room 1 out of bounds
	Result.Rooms[1].Position = FIntVector(8, 8, 0);
	Result.Rooms[1].Size = FIntVector(5, 5, 1); // Extends to (12,12,0) — well beyond 10x10

	TArray<FDungeonValidationIssue> Issues;
	FDungeonValidator::ValidateCellBounds(Result, Issues);
	TestTrue(TEXT("Out-of-bounds room detected"), Issues.Num() > 0);

	return true;
}

// ============================================================================
// OVERLAP TESTS
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonValidateOverlapNone, "Dungeon.GridValidation.Overlap.NoOverlapPasses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonValidateOverlapNone::RunTest(const FString& Parameters)
{
	FDungeonResult Result = DungeonValidationTestHelpers::CreateSimpleResult();

	TArray<FDungeonValidationIssue> Issues;
	FDungeonValidator::ValidateNoRoomOverlap(Result, Issues);
	TestEqual(TEXT("No overlap issues"), Issues.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonValidateOverlapDetected, "Dungeon.GridValidation.Overlap.OverlappingRoomsDetected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonValidateOverlapDetected::RunTest(const FString& Parameters)
{
	FDungeonResult Result = DungeonValidationTestHelpers::CreateSimpleResult();
	// Move room 1 to overlap room 0
	Result.Rooms[1].Position = FIntVector(1, 1, 0);
	Result.Rooms[1].Size = FIntVector(3, 3, 1);

	TArray<FDungeonValidationIssue> Issues;
	FDungeonValidator::ValidateNoRoomOverlap(Result, Issues);
	TestTrue(TEXT("Overlap detected"), Issues.Num() > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonValidateOverlapAdjacent, "Dungeon.GridValidation.Overlap.AdjacentRoomsOK",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonValidateOverlapAdjacent::RunTest(const FString& Parameters)
{
	FDungeonResult Result = DungeonValidationTestHelpers::CreateSimpleResult();
	// Place room 1 immediately adjacent (touching but not overlapping)
	Result.Rooms[1].Position = FIntVector(3, 0, 0);
	Result.Rooms[1].Size = FIntVector(3, 3, 1);

	TArray<FDungeonValidationIssue> Issues;
	FDungeonValidator::ValidateNoRoomOverlap(Result, Issues);
	TestEqual(TEXT("Adjacent rooms not flagged as overlap"), Issues.Num(), 0);

	return true;
}

// ============================================================================
// BUFFER TESTS
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonValidateBufferMaintained, "Dungeon.GridValidation.Buffer.MaintainedPasses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonValidateBufferMaintained::RunTest(const FString& Parameters)
{
	FDungeonResult Result = DungeonValidationTestHelpers::CreateSimpleResult();
	UDungeonConfiguration* Config = DungeonValidationTestHelpers::CreateTestConfig();
	Config->RoomBuffer = 1;

	TArray<FDungeonValidationIssue> Issues;
	FDungeonValidator::ValidateRoomBuffer(Result, *Config, Issues);
	TestEqual(TEXT("Buffer maintained"), Issues.Num(), 0);

	DungeonValidationTestHelpers::CleanupConfig(Config);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonValidateBufferViolation, "Dungeon.GridValidation.Buffer.ViolationDetected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonValidateBufferViolation::RunTest(const FString& Parameters)
{
	FDungeonResult Result = DungeonValidationTestHelpers::CreateSimpleResult();
	// Place room 1 adjacent to room 0 (no gap) — violates buffer of 1
	Result.Rooms[1].Position = FIntVector(3, 0, 0);
	Result.Rooms[1].Size = FIntVector(3, 3, 1);

	UDungeonConfiguration* Config = DungeonValidationTestHelpers::CreateTestConfig();
	Config->RoomBuffer = 1;

	TArray<FDungeonValidationIssue> Issues;
	FDungeonValidator::ValidateRoomBuffer(Result, *Config, Issues);
	TestTrue(TEXT("Buffer violation detected"), Issues.Num() > 0);

	DungeonValidationTestHelpers::CleanupConfig(Config);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonValidateBufferZeroAllowed, "Dungeon.GridValidation.Buffer.ZeroBufferAllowed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonValidateBufferZeroAllowed::RunTest(const FString& Parameters)
{
	FDungeonResult Result = DungeonValidationTestHelpers::CreateSimpleResult();
	// Place rooms adjacent
	Result.Rooms[1].Position = FIntVector(3, 0, 0);
	Result.Rooms[1].Size = FIntVector(3, 3, 1);

	UDungeonConfiguration* Config = DungeonValidationTestHelpers::CreateTestConfig();
	Config->RoomBuffer = 0;

	TArray<FDungeonValidationIssue> Issues;
	FDungeonValidator::ValidateRoomBuffer(Result, *Config, Issues);
	TestEqual(TEXT("Zero buffer allows adjacent rooms"), Issues.Num(), 0);

	DungeonValidationTestHelpers::CleanupConfig(Config);
	return true;
}

// ============================================================================
// REACHABILITY TESTS
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonValidateReachAll, "Dungeon.GridValidation.Reachability.AllReachablePasses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonValidateReachAll::RunTest(const FString& Parameters)
{
	FDungeonResult Result = DungeonValidationTestHelpers::CreateSimpleResult();

	TArray<FDungeonValidationIssue> Issues;
	FDungeonValidator::ValidateReachability(Result, Issues);
	TestEqual(TEXT("All cells reachable"), Issues.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonValidateReachOrphanRoom, "Dungeon.GridValidation.Reachability.OrphanedRoomDetected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonValidateReachOrphanRoom::RunTest(const FString& Parameters)
{
	FDungeonResult Result = DungeonValidationTestHelpers::CreateSimpleResult();

	// Add an isolated room with no hallway connection
	// Place it at (0,7,0) — disconnected from everything
	for (int32 Y = 7; Y <= 9; ++Y)
	{
		for (int32 X = 0; X <= 2; ++X)
		{
			FDungeonCell& Cell = Result.Grid.GetCell(X, Y, 0);
			const bool bBoundary = (X == 0 || X == 2 || Y == 7 || Y == 9);
			Cell.CellType = bBoundary ? EDungeonCellType::RoomWall : EDungeonCellType::Room;
			Cell.RoomIndex = 3;
		}
	}

	TArray<FDungeonValidationIssue> Issues;
	FDungeonValidator::ValidateReachability(Result, Issues);
	TestTrue(TEXT("Orphaned room cells detected as unreachable"), Issues.Num() > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonValidateReachOrphanHallway, "Dungeon.GridValidation.Reachability.OrphanedHallwayDetected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonValidateReachOrphanHallway::RunTest(const FString& Parameters)
{
	FDungeonResult Result = DungeonValidationTestHelpers::CreateSimpleResult();

	// Add disconnected hallway cells in an unused area
	Result.Grid.GetCell(9, 0, 0).CellType = EDungeonCellType::Hallway;
	Result.Grid.GetCell(9, 1, 0).CellType = EDungeonCellType::Hallway;

	TArray<FDungeonValidationIssue> Issues;
	FDungeonValidator::ValidateReachability(Result, Issues);
	TestTrue(TEXT("Orphaned hallway cells detected"), Issues.Num() > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonValidateReachMultiFloor, "Dungeon.GridValidation.Reachability.MultiFloorWithStairs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonValidateReachMultiFloor::RunTest(const FString& Parameters)
{
	// 10x10x2 grid with rooms on different floors connected via staircase
	FDungeonResult Result;
	Result.GridSize = FIntVector(10, 10, 2);
	Result.Grid.Initialize(FIntVector(10, 10, 2));
	Result.CellWorldSize = 400.0f;
	Result.Seed = 99;

	// Room 0: floor 0 at (0,0,0) 3x3
	FDungeonRoom Room0;
	Room0.RoomIndex = 1;
	Room0.RoomType = EDungeonRoomType::Entrance;
	Room0.Position = FIntVector(0, 0, 0);
	Room0.Size = FIntVector(3, 3, 1);
	Room0.Center = FIntVector(1, 1, 0);
	Room0.FloorLevel = 0;
	Result.Rooms.Add(Room0);

	// Room 1: floor 1 at (7, 0, 1) 3x3
	FDungeonRoom Room1;
	Room1.RoomIndex = 2;
	Room1.RoomType = EDungeonRoomType::Generic;
	Room1.Position = FIntVector(7, 0, 1);
	Room1.Size = FIntVector(3, 3, 1);
	Room1.Center = FIntVector(8, 1, 1);
	Room1.FloorLevel = 1;
	Result.Rooms.Add(Room1);

	Result.EntranceRoomIndex = 0;
	Result.EntranceCell = FIntVector(1, 1, 0);

	// Stamp Room 0
	for (int32 Y = 0; Y <= 2; ++Y)
	{
		for (int32 X = 0; X <= 2; ++X)
		{
			FDungeonCell& Cell = Result.Grid.GetCell(X, Y, 0);
			const bool bBoundary = (X == 0 || X == 2 || Y == 0 || Y == 2);
			Cell.CellType = bBoundary ? EDungeonCellType::RoomWall : EDungeonCellType::Room;
			Cell.RoomIndex = 1;
		}
	}
	Result.Grid.GetCell(1, 1, 0).CellType = EDungeonCellType::Entrance;
	Result.Grid.GetCell(2, 1, 0).CellType = EDungeonCellType::Door;

	// Stamp Room 1
	for (int32 Y = 0; Y <= 2; ++Y)
	{
		for (int32 X = 7; X <= 9; ++X)
		{
			FDungeonCell& Cell = Result.Grid.GetCell(X, Y, 1);
			const bool bBoundary = (X == 7 || X == 9 || Y == 0 || Y == 2);
			Cell.CellType = bBoundary ? EDungeonCellType::RoomWall : EDungeonCellType::Room;
			Cell.RoomIndex = 2;
		}
	}
	Result.Grid.GetCell(7, 1, 1).CellType = EDungeonCellType::Door;

	// Hallway on floor 0: (3,1,0) to (5,1,0)
	for (int32 X = 3; X <= 5; ++X)
	{
		Result.Grid.GetCell(X, 1, 0).CellType = EDungeonCellType::Hallway;
	}

	// Staircase connecting floors: (5,1,0) → (5,1,1) vertical transition
	Result.Grid.GetCell(5, 1, 0).CellType = EDungeonCellType::Staircase;
	Result.Grid.GetCell(5, 1, 1).CellType = EDungeonCellType::Staircase;

	// Hallway on floor 1: (6,1,1)
	Result.Grid.GetCell(6, 1, 1).CellType = EDungeonCellType::Hallway;

	TArray<FDungeonValidationIssue> Issues;
	FDungeonValidator::ValidateReachability(Result, Issues);
	TestEqual(TEXT("Multi-floor with stairs: all reachable"), Issues.Num(), 0);

	return true;
}

// ============================================================================
// HEADROOM TESTS
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonValidateHeadroomClear, "Dungeon.GridValidation.Headroom.ClearPasses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonValidateHeadroomClear::RunTest(const FString& Parameters)
{
	// 10x10x3 grid with a proper staircase
	FDungeonResult Result;
	Result.GridSize = FIntVector(10, 10, 3);
	Result.Grid.Initialize(FIntVector(10, 10, 3));
	Result.EntranceRoomIndex = 0;
	Result.EntranceCell = FIntVector(1, 1, 0);

	// Mark staircase cells
	Result.Grid.GetCell(5, 1, 0).CellType = EDungeonCellType::Staircase;
	Result.Grid.GetCell(5, 1, 1).CellType = EDungeonCellType::StaircaseHead;
	Result.Grid.GetCell(5, 1, 2).CellType = EDungeonCellType::StaircaseHead;

	FDungeonStaircase Staircase;
	Staircase.BottomCell = FIntVector(5, 1, 0);
	Staircase.TopCell = FIntVector(5, 1, 2);
	Staircase.OccupiedCells.Add(FIntVector(5, 1, 0));
	Staircase.OccupiedCells.Add(FIntVector(5, 1, 1));
	Staircase.OccupiedCells.Add(FIntVector(5, 1, 2));
	Result.Staircases.Add(MoveTemp(Staircase));

	TArray<FDungeonValidationIssue> Issues;
	FDungeonValidator::ValidateStaircaseHeadroom(Result, Issues);
	TestEqual(TEXT("Clear headroom passes"), Issues.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonValidateHeadroomBlocked, "Dungeon.GridValidation.Headroom.BlockedDetected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonValidateHeadroomBlocked::RunTest(const FString& Parameters)
{
	// 10x10x3 grid with a room blocking staircase headroom
	FDungeonResult Result;
	Result.GridSize = FIntVector(10, 10, 3);
	Result.Grid.Initialize(FIntVector(10, 10, 3));
	Result.EntranceRoomIndex = 0;
	Result.EntranceCell = FIntVector(1, 1, 0);

	// Staircase body on floor 0
	Result.Grid.GetCell(5, 1, 0).CellType = EDungeonCellType::Staircase;
	// Room blocking headroom on floor 1
	Result.Grid.GetCell(5, 1, 1).CellType = EDungeonCellType::Room;
	Result.Grid.GetCell(5, 1, 1).RoomIndex = 1;

	FDungeonStaircase Staircase;
	Staircase.BottomCell = FIntVector(5, 1, 0);
	Staircase.TopCell = FIntVector(5, 1, 1);
	Staircase.OccupiedCells.Add(FIntVector(5, 1, 0));
	Staircase.OccupiedCells.Add(FIntVector(5, 1, 1));
	Result.Staircases.Add(MoveTemp(Staircase));

	TArray<FDungeonValidationIssue> Issues;
	FDungeonValidator::ValidateStaircaseHeadroom(Result, Issues);
	TestTrue(TEXT("Blocked headroom detected"), Issues.Num() > 0);

	return true;
}

// ============================================================================
// CONNECTIVITY TESTS
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonValidateConnectAll, "Dungeon.GridValidation.Connectivity.AllConnectedPasses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonValidateConnectAll::RunTest(const FString& Parameters)
{
	FDungeonResult Result = DungeonValidationTestHelpers::CreateSimpleResult();

	TArray<FDungeonValidationIssue> Issues;
	FDungeonValidator::ValidateRoomConnectivity(Result, Issues);
	TestEqual(TEXT("All rooms connected"), Issues.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonValidateConnectDisconnected, "Dungeon.GridValidation.Connectivity.DisconnectedRoomDetected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonValidateConnectDisconnected::RunTest(const FString& Parameters)
{
	FDungeonResult Result = DungeonValidationTestHelpers::CreateSimpleResult();

	// Add a third room with no hallway
	FDungeonRoom Room2;
	Room2.RoomIndex = 3;
	Room2.RoomType = EDungeonRoomType::Generic;
	Room2.Position = FIntVector(0, 7, 0);
	Room2.Size = FIntVector(3, 3, 1);
	Room2.Center = FIntVector(1, 8, 0);
	Room2.FloorLevel = 0;
	Result.Rooms.Add(Room2);

	TArray<FDungeonValidationIssue> Issues;
	FDungeonValidator::ValidateRoomConnectivity(Result, Issues);
	TestTrue(TEXT("Disconnected room detected"), Issues.Num() > 0);

	return true;
}

// ============================================================================
// ENTRANCE TESTS
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonValidateEntranceValid, "Dungeon.GridValidation.Entrance.ValidPasses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonValidateEntranceValid::RunTest(const FString& Parameters)
{
	FDungeonResult Result = DungeonValidationTestHelpers::CreateSimpleResult();

	TArray<FDungeonValidationIssue> Issues;
	FDungeonValidator::ValidateEntrance(Result, Issues);
	TestEqual(TEXT("Valid entrance passes"), Issues.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonValidateEntranceWrongType, "Dungeon.GridValidation.Entrance.WrongTypeDetected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonValidateEntranceWrongType::RunTest(const FString& Parameters)
{
	FDungeonResult Result = DungeonValidationTestHelpers::CreateSimpleResult();
	// Change entrance cell type to Room instead of Entrance
	Result.Grid.GetCell(1, 1, 0).CellType = EDungeonCellType::Room;

	TArray<FDungeonValidationIssue> Issues;
	FDungeonValidator::ValidateEntrance(Result, Issues);
	TestTrue(TEXT("Wrong entrance type detected"), Issues.Num() > 0);

	return true;
}

// ============================================================================
// METRICS TESTS
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonValidateMetricsCorrect, "Dungeon.GridValidation.Metrics.CorrectCountsPasses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonValidateMetricsCorrect::RunTest(const FString& Parameters)
{
	FDungeonResult Result = DungeonValidationTestHelpers::CreateSimpleResult();

	TArray<FDungeonValidationIssue> Issues;
	FDungeonValidator::ValidateMetrics(Result, Issues);
	TestEqual(TEXT("Correct metrics pass"), Issues.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonValidateMetricsMismatch, "Dungeon.GridValidation.Metrics.MismatchDetected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonValidateMetricsMismatch::RunTest(const FString& Parameters)
{
	FDungeonResult Result = DungeonValidationTestHelpers::CreateSimpleResult();
	// Corrupt metrics
	Result.TotalRoomCells = 999;
	Result.TotalHallwayCells = 0;

	TArray<FDungeonValidationIssue> Issues;
	FDungeonValidator::ValidateMetrics(Result, Issues);
	TestTrue(TEXT("Metrics mismatch detected"), Issues.Num() >= 2);

	return true;
}
