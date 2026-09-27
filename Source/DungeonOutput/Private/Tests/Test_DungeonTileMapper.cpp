// Test_DungeonTileMapper.cpp — Unit and integration tests for the tile mapping algorithm
#include "Misc/AutomationTest.h"
#include "DungeonTypes.h"
#include "DungeonConfig.h"
#include "DungeonGenerator.h"
#include "DungeonTileSet.h"
#include "DungeonTileMapper.h"
#include "DungeonBoundaryRules.h"

// ============================================================================
// Test Helpers
// ============================================================================

namespace DungeonTileMapperTestHelpers
{
	UDungeonTileSet* CreateTileSet()
	{
		UDungeonTileSet* TS = NewObject<UDungeonTileSet>();
		TS->AddToRoot();
		return TS;
	}

	void CleanupTileSet(UDungeonTileSet* TS)
	{
		if (TS) { TS->RemoveFromRoot(); }
	}

	/**
	 * Create a 5x5x1 grid with a 3x3 room at (1,1,0) surrounded by RoomWall.
	 * Layout (Z=0):
	 *   W W W W W
	 *   W R R R W
	 *   W R R R W
	 *   W R R R W
	 *   W W W W W
	 * Where W=RoomWall, R=Room. Outside = Empty (but our grid is exactly 5x5).
	 */
	FDungeonResult CreateSingleRoomResult()
	{
		FDungeonResult Result;
		Result.GridSize = FIntVector(5, 5, 1);
		Result.CellWorldSize = 400.0f;
		Result.Grid.Initialize(FIntVector(5, 5, 1));

		// Fill everything with RoomWall first
		for (int32 Y = 0; Y < 5; ++Y)
		{
			for (int32 X = 0; X < 5; ++X)
			{
				Result.Grid.GetCell(X, Y, 0).CellType = EDungeonCellType::RoomWall;
				Result.Grid.GetCell(X, Y, 0).RoomIndex = 1;
			}
		}

		// Inner 3x3 is Room
		for (int32 Y = 1; Y <= 3; ++Y)
		{
			for (int32 X = 1; X <= 3; ++X)
			{
				Result.Grid.GetCell(X, Y, 0).CellType = EDungeonCellType::Room;
				Result.Grid.GetCell(X, Y, 0).RoomIndex = 1;
			}
		}

		FDungeonRoom Room;
		Room.RoomIndex = 1;
		Room.Position = FIntVector(1, 1, 0);
		Room.Size = FIntVector(3, 3, 1);
		Room.Center = FIntVector(2, 2, 0);
		Result.Rooms.Add(Room);
		Result.EntranceRoomIndex = -1;
		return Result;
	}

	/**
	 * Create a grid with a 3x3 room + 3-cell hallway + 3x3 room.
	 * 11x5x1 grid:
	 *   W W W W W . . . W W W
	 *   W R R R W H H H W R R   (Y=1: room + hall + room)
	 *   W R R R D H H H D R R
	 *   W R R R W H H H W R R
	 *   W W W W W . . . W W W
	 * Simplified: Two rooms connected by a 3-cell hallway through doors.
	 */
	FDungeonResult CreateHallwayResult()
	{
		FDungeonResult Result;
		Result.GridSize = FIntVector(11, 5, 1);
		Result.CellWorldSize = 400.0f;
		Result.Grid.Initialize(FIntVector(11, 5, 1));

		// Default all to Empty
		for (int32 Y = 0; Y < 5; ++Y)
			for (int32 X = 0; X < 11; ++X)
				Result.Grid.GetCell(X, Y, 0).CellType = EDungeonCellType::Empty;

		// Room A: walls at x=[0..4], y=[0..4], interior room at x=[1..3], y=[1..3]
		for (int32 Y = 0; Y < 5; ++Y)
			for (int32 X = 0; X < 5; ++X)
				Result.Grid.GetCell(X, Y, 0).CellType = EDungeonCellType::RoomWall;
		for (int32 Y = 1; Y <= 3; ++Y)
			for (int32 X = 1; X <= 3; ++X)
				Result.Grid.GetCell(X, Y, 0).CellType = EDungeonCellType::Room;

		// Room B: walls at x=[8..10], y=[0..4], interior at x=[9..10], y=[1..3]
		// (using 3-wide room at x=[8..10])
		for (int32 Y = 0; Y < 5; ++Y)
			for (int32 X = 8; X < 11; ++X)
				Result.Grid.GetCell(X, Y, 0).CellType = EDungeonCellType::RoomWall;
		for (int32 Y = 1; Y <= 3; ++Y)
			for (int32 X = 9; X <= 10; ++X)
				Result.Grid.GetCell(X, Y, 0).CellType = EDungeonCellType::Room;

		// Hallway at y=2, x=[5..7]
		for (int32 X = 5; X <= 7; ++X)
		{
			Result.Grid.GetCell(X, 2, 0).CellType = EDungeonCellType::Hallway;
			Result.Grid.GetCell(X, 2, 0).HallwayIndex = 1;
		}

		// Doors connecting rooms to hallway
		Result.Grid.GetCell(4, 2, 0).CellType = EDungeonCellType::Door;
		Result.Grid.GetCell(8, 2, 0).CellType = EDungeonCellType::Door;

		Result.EntranceRoomIndex = -1;
		return Result;
	}

