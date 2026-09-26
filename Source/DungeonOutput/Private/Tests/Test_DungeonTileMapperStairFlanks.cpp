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
		for (const FTransform& T : TileMap.Transforms[static_cast<int32>(EDungeonTileType::WallSegment)])
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
			if (FMath::Abs(Lateral.X) > CS * 0.5f || FMath::Abs(Lateral.Y) > CS * 0.5f)
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
