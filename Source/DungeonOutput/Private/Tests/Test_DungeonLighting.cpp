// Test_DungeonLighting.cpp — Environment E5: hallway wall lights and the light-tightness coverage
// report (every needed wall / floor / ceiling has exactly one piece on generated dungeons).
#include "Misc/AutomationTest.h"
#include "DungeonTypes.h"
#include "DungeonConfig.h"
#include "DungeonGenerator.h"
#include "DungeonTileSet.h"
#include "DungeonTileMapper.h"
#include "DungeonCoverage.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace DungeonLightingTestHelpers
{
	UDungeonTileSet* MakeLightingTileSet()
	{
		UDungeonTileSet* TS = NewObject<UDungeonTileSet>();
		TS->AddToRoot();
		TS->Slots.Add(EDungeonTileType::WallPartition, TS->GetSlot(EDungeonTileType::WallSegment));
		return TS;
	}

	/** 7x5x1 corridor along y=2 from x=1..5 (hallway index 1). */
	FDungeonResult MakeLightingCorridor()
	{
		FDungeonResult R;
		R.GridSize = FIntVector(7, 5, 1); R.CellWorldSize = 400.0f; R.Grid.Initialize(R.GridSize);
		for (int32 X = 1; X <= 5; ++X)
		{ FDungeonCell& C = R.Grid.GetCell(X, 2, 0); C.CellType = EDungeonCellType::Hallway; C.HallwayIndex = 1; }
		R.EntranceRoomIndex = -1;
		return R;
	}
}

using namespace DungeonLightingTestHelpers;

#define LIGHTING_TEST(ClassName, PrettyName) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(ClassName, PrettyName, EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// ---------------------------------------------------------------------------
// Hallway lights: every Nth rock-backed hallway face, after the room lights, under the cap; off
// by default.
// ---------------------------------------------------------------------------
LIGHTING_TEST(FLightingHallwayLights, "Dungeon.Lighting.HallwayWallLights")
bool FLightingHallwayLights::RunTest(const FString& Parameters)
{
	UDungeonTileSet* TS = MakeLightingTileSet();
	TS->FixtureRules.RoomWallLightEvery = 0;
	// Default: corridors stay dark.
	{
		const FDungeonTileMapResult Map = FDungeonTileMapper::MapToTiles(MakeLightingCorridor(), *TS, FVector::ZeroVector);
		TestEqual(TEXT("default: no hallway lights"), Map.Fixtures.Num(), 0);
	}
	// The 5-cell corridor has 12 rock-backed faces (2 per cell + the two ends): every 3rd -> 4,
	// every face -> 12, all on hallway cells at the mount height on a finished face.
	{
		TS->FixtureRules.HallwayWallLightEvery = 3;
		const FDungeonTileMapResult Map = FDungeonTileMapper::MapToTiles(MakeLightingCorridor(), *TS, FVector::ZeroVector);
		TestEqual(TEXT("every 3rd of 12 -> 4"), Map.Fixtures.Num(), 4);
		for (const FDungeonFixture& F : Map.Fixtures)
		{
			TestTrue(TEXT("on a hallway cell"), F.Cell.Y == 2 && F.Cell.X >= 1 && F.Cell.X <= 5);
			TestTrue(TEXT("mount height"), FMath::IsNearlyEqual(F.Anchor.GetLocation().Z, 190.0f, 0.5f));
		}
		TS->FixtureRules.HallwayWallLightEvery = 1;
		TestEqual(TEXT("every face -> 12"), FDungeonTileMapper::MapToTiles(MakeLightingCorridor(), *TS, FVector::ZeroVector).Fixtures.Num(), 12);
		TS->FixtureRules.MaxWallLights = 5;
		TestEqual(TEXT("cap 5"), FDungeonTileMapper::MapToTiles(MakeLightingCorridor(), *TS, FVector::ZeroVector).Fixtures.Num(), 5);
	}
	TS->RemoveFromRoot();
	return true;
}

// ---------------------------------------------------------------------------
// Coverage: generated dungeons are light-tight (no needed boundary without a piece, no face with
// two), with and without the open entrance; removing the wall slot is reported.
// ---------------------------------------------------------------------------
LIGHTING_TEST(FLightingCoverage, "Dungeon.Coverage.GeneratedDungeonsLightTight")
bool FLightingCoverage::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = NewObject<UDungeonConfiguration>();
	Config->AddToRoot();
	Config->GridSize = FIntVector(24, 24, 3);
	Config->RoomCount = 8;
	Config->MinRoomSize = FIntVector(3, 3, 1);
	Config->MaxRoomSize = FIntVector(6, 6, 2);
	Config->RoomBuffer = 1;
	UDungeonGenerator* Generator = NewObject<UDungeonGenerator>();
	Generator->AddToRoot();
	UDungeonTileSet* TS = MakeLightingTileSet();

	int32 Checked = 0;
	for (int64 Seed = 1; Seed <= 10; ++Seed)
	{
		const FDungeonResult R = Generator->Generate(Config, Seed);
		if (R.Rooms.Num() < 2) { continue; }
		++Checked;
		const FVector Offset(1000.0f * Seed, -500.0f, 250.0f);
		for (bool bOpen : { false, true })
		{
			const FDungeonTileMapResult Map = FDungeonTileMapper::MapToTiles(R, *TS, Offset, bOpen);
			const FDungeonCoverageReport Report = FDungeonCoverage::Analyse(R, Map, Offset, bOpen);
			TestTrue(FString::Printf(TEXT("seed %lld open=%d: report has boundaries"), Seed, bOpen ? 1 : 0), Report.WallFacesNeeded > 0 && Report.FloorsNeeded > 0 && Report.CeilingsNeeded > 0);
			TestTrue(FString::Printf(TEXT("seed %lld open=%d: light-tight\n%s"), Seed, bOpen ? 1 : 0, *Report.Describe()), Report.IsLightTight());
			TestEqual(TEXT("problem lists parallel"), Report.Problems.Num(), Report.ProblemLocations.Num());
		}
	}
	TestTrue(TEXT("checked generated dungeons"), Checked > 0);

	// Negative: no wall slot at all -> every walled face is reported uncovered.
	{
		const FDungeonResult R = Generator->Generate(Config, 3);
		UDungeonTileSet* Bare = NewObject<UDungeonTileSet>();
		Bare->AddToRoot();
		Bare->Slots.Remove(EDungeonTileType::WallSegment);
		Bare->Slots.Remove(EDungeonTileType::DoorFrame);
		Bare->Slots.Remove(EDungeonTileType::EntranceFrame);
		const FDungeonTileMapResult Map = FDungeonTileMapper::MapToTiles(R, *Bare, FVector::ZeroVector);
		const FDungeonCoverageReport Report = FDungeonCoverage::Analyse(R, Map, FVector::ZeroVector, false);
		TestTrue(TEXT("no walls: not light-tight"), !Report.IsLightTight());
		TestEqual(TEXT("no walls: every walled face uncovered"), Report.WallFacesUncovered, Report.WallFacesNeeded);
		TestEqual(TEXT("no walls: floors still covered"), Report.FloorsUncovered, 0);
		TestTrue(TEXT("no walls: problems listed"), Report.Problems.Num() == Report.WallFacesUncovered);
		Bare->RemoveFromRoot();
	}

	TS->RemoveFromRoot();
	Generator->RemoveFromRoot();
	Config->RemoveFromRoot();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