	/**
	 * Create a 5x5x2 grid with the same room at Z=0 and Z=1 (stacked).
	 */
	FDungeonResult CreateMultiFloorResult()
	{
		FDungeonResult Result;
		Result.GridSize = FIntVector(5, 5, 2);
		Result.CellWorldSize = 400.0f;
		Result.Grid.Initialize(FIntVector(5, 5, 2));

		for (int32 Z = 0; Z < 2; ++Z)
		{
			for (int32 Y = 0; Y < 5; ++Y)
			{
				for (int32 X = 0; X < 5; ++X)
				{
					Result.Grid.GetCell(X, Y, Z).CellType = EDungeonCellType::RoomWall;
				}
			}
			for (int32 Y = 1; Y <= 3; ++Y)
			{
				for (int32 X = 1; X <= 3; ++X)
				{
					Result.Grid.GetCell(X, Y, Z).CellType = EDungeonCellType::Room;
				}
			}
		}

		Result.EntranceRoomIndex = -1;
		return Result;
	}
}

// ============================================================================
// Tests
// ============================================================================

// --- 1. EmptyGrid.ProducesNoInstances ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTileMapperEmptyGrid,
	"Dungeon.TileMapper.EmptyGrid.ProducesNoInstances",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTileMapperEmptyGrid::RunTest(const FString& Parameters)
{
	using namespace DungeonTileMapperTestHelpers;

	UDungeonTileSet* TS = CreateTileSet();
	FDungeonResult Result;
	Result.GridSize = FIntVector(5, 5, 1);
	Result.CellWorldSize = 400.0f;
	Result.Grid.Initialize(FIntVector(5, 5, 1));
	// All cells default to Empty

	FDungeonTileMapResult TileMap = FDungeonTileMapper::MapToTiles(Result, *TS, FVector::ZeroVector);
	TestEqual(TEXT("Empty grid produces 0 instances"), TileMap.GetTotalInstanceCount(), 0);

	CleanupTileSet(TS);
	return true;
}

// --- 2. SingleRoom.HasFloors ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTileMapperSingleRoomFloors,
	"Dungeon.TileMapper.SingleRoom.HasFloors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTileMapperSingleRoomFloors::RunTest(const FString& Parameters)
{
	using namespace DungeonTileMapperTestHelpers;

	UDungeonTileSet* TS = CreateTileSet();
	FDungeonResult Result = CreateSingleRoomResult();

	FDungeonTileMapResult TileMap = FDungeonTileMapper::MapToTiles(Result, *TS, FVector::ZeroVector);

	// 3x3 room at Z=0, cell below (Z=-1) is OOB → all 9 cells get floors
	const int32 FloorCount = TileMap.Transforms[static_cast<int32>(EDungeonTileType::RoomFloor)].Num();
	TestEqual(TEXT("3x3 room should have 9 floor tiles"), FloorCount, 9);

	CleanupTileSet(TS);
	return true;
}

// --- 3. SingleRoom.HasCeilings ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTileMapperSingleRoomCeilings,
	"Dungeon.TileMapper.SingleRoom.HasCeilings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTileMapperSingleRoomCeilings::RunTest(const FString& Parameters)
{
	using namespace DungeonTileMapperTestHelpers;

	UDungeonTileSet* TS = CreateTileSet();
	FDungeonResult Result = CreateSingleRoomResult();
	// Z=0 in a 1-level grid → Z+1 is OOB → ceilings placed

	FDungeonTileMapResult TileMap = FDungeonTileMapper::MapToTiles(Result, *TS, FVector::ZeroVector);

	const int32 CeilingCount = TileMap.Transforms[static_cast<int32>(EDungeonTileType::RoomCeiling)].Num();
	TestEqual(TEXT("3x3 room with solid above should have 9 ceiling tiles"), CeilingCount, 9);

	CleanupTileSet(TS);
	return true;
}

// --- 4. SingleRoom.HasWalls ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTileMapperSingleRoomWalls,
	"Dungeon.TileMapper.SingleRoom.HasWalls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTileMapperSingleRoomWalls::RunTest(const FString& Parameters)
{
	using namespace DungeonTileMapperTestHelpers;

	UDungeonTileSet* TS = CreateTileSet();
	FDungeonResult Result = CreateSingleRoomResult();

	FDungeonTileMapResult TileMap = FDungeonTileMapper::MapToTiles(Result, *TS, FVector::ZeroVector);

	// 3x3 room surrounded by RoomWall on all 4 sides:
	// Top row (Y=1): 3 cells each have wall on -Y side = 3
	// Bottom row (Y=3): 3 cells each have wall on +Y side = 3
	// Left col (X=1): 3 cells each have wall on -X side = 3
	// Right col (X=3): 3 cells each have wall on +X side = 3
	// Total = 12 wall segments
	const int32 WallCount = TileMap.Transforms[static_cast<int32>(EDungeonTileType::WallSegment)].Num();
	TestEqual(TEXT("3x3 room surrounded by walls should have 12 wall segments"), WallCount, 12);

	CleanupTileSet(TS);
	return true;
}

