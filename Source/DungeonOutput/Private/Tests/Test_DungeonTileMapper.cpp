// Test_DungeonTileMapper.cpp — Unit and integration tests for the tile mapping algorithm
#include "Misc/AutomationTest.h"
#include "DungeonTypes.h"
#include "DungeonConfig.h"
#include "DungeonGenerator.h"
#include "DungeonTileSet.h"
#include "DungeonTileMapper.h"

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

	// Place a staircase cell at (1,1,0) going +X
	Result.Grid.GetCell(1, 1, 0).CellType = EDungeonCellType::Staircase;
	Result.Grid.GetCell(1, 1, 0).StaircaseDirection = 0; // +X
	Result.EntranceRoomIndex = -1;

	FDungeonTileMapResult TileMap = FDungeonTileMapper::MapToTiles(Result, *TS, FVector::ZeroVector);

	const int32 StairCount = TileMap.Transforms[static_cast<int32>(EDungeonTileType::StaircaseMesh)].Num();
	TestEqual(TEXT("Single staircase cell should produce 1 StaircaseMesh"), StairCount, 1);

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

	// Direction: 0=+X(0°), 1=-X(180°), 2=+Y(90°), 3=-Y(-90°)
	const float ExpectedYaws[] = { 0.0f, 180.0f, 90.0f, -90.0f };

	for (int32 Dir = 0; Dir < 4; ++Dir)
	{
		FDungeonResult Result;
		Result.GridSize = FIntVector(3, 3, 1);
		Result.CellWorldSize = 400.0f;
		Result.Grid.Initialize(FIntVector(3, 3, 1));
		Result.Grid.GetCell(1, 1, 0).CellType = EDungeonCellType::Staircase;
		Result.Grid.GetCell(1, 1, 0).StaircaseDirection = static_cast<uint8>(Dir);
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
				FMath::IsNearlyEqual(ActualYaw, ExpectedYaws[Dir], 0.1f));
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
