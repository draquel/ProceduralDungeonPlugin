// Test_DungeonTileMapperStairFlanks.cpp — The tile mapper places staircase flank walls where the
// shared boundary rules say, and from the side that will not cut the ramp mesh.
//
// A flank of a Staircase / StaircaseHead cell is a wall from both sides of the face (rule 5). The
// mapper places that wall on the OPEN neighbour's face (a hallway running alongside the ramp), and
// only on the stair cell's own face when the neighbour is solid or out of bounds. The mapper used
// to wall stair flanks locally and one-sidedly from the stair cell, which put wall modules inset
// into the ramp mesh and left the hallway's face open in the voxel backend.

#include "Misc/AutomationTest.h"
#include "DungeonTypes.h"
#include "DungeonTileSet.h"
#include "DungeonTileMapper.h"

namespace DungeonTileMapperStairFlankTestHelpers
{
	/** Wall segments placed on a given cell's face (matched by placement yaw and cell proximity). */
	int32 CountWallsOnFace(const FDungeonTileMapResult& TileMap, const FVector& CellCenter, float CS, float Yaw)
	{
		int32 Count = 0;
		// Wall-family pieces: a rock-backed WallSegment or, on a face shared with another open cell
		// (a flank seen from a corridor), the two-faced WallPartition placed by the owning side.
		for (EDungeonTileType WallType : { EDungeonTileType::WallSegment, EDungeonTileType::WallPartition })
		for (const FTransform& T : TileMap.Transforms[static_cast<int32>(WallType)])
		{
			if (FMath::Abs(FRotator::NormalizeAxis(T.Rotator().Yaw - Yaw)) > 1.0f)
			{
				continue;
			}
			// A wall on this cell's face lies about half a cell away along the face normal (the
			// mesh pivot may shift it slightly) and within the cell's footprint laterally.
			const FVector D = T.GetLocation() - CellCenter;
			const FVector Normal = FRotator(0.0f, Yaw, 0.0f).Vector();
			const float Along = FVector::DotProduct(D, Normal);
			const FVector Lateral = D - Normal * Along;
			if (Along < CS * 0.25f || Along > CS * 0.75f)
			{
				continue;
			}
			// Wall pivots sit within the cell's own height band (the mapper offsets them half a cell
			// up); the band excludes the cells directly above and below.
			if (FMath::Abs(Lateral.X) > CS * 0.5f || FMath::Abs(Lateral.Y) > CS * 0.5f || D.Z < -CS * 0.1f || D.Z > CS * 0.9f)
			{
				continue;
			}
			++Count;
		}
		return Count;
	}
}

// ============================================================================
// Hallway alongside a ramp: the hallway walls its face, the stair does not wall into the ramp
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTileMapperStairFlankWalls, "Dungeon.TileMapper.Staircase.FlankWallsPlacedFromOpenSide",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTileMapperStairFlankWalls::RunTest(const FString& Parameters)
{
	using namespace DungeonTileMapperStairFlankTestHelpers;

	UDungeonTileSet* TS = NewObject<UDungeonTileSet>();
	TS->AddToRoot();

	// 3x3x2 grid. Staircase body at (1,1,0) climbing +X (flanks are its +Y and -Y faces), a
	// same-index Hallway on its +Y flank at (1,2,0), Empty on its -Y flank at (1,0,0).
	FDungeonResult Result;
	Result.GridSize = FIntVector(3, 3, 2);
	Result.CellWorldSize = 400.0f;
	Result.Grid.Initialize(FIntVector(3, 3, 2));

	FDungeonCell& Stair = Result.Grid.GetCell(1, 1, 0);
	Stair.CellType = EDungeonCellType::Staircase;
	Stair.HallwayIndex = 1;
	Stair.StaircaseDirection = 0;

	FDungeonCell& Hall = Result.Grid.GetCell(1, 2, 0);
	Hall.CellType = EDungeonCellType::Hallway;
	Hall.HallwayIndex = 1;

	FDungeonStaircase Staircase;
	Staircase.BottomCell = FIntVector(0, 1, 0);
	Staircase.TopCell = FIntVector(2, 1, 1);
	Staircase.Direction = 0;
	Staircase.RiseRunRatio = 1;
	Result.Staircases.Add(Staircase);
	Result.EntranceRoomIndex = -1;

	const FDungeonTileMapResult TileMap = FDungeonTileMapper::MapToTiles(Result, *TS, FVector::ZeroVector);

	const float CS = Result.CellWorldSize;
	const FVector StairCenter = Result.GridToWorld(FIntVector(1, 1, 0)) + FVector(CS * 0.5f, CS * 0.5f, 0.0f);
	const FVector HallCenter = Result.GridToWorld(FIntVector(1, 2, 0)) + FVector(CS * 0.5f, CS * 0.5f, 0.0f);

	// Face yaws as the mapper places them: +X 0, -X 180, +Y 90, -Y -90.
	TestEqual(TEXT("hallway walls its -Y face toward the ramp flank"), CountWallsOnFace(TileMap, HallCenter, CS, -90.0f), 1);
	TestEqual(TEXT("stair places no wall on its +Y flank into the hallway (the hallway owns it)"), CountWallsOnFace(TileMap, StairCenter, CS, 90.0f), 0);
	TestEqual(TEXT("stair walls its -Y flank against Empty"), CountWallsOnFace(TileMap, StairCenter, CS, -90.0f), 1);

	TS->RemoveFromRoot();
	return true;
}