// --- 5. MultiFloor.NoFloorBetweenStacked ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTileMapperMultiFloorNoMiddle,
	"Dungeon.TileMapper.MultiFloor.NoFloorBetweenStacked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTileMapperMultiFloorNoMiddle::RunTest(const FString& Parameters)
{
	using namespace DungeonTileMapperTestHelpers;

	UDungeonTileSet* TS = CreateTileSet();
	FDungeonResult Result = CreateMultiFloorResult();

	FDungeonTileMapResult TileMap = FDungeonTileMapper::MapToTiles(Result, *TS, FVector::ZeroVector);

	// Z=0 rooms: cell above at Z=1 is Room (not solid) → NO ceiling for Z=0
	// Z=1 rooms: cell below at Z=0 is Room (not solid) → NO floor for Z=1
	// Z=0 floor: cell below Z=-1 is OOB (solid) → 9 floors
	// Z=1 ceiling: cell above Z=2 is OOB (solid) → 9 ceilings
	const int32 FloorCount = TileMap.Transforms[static_cast<int32>(EDungeonTileType::RoomFloor)].Num();
	const int32 CeilingCount = TileMap.Transforms[static_cast<int32>(EDungeonTileType::RoomCeiling)].Num();

	TestEqual(TEXT("Stacked rooms: only bottom floor gets floor tiles"), FloorCount, 9);
	TestEqual(TEXT("Stacked rooms: only top floor gets ceiling tiles"), CeilingCount, 9);

	CleanupTileSet(TS);
	return true;
}

// --- 6. Hallway.UsesHallwayFloorTile ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTileMapperHallwayFloorType,
	"Dungeon.TileMapper.Hallway.UsesHallwayFloorTile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTileMapperHallwayFloorType::RunTest(const FString& Parameters)
{
	using namespace DungeonTileMapperTestHelpers;

	UDungeonTileSet* TS = CreateTileSet();
	FDungeonResult Result = CreateHallwayResult();

	FDungeonTileMapResult TileMap = FDungeonTileMapper::MapToTiles(Result, *TS, FVector::ZeroVector);

	// 3 hallway cells should produce HallwayFloor, not RoomFloor
	const int32 HallFloorCount = TileMap.Transforms[static_cast<int32>(EDungeonTileType::HallwayFloor)].Num();
	TestTrue(TEXT("Hallway cells should produce HallwayFloor tiles"), HallFloorCount >= 3);

	CleanupTileSet(TS);
	return true;
}

// --- 7. Staircase.ProducesStaircaseTile ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTileMapperStaircaseExists,
	"Dungeon.TileMapper.Staircase.ProducesStaircaseTile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTileMapperStaircaseExists::RunTest(const FString& Parameters)
{
	using namespace DungeonTileMapperTestHelpers;

	UDungeonTileSet* TS = CreateTileSet();

	FDungeonResult Result;
	Result.GridSize = FIntVector(3, 3, 2);
	Result.CellWorldSize = 400.0f;
	Result.Grid.Initialize(FIntVector(3, 3, 2));

	// Staircase meshes are driven by Result.Staircases (the pathfinder's records), not by cell
	// flags — the cells only shape floors/walls around the ramp.
	Result.Grid.GetCell(1, 1, 0).CellType = EDungeonCellType::Staircase;
	Result.Grid.GetCell(1, 1, 0).StaircaseDirection = 0; // climb +X

	FDungeonStaircase Staircase;
	Staircase.BottomCell = FIntVector(1, 1, 0);
	Staircase.TopCell = FIntVector(2, 1, 1);
	Staircase.Direction = 0;
	Staircase.RiseRunRatio = 1;
	Result.Staircases.Add(Staircase);
	Result.EntranceRoomIndex = -1;

	FDungeonTileMapResult TileMap = FDungeonTileMapper::MapToTiles(Result, *TS, FVector::ZeroVector);

	const int32 StairCount = TileMap.Transforms[static_cast<int32>(EDungeonTileType::StaircaseMesh)].Num();
	TestEqual(TEXT("Single staircase record should produce 1 StaircaseMesh"), StairCount, 1);

	CleanupTileSet(TS);
	return true;
}

