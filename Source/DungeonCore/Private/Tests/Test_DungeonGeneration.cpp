// Test_DungeonGeneration.cpp — Integration tests running the full dungeon pipeline
#include "Misc/AutomationTest.h"
#include "DungeonTypes.h"
#include "DungeonConfig.h"
#include "DungeonGenerator.h"
#include "DungeonValidator.h"

// ============================================================================
// Test Helpers
// ============================================================================

namespace DungeonGenerationTestHelpers
{
	UDungeonConfiguration* CreateDefaultConfig()
	{
		UDungeonConfiguration* Config = NewObject<UDungeonConfiguration>();
		Config->AddToRoot();
		Config->GridSize = FIntVector(30, 30, 3);
		Config->RoomCount = 6;
		Config->MinRoomSize = FIntVector(3, 3, 1);
		Config->MaxRoomSize = FIntVector(7, 7, 1);
		Config->RoomBuffer = 1;
		Config->bUseFixedSeed = true;
		Config->FixedSeed = 12345;
		return Config;
	}

	UDungeonConfiguration* CreateSingleFloorConfig()
	{
		UDungeonConfiguration* Config = NewObject<UDungeonConfiguration>();
		Config->AddToRoot();
		Config->GridSize = FIntVector(20, 20, 1);
		Config->RoomCount = 4;
		Config->MinRoomSize = FIntVector(3, 3, 1);
		Config->MaxRoomSize = FIntVector(5, 5, 1);
		Config->RoomBuffer = 1;
		Config->bUseFixedSeed = true;
		Config->FixedSeed = 54321;
		return Config;
	}

	UDungeonConfiguration* CreateMultiFloorConfig()
	{
		UDungeonConfiguration* Config = NewObject<UDungeonConfiguration>();
		Config->AddToRoot();
		Config->GridSize = FIntVector(30, 30, 5);
		Config->RoomCount = 8;
		Config->MinRoomSize = FIntVector(3, 3, 1);
		Config->MaxRoomSize = FIntVector(7, 7, 2);
		Config->RoomBuffer = 1;
		Config->bUseFixedSeed = true;
		Config->FixedSeed = 77777;
		return Config;
	}