// ============================================================================
// Two-floor room above the doorway a ramp starts from: its upper airspace faces the low
// headroom's ENTRY face and must be walled (from the room's side); the ramp's top still opens
// onto its exit landing.
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTileMapperHeadroomEntryWalled, "Dungeon.TileMapper.Staircase.HeadroomEntryFaceWalledFromRoom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FTileMapperHeadroomEntryWalled::RunTest(const FString& Parameters)
{
	using namespace DungeonTileMapperStairFlankTestHelpers;

	UDungeonTileSet* TS = NewObject<UDungeonTileSet>();
	TS->AddToRoot();

	// 3x4x3 grid, ramp climbing +Y. Room 4 occupies (1,0,0..1): its Door (1,0,0) is the entry
	// landing, its upper cell (1,0,1) faces the low headroom (1,1,1). Body (1,1,0), headroom
	// (1,1,1) and (1,1,2), exit landing Hallway (1,2,1).
	FDungeonResult Result;
	Result.GridSize = FIntVector(3, 4, 3);
	Result.CellWorldSize = 400.0f;
	Result.Grid.Initialize(FIntVector(3, 4, 3));

	auto Set = [&](int32 X, int32 Y, int32 Z, EDungeonCellType Type, uint8 Room, uint8 Hall)
	{
		FDungeonCell& C = Result.Grid.GetCell(X, Y, Z);
		C.CellType = Type; C.RoomIndex = Room; C.HallwayIndex = Hall; C.StaircaseDirection = 2;
	};
	Set(1, 0, 0, EDungeonCellType::Door, 4, 1);
	Set(1, 0, 1, EDungeonCellType::Room, 4, 0);
	Set(1, 1, 0, EDungeonCellType::Staircase, 0, 1);
	Set(1, 1, 1, EDungeonCellType::StaircaseHead, 0, 1);
	Set(1, 1, 2, EDungeonCellType::StaircaseHead, 0, 1);
	Set(1, 2, 1, EDungeonCellType::Hallway, 0, 1);

	FDungeonStaircase Staircase;
	Staircase.BottomCell = FIntVector(1, 0, 0);
	Staircase.TopCell = FIntVector(1, 2, 1);
	Staircase.Direction = 2;
	Staircase.RiseRunRatio = 1;
	Result.Staircases.Add(Staircase);
	Result.EntranceRoomIndex = -1;

	const FDungeonTileMapResult TileMap = FDungeonTileMapper::MapToTiles(Result, *TS, FVector::ZeroVector);

	const float CS = Result.CellWorldSize;
	auto Center = [&](int32 X, int32 Y, int32 Z) { return Result.GridToWorld(FIntVector(X, Y, Z)) + FVector(CS * 0.5f, CS * 0.5f, 0.0f); };

	// Room's upper airspace walls its +Y face toward the headroom entry face.
	TestEqual(TEXT("room upper cell walls its +Y face toward the low headroom"), CountWallsOnFace(TileMap, Center(1, 0, 1), CS, 90.0f), 1);
	// The headroom never places a wall into the open room (the room owns that wall).
	TestEqual(TEXT("low headroom places no wall on its -Y entry face into the room"), CountWallsOnFace(TileMap, Center(1, 1, 1), CS, -90.0f), 0);
	// The ramp's top stays open: no wall between the low headroom and the exit landing, from either side.
	TestEqual(TEXT("low headroom climb face open"), CountWallsOnFace(TileMap, Center(1, 1, 1), CS, 90.0f), 0);
	TestEqual(TEXT("exit landing open toward the ramp"), CountWallsOnFace(TileMap, Center(1, 2, 1), CS, -90.0f), 0);
	// Upper headroom walls its climb face against the solid cell above the landing.
	TestEqual(TEXT("upper headroom walls its climb face against Empty"), CountWallsOnFace(TileMap, Center(1, 1, 2), CS, 90.0f), 1);
	// The doorway at the ramp's foot stays open (the Door places its frame, not a wall).
	TestEqual(TEXT("door -> ramp foot has no wall"), CountWallsOnFace(TileMap, Center(1, 0, 0), CS, 90.0f), 0);

	TS->RemoveFromRoot();
	return true;
}