// --- 8. Staircase.CorrectRotation ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTileMapperStaircaseRotation,
	"Dungeon.TileMapper.Staircase.CorrectRotation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTileMapperStaircaseRotation::RunTest(const FString& Parameters)
{
	using namespace DungeonTileMapperTestHelpers;

	UDungeonTileSet* TS = CreateTileSet();

	// Mesh convention: slopes DOWN along local +Y (climb = -Y), so the directional yaw rotates
	// -Y onto the climb direction: 0=+X(90°), 1=-X(-90°), 2=+Y(180°), 3=-Y(0°).
	const float ExpectedYaws[] = { 90.0f, -90.0f, 180.0f, 0.0f };
	const FIntVector BottomCells[] = {
		FIntVector(0, 1, 0), FIntVector(2, 1, 0), FIntVector(1, 0, 0), FIntVector(1, 2, 0) };

	for (int32 Dir = 0; Dir < 4; ++Dir)
	{
		FDungeonResult Result;
		Result.GridSize = FIntVector(3, 3, 2);
		Result.CellWorldSize = 400.0f;
		Result.Grid.Initialize(FIntVector(3, 3, 2));
		Result.Grid.GetCell(BottomCells[Dir].X, BottomCells[Dir].Y, 0).CellType = EDungeonCellType::Staircase;
		Result.Grid.GetCell(BottomCells[Dir].X, BottomCells[Dir].Y, 0).StaircaseDirection = static_cast<uint8>(Dir);

		FDungeonStaircase Staircase;
		Staircase.BottomCell = BottomCells[Dir];
		Staircase.TopCell = FIntVector(1, 1, 1);
		Staircase.Direction = static_cast<uint8>(Dir);
		Staircase.RiseRunRatio = 1;
		Result.Staircases.Add(Staircase);
		Result.EntranceRoomIndex = -1;

		FDungeonTileMapResult TileMap = FDungeonTileMapper::MapToTiles(Result, *TS, FVector::ZeroVector);

		const TArray<FTransform>& StairTransforms =
			TileMap.Transforms[static_cast<int32>(EDungeonTileType::StaircaseMesh)];

		if (TestEqual(FString::Printf(TEXT("Direction %d should have 1 staircase"), Dir),
			StairTransforms.Num(), 1))
		{
			const float ActualYaw = StairTransforms[0].Rotator().Yaw;
			TestTrue(FString::Printf(TEXT("Direction %d: yaw should be %.1f, got %.1f"),
				Dir, ExpectedYaws[Dir], ActualYaw),
				FMath::IsNearlyZero(FRotator::NormalizeAxis(ActualYaw - ExpectedYaws[Dir]), 0.1f));
		}
	}

	CleanupTileSet(TS);
	return true;
}

// --- 9. Door.ProducesDoorFrame ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTileMapperDoorFrame,
	"Dungeon.TileMapper.Door.ProducesDoorFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTileMapperDoorFrame::RunTest(const FString& Parameters)
{
	using namespace DungeonTileMapperTestHelpers;

	UDungeonTileSet* TS = CreateTileSet();
	FDungeonResult Result = CreateHallwayResult();

	FDungeonTileMapResult TileMap = FDungeonTileMapper::MapToTiles(Result, *TS, FVector::ZeroVector);

	// 2 door cells in our hallway result
	const int32 DoorCount = TileMap.Transforms[static_cast<int32>(EDungeonTileType::DoorFrame)].Num();
	TestEqual(TEXT("Hallway result should have 2 DoorFrame tiles"), DoorCount, 2);

	CleanupTileSet(TS);
	return true;
}

// --- 10. FullGeneration.IntegrationTest ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTileMapperFullIntegration,
	"Dungeon.TileMapper.FullGeneration.IntegrationTest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTileMapperFullIntegration::RunTest(const FString& Parameters)
{
	using namespace DungeonTileMapperTestHelpers;

	UDungeonTileSet* TS = CreateTileSet();

	// Run real generator
	UDungeonConfiguration* Config = NewObject<UDungeonConfiguration>();
	Config->AddToRoot();
	Config->GridSize = FIntVector(25, 25, 1);
	Config->RoomCount = 4;
	Config->MinRoomSize = FIntVector(3, 3, 1);
	Config->MaxRoomSize = FIntVector(5, 5, 1);
	Config->RoomBuffer = 1;
	Config->bUseFixedSeed = true;
	Config->FixedSeed = 99999;

	UDungeonGenerator* Generator = NewObject<UDungeonGenerator>();
	FDungeonResult Result = Generator->Generate(Config, 99999);

	TestTrue(TEXT("Generator should produce rooms"), Result.Rooms.Num() > 0);

	// Map to tiles
	FDungeonTileMapResult TileMap = FDungeonTileMapper::MapToTiles(Result, *TS, FVector::ZeroVector);

	const int32 TotalCount = TileMap.GetTotalInstanceCount();
	TestTrue(TEXT("Total instance count should be > 0"), TotalCount > 0);

	const int32 FloorCount = TileMap.Transforms[static_cast<int32>(EDungeonTileType::RoomFloor)].Num()
		+ TileMap.Transforms[static_cast<int32>(EDungeonTileType::HallwayFloor)].Num();
	TestTrue(TEXT("Should have floor tiles"), FloorCount > 0);

	const int32 WallCount = TileMap.Transforms[static_cast<int32>(EDungeonTileType::WallSegment)].Num();
	TestTrue(TEXT("Should have wall tiles"), WallCount > 0);

	const int32 CeilingCount = TileMap.Transforms[static_cast<int32>(EDungeonTileType::RoomCeiling)].Num()
		+ TileMap.Transforms[static_cast<int32>(EDungeonTileType::HallwayCeiling)].Num();
	TestTrue(TEXT("Should have ceiling tiles"), CeilingCount > 0);

	Config->RemoveFromRoot();
	CleanupTileSet(TS);
	return true;
}