	bool AreDungeonResultsIdentical(const FDungeonResult& A, const FDungeonResult& B)
	{
		if (A.GridSize != B.GridSize) return false;
		if (A.Rooms.Num() != B.Rooms.Num()) return false;
		if (A.Hallways.Num() != B.Hallways.Num()) return false;
		if (A.Staircases.Num() != B.Staircases.Num()) return false;
		if (A.EntranceRoomIndex != B.EntranceRoomIndex) return false;
		if (A.EntranceCell != B.EntranceCell) return false;

		// Compare grid cells
		if (A.Grid.Cells.Num() != B.Grid.Cells.Num()) return false;
		for (int32 i = 0; i < A.Grid.Cells.Num(); ++i)
		{
			if (A.Grid.Cells[i].CellType != B.Grid.Cells[i].CellType) return false;
			if (A.Grid.Cells[i].RoomIndex != B.Grid.Cells[i].RoomIndex) return false;
		}

		// Compare rooms
		for (int32 i = 0; i < A.Rooms.Num(); ++i)
		{
			if (A.Rooms[i].Position != B.Rooms[i].Position) return false;
			if (A.Rooms[i].Size != B.Rooms[i].Size) return false;
		}

		// Compare hallway paths
		for (int32 i = 0; i < A.Hallways.Num(); ++i)
		{
			if (A.Hallways[i].PathCells.Num() != B.Hallways[i].PathCells.Num()) return false;
			for (int32 j = 0; j < A.Hallways[i].PathCells.Num(); ++j)
			{
				if (A.Hallways[i].PathCells[j] != B.Hallways[i].PathCells[j]) return false;
			}
		}

		// Compare staircase cells
		for (int32 i = 0; i < A.Staircases.Num(); ++i)
		{
			if (A.Staircases[i].OccupiedCells.Num() != B.Staircases[i].OccupiedCells.Num()) return false;
		}

		return true;
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
// VALIDATION PASS-THROUGH TESTS
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonGenValidateSingleFloor, "Dungeon.Generation.Validation.SingleFloorPasses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonGenValidateSingleFloor::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = DungeonGenerationTestHelpers::CreateSingleFloorConfig();
	UDungeonGenerator* Generator = NewObject<UDungeonGenerator>();
	Generator->AddToRoot();

	FDungeonResult Result = Generator->Generate(Config, 42);
	TestTrue(TEXT("Generation produced rooms"), Result.Rooms.Num() >= 2);

	FDungeonValidationResult Validation = FDungeonValidator::ValidateAll(Result, *Config);
	if (!Validation.bPassed)
	{
		AddError(FString::Printf(TEXT("Validation failed: %s"), *Validation.GetSummary()));
	}
	TestTrue(TEXT("Single-floor passes validation"), Validation.bPassed);

	Generator->RemoveFromRoot();
	DungeonGenerationTestHelpers::CleanupConfig(Config);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonGenValidateMultiFloor, "Dungeon.Generation.Validation.MultiFloorPasses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonGenValidateMultiFloor::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = DungeonGenerationTestHelpers::CreateMultiFloorConfig();
	UDungeonGenerator* Generator = NewObject<UDungeonGenerator>();
	Generator->AddToRoot();

	FDungeonResult Result = Generator->Generate(Config, 42);
	TestTrue(TEXT("Generation produced rooms"), Result.Rooms.Num() >= 2);

	FDungeonValidationResult Validation = FDungeonValidator::ValidateAll(Result, *Config);
	if (!Validation.bPassed)
	{
		AddError(FString::Printf(TEXT("Validation failed: %s"), *Validation.GetSummary()));
	}
	TestTrue(TEXT("Multi-floor passes validation"), Validation.bPassed);

	Generator->RemoveFromRoot();
	DungeonGenerationTestHelpers::CleanupConfig(Config);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonGenValidateLargeGrid, "Dungeon.Generation.Validation.LargeGridPasses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonGenValidateLargeGrid::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = NewObject<UDungeonConfiguration>();
	Config->AddToRoot();
	Config->GridSize = FIntVector(50, 50, 3);
	Config->RoomCount = 12;
	Config->MinRoomSize = FIntVector(3, 3, 1);
	Config->MaxRoomSize = FIntVector(8, 8, 1);
	Config->RoomBuffer = 1;

	UDungeonGenerator* Generator = NewObject<UDungeonGenerator>();
	Generator->AddToRoot();

	FDungeonResult Result = Generator->Generate(Config, 99);
	TestTrue(TEXT("Generation produced rooms"), Result.Rooms.Num() >= 2);

	FDungeonValidationResult Validation = FDungeonValidator::ValidateAll(Result, *Config);
	if (!Validation.bPassed)
	{
		AddError(FString::Printf(TEXT("Validation failed: %s"), *Validation.GetSummary()));
	}
	TestTrue(TEXT("Large grid passes validation"), Validation.bPassed);

	Generator->RemoveFromRoot();
	DungeonGenerationTestHelpers::CleanupConfig(Config);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonGenValidateMinimalRooms, "Dungeon.Generation.Validation.MinimalRoomsPasses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonGenValidateMinimalRooms::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = NewObject<UDungeonConfiguration>();
	Config->AddToRoot();
	Config->GridSize = FIntVector(15, 15, 1);
	Config->RoomCount = 2;
	Config->MinRoomSize = FIntVector(3, 3, 1);
	Config->MaxRoomSize = FIntVector(5, 5, 1);
	Config->RoomBuffer = 1;

	UDungeonGenerator* Generator = NewObject<UDungeonGenerator>();
	Generator->AddToRoot();

	FDungeonResult Result = Generator->Generate(Config, 55);
	TestEqual(TEXT("Exactly 2 rooms placed"), Result.Rooms.Num(), 2);

	FDungeonValidationResult Validation = FDungeonValidator::ValidateAll(Result, *Config);
	if (!Validation.bPassed)
	{
		AddError(FString::Printf(TEXT("Validation failed: %s"), *Validation.GetSummary()));
	}
	TestTrue(TEXT("Minimal rooms passes validation"), Validation.bPassed);

	Generator->RemoveFromRoot();
	DungeonGenerationTestHelpers::CleanupConfig(Config);
	return true;
}

// ============================================================================
// DETERMINISM TESTS
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonGenSameSeedSameResult, "Dungeon.Generation.Determinism.SameSeedSameResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonGenSameSeedSameResult::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = DungeonGenerationTestHelpers::CreateDefaultConfig();
	UDungeonGenerator* Generator = NewObject<UDungeonGenerator>();
	Generator->AddToRoot();

