// Test_DungeonStaircaseFlanks.cpp — Generation invariant: nothing walkable touches a ramp's side.
//
// A staircase is entered along its climb axis only. Its body cells (the inclined ramp) and the
// headroom cells above them (the shaft) have two flank faces, and any hallway-family or Door cell
// carved cardinally across a flank produces a corridor that runs into the side of the incline: a
// blind wall at best, a one-sided wall or an opening onto the ramp at worst, and wall modules that
// clip the ramp mesh. The pathfinder keeps stair ZONES one cell apart, but a plain Hallway (including
// another staircase's landing, or the staircase's own corridor) may be routed straight along a
// flank. Measured in the DungeonTest map (seed 28377955, 25x25x5): 4 of 11 staircases had a hallway
// on a body flank and 3 had a same-index hallway beside their shaft.
//
// This test is the RED test for that layout defect. It will fail until the pathfinder treats the
// flanks of every staircase as keep-out for hallway cells.

#include "Misc/AutomationTest.h"
#include "DungeonTypes.h"
#include "DungeonConfig.h"
#include "DungeonGenerator.h"
#include "DungeonBoundaryRules.h"

namespace DungeonStaircaseFlankTestHelpers
{
	/** The DungeonTest map's configuration (DA /Game/PluginTesting/Config/DungeonConfig). */
	UDungeonConfiguration* CreateDungeonTestConfig()
	{
		UDungeonConfiguration* Config = NewObject<UDungeonConfiguration>();
		Config->AddToRoot();
		Config->GridSize = FIntVector(25, 25, 5);
		Config->CellWorldSize = 600.0f;
		Config->RoomCount = 8;
		Config->MinRoomSize = FIntVector(3, 3, 1);
		Config->MaxRoomSize = FIntVector(7, 7, 2);
		Config->RoomBuffer = 2;
		Config->MaxPlacementAttempts = 155;
		Config->EdgeReadditionChance = 0.125f;
		Config->HallwayMergeCostMultiplier = 0.5f;
		Config->RoomPassthroughCostMultiplier = 3.0f;
		Config->StaircaseRiseToRun = 2;
		Config->StaircaseHeadroom = 2;
		Config->bUseFixedSeed = true;
		return Config;
	}

	static const int32 DX[4] = {1, -1, 0, 0};
	static const int32 DY[4] = {0, 0, 1, -1};

	/**
	 * Every (stair cell, flank neighbour) pair where the neighbour is a hallway-family or Door
	 * cell, formatted for the failure message.
	 */
	TArray<FString> FindFlankViolations(const FDungeonGrid& Grid)
	{
		TArray<FString> Violations;
		for (int32 Z = 0; Z < Grid.GridSize.Z; ++Z)
		{
			for (int32 Y = 0; Y < Grid.GridSize.Y; ++Y)
			{
				for (int32 X = 0; X < Grid.GridSize.X; ++X)
				{
					const FDungeonCell& Cell = Grid.GetCell(X, Y, Z);
					if (Cell.CellType != EDungeonCellType::Staircase && Cell.CellType != EDungeonCellType::StaircaseHead)
					{
						continue;
					}
					for (int32 D = 0; D < 4; ++D)
					{
						if (!FDungeonBoundaryRules::IsStairSideFace(Cell, DX[D], DY[D]))
						{
							continue;
						}
						const int32 NX = X + DX[D], NY = Y + DY[D];
						if (!Grid.IsInBounds(NX, NY, Z))
						{
							continue;
						}
						const FDungeonCell& N = Grid.GetCell(NX, NY, Z);
						if (FDungeonBoundaryRules::IsHallwayFamily(N.CellType) || N.CellType == EDungeonCellType::Door)
						{
							Violations.Add(FString::Printf(TEXT("%s(%d,%d,%d) hall %d dir %d has %s(%d,%d,%d) hall %d on its flank"),
								Cell.CellType == EDungeonCellType::Staircase ? TEXT("Staircase") : TEXT("StaircaseHead"),
								X, Y, Z, Cell.HallwayIndex, Cell.StaircaseDirection,
								N.CellType == EDungeonCellType::Hallway ? TEXT("Hallway")
									: N.CellType == EDungeonCellType::Door ? TEXT("Door")
									: N.CellType == EDungeonCellType::Staircase ? TEXT("Staircase") : TEXT("StaircaseHead"),
								NX, NY, Z, N.HallwayIndex));
						}
					}
				}
			}
		}
		return Violations;
	}
}

// ============================================================================
// No hallway-family or Door cell on any staircase flank, over a seed sweep
// ============================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonStaircaseFlanksClear, "Dungeon.Generation.Staircase.FlanksHaveNoHallway",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonStaircaseFlanksClear::RunTest(const FString& Parameters)
{
	using namespace DungeonStaircaseFlankTestHelpers;

	// The DungeonTest map's seed first, then a spread of others on the same config.
	const int64 Seeds[] = { 28377955, 12345, 77777, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };

	UDungeonConfiguration* Config = CreateDungeonTestConfig();
	UDungeonGenerator* Generator = NewObject<UDungeonGenerator>();
	Generator->AddToRoot();

	int32 SeedsWithStairs = 0;
	for (const int64 Seed : Seeds)
	{
		Config->FixedSeed = Seed;
		const FDungeonResult Result = Generator->Generate(Config, Seed);
		if (Result.Staircases.Num() > 0)
		{
			++SeedsWithStairs;
		}

		const TArray<FString> Violations = FindFlankViolations(Result.Grid);
		for (const FString& V : Violations)
		{
			AddError(FString::Printf(TEXT("Seed %lld: %s"), Seed, *V));
		}
	}

	TestTrue(TEXT("The sweep exercised staircases"), SeedsWithStairs > 0);

	Generator->RemoveFromRoot();
	Config->RemoveFromRoot();
	return true;
}