// --- OpenEntranceCeiling: only the designated entrance cell's ceiling is skipped ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTileMapperOpenEntranceCeiling,
	"Dungeon.TileMapper.Entrance.OpenCeilingSkipsOnlyEntranceCell",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTileMapperOpenEntranceCeiling::RunTest(const FString& Parameters)
{
	using namespace DungeonTileMapperTestHelpers;

	UDungeonTileSet* TS = CreateTileSet();
	FDungeonResult Result = CreateSingleRoomResult();

	// Designate the room centre as the entrance cell (a vertical passage would land here).
	Result.Grid.GetCell(2, 2, 0).CellType = EDungeonCellType::Entrance;
	Result.EntranceCell = FIntVector(2, 2, 0);
	Result.EntranceRoomIndex = 1;

	// Default (closed): all 9 walkable cells are ceilinged.
	FDungeonTileMapResult Closed = FDungeonTileMapper::MapToTiles(Result, *TS, FVector::ZeroVector);
	TestEqual(TEXT("Closed: 9 ceilings"),
		Closed.Transforms[static_cast<int32>(EDungeonTileType::RoomCeiling)].Num(), 9);

	// Open: exactly the entrance cell's ceiling is skipped; floors untouched.
	FDungeonTileMapResult Open = FDungeonTileMapper::MapToTiles(Result, *TS, FVector::ZeroVector,
		/*bOpenEntranceCeiling=*/true);
	TestEqual(TEXT("Open: 8 ceilings (entrance cell open)"),
		Open.Transforms[static_cast<int32>(EDungeonTileType::RoomCeiling)].Num(), 8);
	TestEqual(TEXT("Open: floors unaffected"),
		Open.Transforms[static_cast<int32>(EDungeonTileType::RoomFloor)].Num(), 9);

	// No ceiling transform sits over the entrance cell — cell (2,2) spans 800..1200, centre (1000,1000).
	for (const FTransform& Xf : Open.Transforms[static_cast<int32>(EDungeonTileType::RoomCeiling)])
	{
		const FVector P = Xf.GetLocation();
		const bool bOverEntrance = FMath::Abs(P.X - 1000.0f) < 1.0f && FMath::Abs(P.Y - 1000.0f) < 1.0f;
		TestFalse(TEXT("No ceiling over the entrance cell"), bOverEntrance);
	}

	CleanupTileSet(TS);
	return true;
}

// --- OpenEntranceCeiling on a TWO-floor entrance room: the room's lid is the ceiling of the top
// cell above the entrance cell, not the (non-existent) ceiling of the ground-floor cell ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTileMapperOpenEntranceCeilingTallRoom,
	"Dungeon.TileMapper.Entrance.OpenCeilingOpensTallRoomLid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTileMapperOpenEntranceCeilingTallRoom::RunTest(const FString& Parameters)
{
	using namespace DungeonTileMapperTestHelpers;

	UDungeonTileSet* TS = CreateTileSet();

	// 5x5x2: the single-room layout duplicated on Z=1 as the same room (a 3x3x2 room).
	FDungeonResult Result;
	Result.GridSize = FIntVector(5, 5, 2);
	Result.CellWorldSize = 400.0f;
	Result.Grid.Initialize(Result.GridSize);
	for (int32 Z = 0; Z < 2; ++Z)
	{
		for (int32 Y = 0; Y < 5; ++Y)
		{
			for (int32 X = 0; X < 5; ++X)
			{
				const bool bInterior = X >= 1 && X <= 3 && Y >= 1 && Y <= 3;
				FDungeonCell& Cell = Result.Grid.GetCell(X, Y, Z);
				Cell.CellType = bInterior ? EDungeonCellType::Room : EDungeonCellType::RoomWall;
				Cell.RoomIndex = 1;
				Cell.FloorIndex = static_cast<uint8>(Z);
			}
		}
	}
	FDungeonRoom Room;
	Room.RoomIndex = 1;
	Room.Position = FIntVector(1, 1, 0);
	Room.Size = FIntVector(3, 3, 2);
	Room.Center = FIntVector(2, 2, 1);
	Result.Rooms.Add(Room);
	Result.EntranceRoomIndex = 0;
	Result.EntranceCell = FIntVector(2, 2, 0); // ground floor
	Result.Grid.GetCell(2, 2, 0).CellType = EDungeonCellType::Entrance;

	// Closed: only the Z=1 cells have a ceiling (Z=0 -> Z=1 is the same room, open) = 9.
	FDungeonTileMapResult Closed = FDungeonTileMapper::MapToTiles(Result, *TS, FVector::ZeroVector);
	TestEqual(TEXT("Closed: 9 ceilings (lid only)"),
		Closed.Transforms[static_cast<int32>(EDungeonTileType::RoomCeiling)].Num(), 9);

	// Open: the lid over the entrance column is skipped -> 8. (Before the opening-cell fix the
	// mapper skipped the ground-floor cell's non-existent ceiling and the lid stayed at 9.)
	FDungeonTileMapResult Open = FDungeonTileMapper::MapToTiles(Result, *TS, FVector::ZeroVector,
		/*bOpenEntranceCeiling=*/true);
	TestEqual(TEXT("Open: 8 ceilings (lid over the entrance column open)"),
		Open.Transforms[static_cast<int32>(EDungeonTileType::RoomCeiling)].Num(), 8);

	// The lid over cell (2,2,1) sits at Z = 2 * 400 = 800; nothing remains at that XY.
	for (const FTransform& Xf : Open.Transforms[static_cast<int32>(EDungeonTileType::RoomCeiling)])
	{
		const FVector P = Xf.GetLocation();
		const bool bOverEntrance = FMath::Abs(P.X - 1000.0f) < 1.0f && FMath::Abs(P.Y - 1000.0f) < 1.0f;
		TestFalse(TEXT("No lid over the entrance column"), bOverEntrance);
	}

	CleanupTileSet(TS);
	return true;
}