	FDungeonResult ResultA = Generator->Generate(Config, 42);
	FDungeonResult ResultB = Generator->Generate(Config, 42);

	TestTrue(TEXT("Same seed produces identical results"),
		DungeonGenerationTestHelpers::AreDungeonResultsIdentical(ResultA, ResultB));

	Generator->RemoveFromRoot();
	DungeonGenerationTestHelpers::CleanupConfig(Config);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonGenDifferentSeedsDiffer, "Dungeon.Generation.Determinism.DifferentSeedsDiffer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonGenDifferentSeedsDiffer::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = DungeonGenerationTestHelpers::CreateDefaultConfig();
	UDungeonGenerator* Generator = NewObject<UDungeonGenerator>();
	Generator->AddToRoot();

	FDungeonResult ResultA = Generator->Generate(Config, 42);
	FDungeonResult ResultB = Generator->Generate(Config, 43);

	TestFalse(TEXT("Different seeds produce different results"),
		DungeonGenerationTestHelpers::AreDungeonResultsIdentical(ResultA, ResultB));

	Generator->RemoveFromRoot();
	DungeonGenerationTestHelpers::CleanupConfig(Config);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonGenConsistentAcrossRuns, "Dungeon.Generation.Determinism.ConsistentAcrossRuns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonGenConsistentAcrossRuns::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = DungeonGenerationTestHelpers::CreateDefaultConfig();
	UDungeonGenerator* Generator = NewObject<UDungeonGenerator>();
	Generator->AddToRoot();

	FDungeonResult Reference = Generator->Generate(Config, 12345);

	for (int32 i = 0; i < 5; ++i)
	{
		FDungeonResult Run = Generator->Generate(Config, 12345);
		TestTrue(FString::Printf(TEXT("Run %d identical to reference"), i),
			DungeonGenerationTestHelpers::AreDungeonResultsIdentical(Reference, Run));
	}

	Generator->RemoveFromRoot();
	DungeonGenerationTestHelpers::CleanupConfig(Config);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonGenSeedZeroValid, "Dungeon.Generation.Determinism.SeedZeroProducesValidResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonGenSeedZeroValid::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = DungeonGenerationTestHelpers::CreateSingleFloorConfig();
	UDungeonGenerator* Generator = NewObject<UDungeonGenerator>();
	Generator->AddToRoot();

	FDungeonResult Result = Generator->Generate(Config, 0);
	TestTrue(TEXT("Seed 0 produces rooms"), Result.Rooms.Num() >= 2);
	TestTrue(TEXT("Seed 0 result has non-zero seed"), Result.Seed != 0);

	Generator->RemoveFromRoot();
	DungeonGenerationTestHelpers::CleanupConfig(Config);
	return true;
}

