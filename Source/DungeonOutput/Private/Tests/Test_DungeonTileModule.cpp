// Test_DungeonTileModule.cpp — Tile module system (P1): the mapper emits a clean uniform-scale
// anchor for module-backed types, and modules don't disturb the legacy per-type placement.
#include "Misc/AutomationTest.h"
#include "DungeonTypes.h"
#include "DungeonTileSet.h"
#include "DungeonTileMapper.h"
#include "DungeonTileModule.h"
#include "Engine/StaticMesh.h"

namespace DungeonTileModuleTestHelpers
{
	// A 3x3 room at (1,1,0) in a 5x5x1 grid; floor below is OOB so all 9 cells get floors.
	FDungeonResult MakeSingleRoom(float CellWorldSize)
	{
		FDungeonResult Result;
		Result.GridSize = FIntVector(5, 5, 1);
		Result.CellWorldSize = CellWorldSize;
		Result.Grid.Initialize(FIntVector(5, 5, 1));
		for (int32 Y = 0; Y < 5; ++Y)
			for (int32 X = 0; X < 5; ++X)
			{
				Result.Grid.GetCell(X, Y, 0).CellType = EDungeonCellType::RoomWall;
				Result.Grid.GetCell(X, Y, 0).RoomIndex = 1;
			}
		for (int32 Y = 1; Y <= 3; ++Y)
			for (int32 X = 1; X <= 3; ++X)
			{
				Result.Grid.GetCell(X, Y, 0).CellType = EDungeonCellType::Room;
				Result.Grid.GetCell(X, Y, 0).RoomIndex = 1;
			}
		FDungeonRoom Room;
		Room.RoomIndex = 1; Room.Position = FIntVector(1, 1, 0); Room.Size = FIntVector(3, 3, 1);
		Room.Center = FIntVector(2, 2, 0);
		Result.Rooms.Add(Room);
		Result.EntranceRoomIndex = -1;
		return Result;
	}

	UDungeonTileModule* MakeModule(float ReferenceCellSize, int32 ElementCount)
	{
		UDungeonTileModule* Module = NewObject<UDungeonTileModule>();
		Module->AddToRoot();
		Module->ReferenceCellSize = ReferenceCellSize;
		const FSoftObjectPath Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
		for (int32 i = 0; i < ElementCount; ++i)
		{
			FDungeonModuleElement E;
			E.Mesh = TSoftObjectPtr<UStaticMesh>(Cube);
			E.RelativeTransform = FTransform(FVector(0.0f, 0.0f, static_cast<float>(i) * 10.0f));
			Module->Elements.Add(E);
		}
		return Module;
	}
}

// Module on RoomFloor: 9 anchors (one per cell), each with a UNIFORM scale = CS / ReferenceCellSize
// and no pivot correction. This is the clean anchor the actor expands module elements against.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTileModuleUniformAnchor,
	"Dungeon.TileModule.RoomFloor.UniformAnchorNoPivot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTileModuleUniformAnchor::RunTest(const FString& Parameters)
{
	using namespace DungeonTileModuleTestHelpers;

	UDungeonTileSet* TS = NewObject<UDungeonTileSet>();
	TS->AddToRoot();

	// RoomFloor is a module authored at 200; place at cell 400 -> uniform scale 2.0.
	UDungeonTileModule* Module = MakeModule(/*ReferenceCellSize=*/200.0f, /*ElementCount=*/2);
	TS->TileModules.Add(EDungeonTileType::RoomFloor, TSoftObjectPtr<UDungeonTileModule>(Module));

	FDungeonResult Result = MakeSingleRoom(/*CellWorldSize=*/400.0f);
	FDungeonTileMapResult TileMap = FDungeonTileMapper::MapToTiles(Result, *TS, FVector::ZeroVector);

	const TArray<FTransform>& Floors = TileMap.Transforms[static_cast<int32>(EDungeonTileType::RoomFloor)];
	TestEqual(TEXT("Module-backed RoomFloor still produces one anchor per cell"), Floors.Num(), 9);

	for (const FTransform& Xf : Floors)
	{
		const FVector S = Xf.GetScale3D();
		TestTrue(TEXT("Anchor scale is uniform 2.0 (400/200)"),
			FMath::IsNearlyEqual(S.X, 2.0f, 0.001f) && FMath::IsNearlyEqual(S.Y, 2.0f, 0.001f)
			&& FMath::IsNearlyEqual(S.Z, 2.0f, 0.001f));
	}

	Module->RemoveFromRoot();
	TS->RemoveFromRoot();
	return true;
}

// A tileset with NO modules must be byte-identical to before the feature: the module path is a
// no-op. (Uses the RoomFloor count as the witness; the wider legacy suite covers the rest.)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTileModuleBackwardCompat,
	"Dungeon.TileModule.NoModules.MatchesLegacy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTileModuleBackwardCompat::RunTest(const FString& Parameters)
{
	using namespace DungeonTileModuleTestHelpers;

	UDungeonTileSet* TS = NewObject<UDungeonTileSet>();
	TS->AddToRoot();

	FDungeonResult Result = MakeSingleRoom(400.0f);
	FDungeonTileMapResult TileMap = FDungeonTileMapper::MapToTiles(Result, *TS, FVector::ZeroVector);

	const TArray<FTransform>& Floors = TileMap.Transforms[static_cast<int32>(EDungeonTileType::RoomFloor)];
	TestEqual(TEXT("No-module tileset: 9 room floors, legacy path"), Floors.Num(), 9);

	// Legacy floor uses the default-cube auto-fit (extent 100 -> scale 4.0 on X/Y), NOT uniform —
	// proving the module path did not alter legacy placement.
	if (Floors.Num() > 0)
	{
		const FVector S = Floors[0].GetScale3D();
		TestTrue(TEXT("Legacy floor keeps per-axis auto-fit (X scale != uniform-1)"),
			!FMath::IsNearlyEqual(S.X, 1.0f, 0.001f));
	}

	TS->RemoveFromRoot();
	return true;
}