// --- Approach-aware opening: a FromSide approach opens the WALL on the face of the opening cell
// (the room face the tunnel enters through), not a ceiling ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTileMapperOpenEntranceSide,
	"Dungeon.TileMapper.Entrance.FromSideOpensFaceWall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTileMapperOpenEntranceSide::RunTest(const FString& Parameters)
{
	using namespace DungeonTileMapperTestHelpers;

	UDungeonTileSet* TS = CreateTileSet();
	FDungeonResult Result = CreateSingleRoomResult();
	Result.Grid.GetCell(2, 2, 0).CellType = EDungeonCellType::Entrance;
	Result.EntranceCell = FIntVector(2, 2, 0);
	Result.EntranceRoomIndex = 0;
	Result.EntranceApproach.Approach = EDungeonEntranceApproach::FromSide;
	Result.EntranceApproach.bSatisfied = true;
	Result.EntranceApproach.Face = EDungeonGridFace::MinX;
	Result.EntranceApproach.OpeningCell = FIntVector(1, 2, 0); // the room's -X face cell in the entrance row

	const int32 WallIdx = static_cast<int32>(EDungeonTileType::WallSegment);
	const FDungeonTileMapResult Closed = FDungeonTileMapper::MapToTiles(Result, *TS, FVector::ZeroVector);
	TestEqual(TEXT("Closed: 12 perimeter walls"), Closed.Transforms[WallIdx].Num(), 12);
	TestEqual(TEXT("Closed: 9 ceilings"), Closed.Transforms[static_cast<int32>(EDungeonTileType::RoomCeiling)].Num(), 9);

	const FDungeonTileMapResult Open = FDungeonTileMapper::MapToTiles(Result, *TS, FVector::ZeroVector, /*bOpenEntranceCeiling=*/true);
	TestEqual(TEXT("Open: 11 walls (face wall open)"), Open.Transforms[WallIdx].Num(), 11);
	TestEqual(TEXT("Open: ceilings untouched for a side approach"), Open.Transforms[static_cast<int32>(EDungeonTileType::RoomCeiling)].Num(), 9);
	TestEqual(TEXT("Open: floors untouched"), Open.Transforms[static_cast<int32>(EDungeonTileType::RoomFloor)].Num(), 9);

	// The -X wall of cell (1,2): cell centre (600,1000), wall offset -200 in X -> (400,1000).
	for (const FTransform& Xf : Open.Transforms[WallIdx])
	{
		const FVector P = Xf.GetLocation();
		const bool bFaceWall = FMath::Abs(P.X - 400.0f) < 1.0f && FMath::Abs(P.Y - 1000.0f) < 1.0f;
		TestFalse(TEXT("No wall on the opening face"), bFaceWall);
	}

	CleanupTileSet(TS);
	return true;
}

// --- FromBelow opens the FLOOR of the opening cell ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTileMapperOpenEntranceBelow,
	"Dungeon.TileMapper.Entrance.FromBelowOpensFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTileMapperOpenEntranceBelow::RunTest(const FString& Parameters)
{
	using namespace DungeonTileMapperTestHelpers;

	UDungeonTileSet* TS = CreateTileSet();
	FDungeonResult Result = CreateSingleRoomResult();
	Result.Grid.GetCell(2, 2, 0).CellType = EDungeonCellType::Entrance;
	Result.EntranceCell = FIntVector(2, 2, 0);
	Result.EntranceRoomIndex = 0;
	Result.EntranceApproach.Approach = EDungeonEntranceApproach::FromBelow;
	Result.EntranceApproach.bSatisfied = true;
	Result.EntranceApproach.OpeningCell = FIntVector(2, 2, 0);

	const int32 FloorIdx = static_cast<int32>(EDungeonTileType::RoomFloor);
	const FDungeonTileMapResult Open = FDungeonTileMapper::MapToTiles(Result, *TS, FVector::ZeroVector, /*bOpenEntranceCeiling=*/true);
	TestEqual(TEXT("Open: 8 floors (opening cell's floor open)"), Open.Transforms[FloorIdx].Num(), 8);
	TestEqual(TEXT("Open: ceilings untouched for a below approach"), Open.Transforms[static_cast<int32>(EDungeonTileType::RoomCeiling)].Num(), 9);
	for (const FTransform& Xf : Open.Transforms[FloorIdx])
	{
		const FVector P = Xf.GetLocation();
		TestFalse(TEXT("No floor under the opening cell"), FMath::Abs(P.X - 1000.0f) < 1.0f && FMath::Abs(P.Y - 1000.0f) < 1.0f);
	}

	CleanupTileSet(TS);
	return true;
}