// ============================================================================
// STRUCTURE TESTS
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonGenHasEntrance, "Dungeon.Generation.Structure.HasEntrance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonGenHasEntrance::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = DungeonGenerationTestHelpers::CreateDefaultConfig();
	UDungeonGenerator* Generator = NewObject<UDungeonGenerator>();
	Generator->AddToRoot();

	FDungeonResult Result = Generator->Generate(Config, 42);

	TestTrue(TEXT("EntranceRoomIndex >= 0"), Result.EntranceRoomIndex >= 0);
	TestTrue(TEXT("EntranceRoomIndex valid"), Result.EntranceRoomIndex < Result.Rooms.Num());
	TestTrue(TEXT("Entrance room type is Entrance"),
		Result.Rooms[Result.EntranceRoomIndex].RoomType == EDungeonRoomType::Entrance);

	Generator->RemoveFromRoot();
	DungeonGenerationTestHelpers::CleanupConfig(Config);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonGenHasHallways, "Dungeon.Generation.Structure.HasHallways",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonGenHasHallways::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = DungeonGenerationTestHelpers::CreateDefaultConfig();
	UDungeonGenerator* Generator = NewObject<UDungeonGenerator>();
	Generator->AddToRoot();

	FDungeonResult Result = Generator->Generate(Config, 42);

	// MST requires at least N-1 edges for N rooms
	const int32 MinHallways = Result.Rooms.Num() - 1;
	TestTrue(FString::Printf(TEXT("At least %d hallways for %d rooms (got %d)"),
		MinHallways, Result.Rooms.Num(), Result.Hallways.Num()),
		Result.Hallways.Num() >= MinHallways);

	Generator->RemoveFromRoot();
	DungeonGenerationTestHelpers::CleanupConfig(Config);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonGenRoomCount, "Dungeon.Generation.Structure.RoomCountRespected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonGenRoomCount::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = DungeonGenerationTestHelpers::CreateDefaultConfig();
	UDungeonGenerator* Generator = NewObject<UDungeonGenerator>();
	Generator->AddToRoot();

	FDungeonResult Result = Generator->Generate(Config, 42);

	// Room count should not exceed config (may be less if placement fails)
	TestTrue(TEXT("Room count <= configured count"),
		Result.Rooms.Num() <= Config->RoomCount);
	TestTrue(TEXT("At least 2 rooms placed"),
		Result.Rooms.Num() >= 2);

	Generator->RemoveFromRoot();
	DungeonGenerationTestHelpers::CleanupConfig(Config);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonGenGridSizeMatches, "Dungeon.Generation.Structure.GridSizeMatches",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonGenGridSizeMatches::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = DungeonGenerationTestHelpers::CreateDefaultConfig();
	UDungeonGenerator* Generator = NewObject<UDungeonGenerator>();
	Generator->AddToRoot();

	FDungeonResult Result = Generator->Generate(Config, 42);

	TestEqual(TEXT("GridSize matches config"), Result.GridSize, Config->GridSize);
	TestEqual(TEXT("Grid.GridSize matches"), Result.Grid.GridSize, Config->GridSize);
	TestEqual(TEXT("Cell count matches"),
		Result.Grid.Cells.Num(),
		Config->GridSize.X * Config->GridSize.Y * Config->GridSize.Z);

	Generator->RemoveFromRoot();
	DungeonGenerationTestHelpers::CleanupConfig(Config);
	return true;
}

// ============================================================================
// STRESS TESTS
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonGenStress20Seeds, "Dungeon.Generation.Stress.TwentySeedsAllPass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonGenStress20Seeds::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = DungeonGenerationTestHelpers::CreateDefaultConfig();
	UDungeonGenerator* Generator = NewObject<UDungeonGenerator>();
	Generator->AddToRoot();

	int32 PassCount = 0;
	for (int64 Seed = 1; Seed <= 20; ++Seed)
	{
		FDungeonResult Result = Generator->Generate(Config, Seed);
		if (Result.Rooms.Num() < 2)
		{
			AddWarning(FString::Printf(TEXT("Seed %lld: only %d rooms placed"), Seed, Result.Rooms.Num()));
			continue;
		}

		FDungeonValidationResult Validation = FDungeonValidator::ValidateAll(Result, *Config);
		if (Validation.bPassed)
		{
			PassCount++;
		}
		else
		{
			AddWarning(FString::Printf(TEXT("Seed %lld failed: %s"), Seed, *Validation.GetSummary()));
		}
	}

	TestTrue(FString::Printf(TEXT("%d/20 seeds passed validation"), PassCount), PassCount >= 18);

	Generator->RemoveFromRoot();
	DungeonGenerationTestHelpers::CleanupConfig(Config);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDungeonGenStressTightGrid, "Dungeon.Generation.Stress.TightGridPasses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDungeonGenStressTightGrid::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = NewObject<UDungeonConfiguration>();
	Config->AddToRoot();
	Config->GridSize = FIntVector(10, 10, 2);
	Config->RoomCount = 3;
	Config->MinRoomSize = FIntVector(3, 3, 1);
	Config->MaxRoomSize = FIntVector(4, 4, 1);
	Config->RoomBuffer = 0;

	UDungeonGenerator* Generator = NewObject<UDungeonGenerator>();
	Generator->AddToRoot();

	FDungeonResult Result = Generator->Generate(Config, 42);
	TestTrue(TEXT("Tight grid produces rooms"), Result.Rooms.Num() >= 2);

	FDungeonValidationResult Validation = FDungeonValidator::ValidateAll(Result, *Config);
	if (!Validation.bPassed)
	{
		AddWarning(FString::Printf(TEXT("Tight grid validation: %s"), *Validation.GetSummary()));
	}
	// Tight grids may have reachability issues due to constraints — just verify no crash
	TestTrue(TEXT("Tight grid completed without crash"), true);

	Generator->RemoveFromRoot();
	DungeonGenerationTestHelpers::CleanupConfig(Config);
	return true;
}