// ============================================================================
// Shared-face ownership (environment polish E1)
// ============================================================================

namespace DungeonTileMapperTestHelpers
{
	/**
	 * 6x3x1: a two-cell room (1..2, 1) beside a two-cell hallway (3..4, 1) with NO door between
	 * them, RoomWall around the room, Empty elsewhere. The face between (2,1) and (3,1) needs a
	 * wall from both sides.
	 */
	FDungeonResult CreateRoomBesideHallwayResult()
	{
		FDungeonResult Result;
		Result.GridSize = FIntVector(6, 3, 1);
		Result.CellWorldSize = 400.0f;
		Result.Grid.Initialize(Result.GridSize);
		for (int32 Y = 0; Y < 3; ++Y)
			for (int32 X = 0; X < 4; ++X)
			{
				FDungeonCell& C = Result.Grid.GetCell(X, Y, 0);
				C.CellType = EDungeonCellType::RoomWall; C.RoomIndex = 1;
			}
		for (int32 X = 1; X <= 2; ++X)
		{
			FDungeonCell& C = Result.Grid.GetCell(X, 1, 0);
			C.CellType = EDungeonCellType::Room; C.RoomIndex = 1;
		}
		for (int32 X = 3; X <= 4; ++X)
		{
			FDungeonCell& C = Result.Grid.GetCell(X, 1, 0);
			C.CellType = EDungeonCellType::Hallway; C.HallwayIndex = 1; C.RoomIndex = 0;
		}
		Result.EntranceRoomIndex = -1;
		return Result;
	}

	/** Wall-family instances whose location matches a face centre (engine cube: zero pivot offset). */
	int32 CountWallFamilyAt(const FDungeonTileMapResult& Map, const FVector& FaceCentre, EDungeonTileType* OutType = nullptr)
	{
		static const EDungeonTileType Family[] = {
			EDungeonTileType::WallSegment, EDungeonTileType::WallPartition,
			EDungeonTileType::DoorFrame, EDungeonTileType::EntranceFrame };
		int32 N = 0;
		for (EDungeonTileType T : Family)
		{
			for (const FTransform& Xf : Map.Transforms[static_cast<int32>(T)])
			{
				if (Xf.GetLocation().Equals(FaceCentre, 1.0f)) { ++N; if (OutType) { *OutType = T; } }
			}
		}
		return N;
	}
}

// --- The face between a room cell and a corridor cell is dressed once, by a partition ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTileMapperSharedFaceOnce,
	"Dungeon.TileMapper.Walls.SharedFaceDressedOnceByOwner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTileMapperSharedFaceOnce::RunTest(const FString& Parameters)
{
	using namespace DungeonTileMapperTestHelpers;

	UDungeonTileSet* TS = CreateTileSet();
	// The partition slot is not a tileset default (see UDungeonTileSet::PopulateDefaultSlots);
	// give it the same cube as the wall segment so the two are told apart by type only.
	TS->Slots.Add(EDungeonTileType::WallPartition, TS->GetSlot(EDungeonTileType::WallSegment));
	const FDungeonResult Result = CreateRoomBesideHallwayResult();
	// Face between cell (2,1) and (3,1): X = 3 * 400, Y = 1.5 * 400, Z = half cell.
	const FVector SharedFace(1200.0f, 600.0f, 200.0f);
	// A rock-backed face for contrast: cell (1,1)'s -X face.
	const FVector RockFace(400.0f, 600.0f, 200.0f);

	{
		const FDungeonTileMapResult Map = FDungeonTileMapper::MapToTiles(Result, *TS, FVector::ZeroVector);
		EDungeonTileType Type = EDungeonTileType::COUNT;
		TestEqual(TEXT("shared face: exactly one piece"), CountWallFamilyAt(Map, SharedFace, &Type), 1);
		TestEqual(TEXT("shared face: it is a partition"), Type, EDungeonTileType::WallPartition);
		TestEqual(TEXT("rock face: exactly one piece"), CountWallFamilyAt(Map, RockFace, &Type), 1);
		TestEqual(TEXT("rock face: it is a wall segment"), Type, EDungeonTileType::WallSegment);
		// The owner is the -X side (the room cell): the instance faces +X.
		for (const FTransform& Xf : Map.Transforms[static_cast<int32>(EDungeonTileType::WallPartition)])
		{
			if (Xf.GetLocation().Equals(SharedFace, 1.0f))
			{
				TestTrue(TEXT("partition placed from the owning (-X) side, facing +X"),
					FMath::IsNearlyEqual(FRotator::NormalizeAxis(Xf.Rotator().Yaw), 0.0f, 0.5f));
			}
		}
	}

	// Without a partition slot the owner falls back to the wall segment — still exactly one piece.
	TS->Slots.Remove(EDungeonTileType::WallPartition);
	{
		const FDungeonTileMapResult Map = FDungeonTileMapper::MapToTiles(Result, *TS, FVector::ZeroVector);
		EDungeonTileType Type = EDungeonTileType::COUNT;
		TestEqual(TEXT("no partition slot: still one piece"), CountWallFamilyAt(Map, SharedFace, &Type), 1);
		TestEqual(TEXT("no partition slot: falls back to WallSegment"), Type, EDungeonTileType::WallSegment);
	}

	CleanupTileSet(TS);
	return true;
}

// --- Coverage on generated dungeons: every wall-needing face of every open cell has exactly one
// wall-family piece, shared faces included (two before E1) ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTileMapperWallCoverage,
	"Dungeon.TileMapper.Walls.EveryFaceDressedExactlyOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTileMapperWallCoverage::RunTest(const FString& Parameters)
{
	using namespace DungeonTileMapperTestHelpers;

	UDungeonConfiguration* Config = NewObject<UDungeonConfiguration>();
	Config->AddToRoot();
	Config->GridSize = FIntVector(24, 24, 3);
	Config->RoomCount = 8;
	Config->MinRoomSize = FIntVector(3, 3, 1);
	Config->MaxRoomSize = FIntVector(6, 6, 2);
	Config->RoomBuffer = 1;
	UDungeonGenerator* Generator = NewObject<UDungeonGenerator>();
	Generator->AddToRoot();
	UDungeonTileSet* TS = CreateTileSet();
	TS->Slots.Add(EDungeonTileType::WallPartition, TS->GetSlot(EDungeonTileType::WallSegment));

	static const int32 DX[4] = {1, -1, 0, 0};
	static const int32 DY[4] = {0, 0, 1, -1};
	int32 SharedFacesSeen = 0;

	for (int64 Seed = 1; Seed <= 12; ++Seed)
	{
		const FDungeonResult R = Generator->Generate(Config, Seed);
		if (R.Rooms.Num() < 2) { continue; }
		const FDungeonTileMapResult Map = FDungeonTileMapper::MapToTiles(R, *TS, FVector::ZeroVector);
		const float CS = R.CellWorldSize;

		// Wall-family instances bucketed by face centre (rounded to a unit).
		TMap<FIntVector, int32> ByFace;
		static const EDungeonTileType Family[] = {
			EDungeonTileType::WallSegment, EDungeonTileType::WallPartition,
			EDungeonTileType::DoorFrame, EDungeonTileType::EntranceFrame };
		for (EDungeonTileType T : Family)
		{
			for (const FTransform& Xf : Map.Transforms[static_cast<int32>(T)])
			{
				const FVector L = Xf.GetLocation();
				ByFace.FindOrAdd(FIntVector(FMath::RoundToInt(L.X), FMath::RoundToInt(L.Y), FMath::RoundToInt(L.Z)))++;
			}
		}

		TSet<FIntVector> Checked;
		for (int32 Z = 0; Z < R.GridSize.Z; ++Z)
		for (int32 Y = 0; Y < R.GridSize.Y; ++Y)
		for (int32 X = 0; X < R.GridSize.X; ++X)
		{
			const FIntVector Cell(X, Y, Z);
			const EDungeonCellType Type = R.Grid.GetCell(Cell).CellType;
			if (!FDungeonBoundaryRules::IsOpenCell(Type)) { continue; }
			// Stair cells are dressed by the ramp mesh; their walls are their neighbours' business.
			if (Type == EDungeonCellType::Staircase || Type == EDungeonCellType::StaircaseHead) { continue; }
			for (int32 D = 0; D < 4; ++D)
			{
				const int32 NX = X + DX[D], NY = Y + DY[D];
				if (!FDungeonBoundaryRules::NeedsWall(R.Grid, Cell, NX, NY, Z)) { continue; }
				const FIntVector Face(
					FMath::RoundToInt((X + 0.5f + 0.5f * DX[D]) * CS),
					FMath::RoundToInt((Y + 0.5f + 0.5f * DY[D]) * CS),
					FMath::RoundToInt((Z + 0.5f) * CS));
				if (Checked.Contains(Face)) { continue; }
				Checked.Add(Face);
				const bool bShared = R.Grid.IsInBounds(NX, NY, Z)
					&& FDungeonBoundaryRules::IsOpenCell(R.Grid.GetCell(NX, NY, Z).CellType);
				SharedFacesSeen += bShared ? 1 : 0;
				const int32 Count = ByFace.FindRef(Face);
				TestEqual(FString::Printf(TEXT("seed %lld cell %s dir %d (%s): one wall-family piece"),
					Seed, *Cell.ToString(), D, bShared ? TEXT("shared") : TEXT("rock")), Count, 1);
			}
		}
	}
	TestTrue(TEXT("generated dungeons contained shared open faces (test is meaningful)"), SharedFacesSeen > 0);

	CleanupTileSet(TS);
	Generator->RemoveFromRoot();
	Config->RemoveFromRoot();
	return true;
}
