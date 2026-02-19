// Test_RoomSemantics.cpp — Unit + integration tests for entrance selection, graph metrics, and room type assignment
#include "Misc/AutomationTest.h"
#include "DungeonTypes.h"
#include "DungeonConfig.h"
#include "DungeonSeed.h"
#include "RoomSemantics.h"
#include "DungeonGenerator.h"
#include "DungeonValidator.h"

// ============================================================================
// Test Helpers
// ============================================================================

namespace RoomSemanticsTestHelpers
{
	UDungeonConfiguration* CreateConfig()
	{
		UDungeonConfiguration* Config = NewObject<UDungeonConfiguration>();
		Config->AddToRoot();
		Config->GridSize = FIntVector(30, 30, 1);
		Config->RoomCount = 5;
		Config->MinRoomSize = FIntVector(3, 3, 1);
		Config->MaxRoomSize = FIntVector(5, 5, 1);
		Config->RoomBuffer = 1;
		Config->EntrancePlacement = EDungeonEntrancePlacement::Any;
		// Clear default rules for controlled testing
		Config->RoomTypeRules.Empty();
		Config->bGuaranteeBossRoom = false;
		return Config;
	}

	void CleanupConfig(UDungeonConfiguration* Config)
	{
		if (Config)
		{
			Config->RemoveFromRoot();
		}
	}

	/** N rooms in a line: room 0 at (0,0,0), room 1 at (6,0,0), etc. FinalEdges: 0-1, 1-2, ... */
	FDungeonResult CreateLinearResult(int32 N, const FIntVector& GridSize = FIntVector(30, 30, 1))
	{
		FDungeonResult Result;
		Result.GridSize = GridSize;
		Result.Grid.Initialize(GridSize);

		for (int32 i = 0; i < N; ++i)
		{
			FDungeonRoom Room;
			Room.RoomIndex = static_cast<uint8>(i + 1);
			Room.Position = FIntVector(i * 6, 0, 0);
			Room.Size = FIntVector(4, 4, 1);
			Room.Center = Room.Position + FIntVector(2, 2, 0);
			Room.FloorLevel = 0;
			Result.Rooms.Add(Room);

			// Mark cells in grid
			for (int32 X = Room.Position.X; X < Room.Position.X + Room.Size.X && X < GridSize.X; ++X)
			{
				for (int32 Y = Room.Position.Y; Y < Room.Position.Y + Room.Size.Y && Y < GridSize.Y; ++Y)
				{
					FDungeonCell& Cell = Result.Grid.GetCell(X, Y, 0);
					Cell.CellType = EDungeonCellType::Room;
					Cell.RoomIndex = Room.RoomIndex;
				}
			}
		}

		// Linear edges
		for (int32 i = 0; i < N - 1; ++i)
		{
			Result.FinalEdges.Add(TPair<uint8, uint8>(
				static_cast<uint8>(i), static_cast<uint8>(i + 1)));
		}

		// Set entrance to room 0
		Result.EntranceRoomIndex = 0;
		Result.EntranceCell = Result.Rooms[0].Center;
		Result.Rooms[0].RoomType = EDungeonRoomType::Entrance;

		return Result;
	}

	/** Star topology: room 0 is hub connected to rooms 1,2,3,4 */
	FDungeonResult CreateBranchingResult()
	{
		FDungeonResult Result;
		Result.GridSize = FIntVector(30, 30, 1);
		Result.Grid.Initialize(Result.GridSize);

		// Hub room at center
		{
			FDungeonRoom Room;
			Room.RoomIndex = 1;
			Room.Position = FIntVector(13, 13, 0);
			Room.Size = FIntVector(4, 4, 1);
			Room.Center = FIntVector(15, 15, 0);
			Result.Rooms.Add(Room);
		}

		// Branch rooms at 4 corners
		FIntVector BranchPositions[] = {
			FIntVector(0, 0, 0),
			FIntVector(26, 0, 0),
			FIntVector(0, 26, 0),
			FIntVector(26, 26, 0),
		};

		for (int32 i = 0; i < 4; ++i)
		{
			FDungeonRoom Room;
			Room.RoomIndex = static_cast<uint8>(i + 2);
			Room.Position = BranchPositions[i];
			Room.Size = FIntVector(4, 4, 1);
			Room.Center = Room.Position + FIntVector(2, 2, 0);
			Result.Rooms.Add(Room);
		}

		// Mark cells in grid for all rooms
		for (const FDungeonRoom& Room : Result.Rooms)
		{
			for (int32 X = Room.Position.X; X < Room.Position.X + Room.Size.X; ++X)
			{
				for (int32 Y = Room.Position.Y; Y < Room.Position.Y + Room.Size.Y; ++Y)
				{
					if (Result.Grid.IsInBounds(X, Y, 0))
					{
						FDungeonCell& Cell = Result.Grid.GetCell(X, Y, 0);
						Cell.CellType = EDungeonCellType::Room;
						Cell.RoomIndex = Room.RoomIndex;
					}
				}
			}
		}

		// Star edges: hub (0) connected to each branch (1-4)
		for (int32 i = 1; i <= 4; ++i)
		{
			Result.FinalEdges.Add(TPair<uint8, uint8>(0, static_cast<uint8>(i)));
		}

		Result.EntranceRoomIndex = 0;
		Result.EntranceCell = Result.Rooms[0].Center;
		Result.Rooms[0].RoomType = EDungeonRoomType::Entrance;

		return Result;
	}

	/** 3 rooms across 2 floors. Room 0 on floor 0, room 1 spans floors 0-1, room 2 on floor 1. */
	FDungeonResult CreateMultiFloorResult()
	{
		FDungeonResult Result;
		Result.GridSize = FIntVector(20, 20, 3);
		Result.Grid.Initialize(Result.GridSize);

		// Room 0: single floor
		{
			FDungeonRoom Room;
			Room.RoomIndex = 1;
			Room.Position = FIntVector(0, 0, 0);
			Room.Size = FIntVector(4, 4, 1);
			Room.Center = FIntVector(2, 2, 0);
			Room.FloorLevel = 0;
			Result.Rooms.Add(Room);
		}

		// Room 1: spans 2 floors
		{
			FDungeonRoom Room;
			Room.RoomIndex = 2;
			Room.Position = FIntVector(8, 0, 0);
			Room.Size = FIntVector(4, 4, 2);
			Room.Center = FIntVector(10, 2, 0);
			Room.FloorLevel = 0;
			Result.Rooms.Add(Room);
		}

		// Room 2: upper floor only
		{
			FDungeonRoom Room;
			Room.RoomIndex = 3;
			Room.Position = FIntVector(16, 0, 2);
			Room.Size = FIntVector(4, 4, 1);
			Room.Center = FIntVector(18, 2, 2);
			Room.FloorLevel = 2;
			Result.Rooms.Add(Room);
		}

		// Edges: 0-1, 1-2
		Result.FinalEdges.Add(TPair<uint8, uint8>(0, 1));
		Result.FinalEdges.Add(TPair<uint8, uint8>(1, 2));

		Result.EntranceRoomIndex = 0;
		Result.EntranceCell = Result.Rooms[0].Center;
		Result.Rooms[0].RoomType = EDungeonRoomType::Entrance;

		return Result;
	}
}

// ============================================================================
// ENTRANCE SELECTION TESTS (4)
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomSemBoundaryEdgePick, "Dungeon.RoomSemantics.Entrance.BoundaryEdgePicksBoundaryRoom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoomSemBoundaryEdgePick::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = RoomSemanticsTestHelpers::CreateConfig();
	Config->EntrancePlacement = EDungeonEntrancePlacement::BoundaryEdge;

	// Create result with 1 boundary room and 1 interior room
	FDungeonResult Result;
	Result.GridSize = FIntVector(30, 30, 1);
	Result.Grid.Initialize(Result.GridSize);

	// Room 0: interior (doesn't touch boundary)
	{
		FDungeonRoom Room;
		Room.RoomIndex = 1;
		Room.Position = FIntVector(10, 10, 0);
		Room.Size = FIntVector(4, 4, 1);
		Room.Center = FIntVector(12, 12, 0);
		Result.Rooms.Add(Room);
	}

	// Room 1: touches boundary (X=0)
	{
		FDungeonRoom Room;
		Room.RoomIndex = 2;
		Room.Position = FIntVector(0, 5, 0);
		Room.Size = FIntVector(4, 4, 1);
		Room.Center = FIntVector(2, 7, 0);
		Result.Rooms.Add(Room);
	}

	FDungeonSeed Seed(42);
	int32 Chosen = FRoomSemantics::SelectEntranceRoom(Result, *Config, Seed);
	TestEqual(TEXT("Boundary room chosen"), Chosen, 1);

	RoomSemanticsTestHelpers::CleanupConfig(Config);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomSemBottomFloorPick, "Dungeon.RoomSemantics.Entrance.BottomFloorPicksFloor0",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoomSemBottomFloorPick::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = RoomSemanticsTestHelpers::CreateConfig();
	Config->EntrancePlacement = EDungeonEntrancePlacement::BottomFloor;
	Config->GridSize = FIntVector(20, 20, 3);

	FDungeonResult Result;
	Result.GridSize = FIntVector(20, 20, 3);
	Result.Grid.Initialize(Result.GridSize);

	// Room 0: floor 2
	{
		FDungeonRoom Room;
		Room.RoomIndex = 1;
		Room.Position = FIntVector(0, 0, 2);
		Room.Size = FIntVector(4, 4, 1);
		Room.Center = FIntVector(2, 2, 2);
		Result.Rooms.Add(Room);
	}

	// Room 1: floor 0
	{
		FDungeonRoom Room;
		Room.RoomIndex = 2;
		Room.Position = FIntVector(10, 0, 0);
		Room.Size = FIntVector(4, 4, 1);
		Room.Center = FIntVector(12, 2, 0);
		Result.Rooms.Add(Room);
	}

	FDungeonSeed Seed(42);
	int32 Chosen = FRoomSemantics::SelectEntranceRoom(Result, *Config, Seed);
	TestEqual(TEXT("Bottom floor room chosen"), Chosen, 1);

	RoomSemanticsTestHelpers::CleanupConfig(Config);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomSemTopFloorPick, "Dungeon.RoomSemantics.Entrance.TopFloorPicksHighestRoom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoomSemTopFloorPick::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = RoomSemanticsTestHelpers::CreateConfig();
	Config->EntrancePlacement = EDungeonEntrancePlacement::TopFloor;
	Config->GridSize = FIntVector(20, 20, 3);

	FDungeonResult Result;
	Result.GridSize = FIntVector(20, 20, 3);
	Result.Grid.Initialize(Result.GridSize);

	// Room 0: floor 0
	{
		FDungeonRoom Room;
		Room.RoomIndex = 1;
		Room.Position = FIntVector(0, 0, 0);
		Room.Size = FIntVector(4, 4, 1);
		Room.Center = FIntVector(2, 2, 0);
		Result.Rooms.Add(Room);
	}

	// Room 1: floor 2 (top of grid Z=3, room Z+SizeZ = 2+1 = 3 >= GridSize.Z)
	{
		FDungeonRoom Room;
		Room.RoomIndex = 2;
		Room.Position = FIntVector(10, 0, 2);
		Room.Size = FIntVector(4, 4, 1);
		Room.Center = FIntVector(12, 2, 2);
		Result.Rooms.Add(Room);
	}

	FDungeonSeed Seed(42);
	int32 Chosen = FRoomSemantics::SelectEntranceRoom(Result, *Config, Seed);
	TestEqual(TEXT("Top floor room chosen"), Chosen, 1);

	RoomSemanticsTestHelpers::CleanupConfig(Config);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomSemAnyPick, "Dungeon.RoomSemantics.Entrance.AnyPicksFromAll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoomSemAnyPick::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = RoomSemanticsTestHelpers::CreateConfig();
	Config->EntrancePlacement = EDungeonEntrancePlacement::Any;

	FDungeonResult Result = RoomSemanticsTestHelpers::CreateLinearResult(5);

	FDungeonSeed Seed(42);
	int32 Chosen = FRoomSemantics::SelectEntranceRoom(Result, *Config, Seed);
	TestTrue(TEXT("Valid index returned"), Chosen >= 0 && Chosen < Result.Rooms.Num());

	RoomSemanticsTestHelpers::CleanupConfig(Config);
	return true;
}

// ============================================================================
// GRAPH METRICS TESTS (5)
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomSemLinearDistances, "Dungeon.RoomSemantics.Graph.LinearDistancesCorrect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoomSemLinearDistances::RunTest(const FString& Parameters)
{
	FDungeonResult Result = RoomSemanticsTestHelpers::CreateLinearResult(5);
	TArray<FRoomSemanticContext> Contexts = FRoomSemantics::ComputeGraphMetrics(Result);

	TestEqual(TEXT("Room 0 distance"), Contexts[0].GraphDistance, 0);
	TestEqual(TEXT("Room 1 distance"), Contexts[1].GraphDistance, 1);
	TestEqual(TEXT("Room 2 distance"), Contexts[2].GraphDistance, 2);
	TestEqual(TEXT("Room 3 distance"), Contexts[3].GraphDistance, 3);
	TestEqual(TEXT("Room 4 distance"), Contexts[4].GraphDistance, 4);

	TestTrue(TEXT("Room 0 normalized ~0.0"), FMath::IsNearlyEqual(Contexts[0].NormalizedDistance, 0.0f, 0.01f));
	TestTrue(TEXT("Room 4 normalized ~1.0"), FMath::IsNearlyEqual(Contexts[4].NormalizedDistance, 1.0f, 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomSemLinearMainPath, "Dungeon.RoomSemantics.Graph.LinearMainPathAllOnPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoomSemLinearMainPath::RunTest(const FString& Parameters)
{
	FDungeonResult Result = RoomSemanticsTestHelpers::CreateLinearResult(5);
	TArray<FRoomSemanticContext> Contexts = FRoomSemantics::ComputeGraphMetrics(Result);

	// In a linear chain, all rooms should be on the main path
	for (int32 i = 0; i < 5; ++i)
	{
		TestTrue(FString::Printf(TEXT("Room %d on main path"), i), Contexts[i].bOnMainPath);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomSemLeafDetection, "Dungeon.RoomSemantics.Graph.StarLeafNodesDetected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoomSemLeafDetection::RunTest(const FString& Parameters)
{
	FDungeonResult Result = RoomSemanticsTestHelpers::CreateBranchingResult();
	TArray<FRoomSemanticContext> Contexts = FRoomSemantics::ComputeGraphMetrics(Result);

	// Hub (room 0) has degree 4 — not a leaf
	TestFalse(TEXT("Hub (room 0) is NOT a leaf"), Contexts[0].bIsLeafNode);

	// Branch rooms (1-4) each have degree 1 — all leaves
	for (int32 i = 1; i <= 4; ++i)
	{
		TestTrue(FString::Printf(TEXT("Branch room %d IS a leaf"), i), Contexts[i].bIsLeafNode);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomSemBranchMainPath, "Dungeon.RoomSemantics.Graph.StarMainPathToFarthest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoomSemBranchMainPath::RunTest(const FString& Parameters)
{
	FDungeonResult Result = RoomSemanticsTestHelpers::CreateBranchingResult();
	TArray<FRoomSemanticContext> Contexts = FRoomSemantics::ComputeGraphMetrics(Result);

	// All branch rooms are distance 1 from hub, so main path is hub → first branch (lowest index tie-break)
	TestTrue(TEXT("Hub on main path"), Contexts[0].bOnMainPath);

	// Exactly one branch room should be on main path (the farthest, or first tie-break)
	int32 MainPathBranches = 0;
	for (int32 i = 1; i <= 4; ++i)
	{
		if (Contexts[i].bOnMainPath)
		{
			MainPathBranches++;
		}
	}
	TestEqual(TEXT("Exactly 1 branch on main path"), MainPathBranches, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomSemMultiFloorDetect, "Dungeon.RoomSemantics.Graph.MultiFloorDetection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoomSemMultiFloorDetect::RunTest(const FString& Parameters)
{
	FDungeonResult Result = RoomSemanticsTestHelpers::CreateMultiFloorResult();
	TArray<FRoomSemanticContext> Contexts = FRoomSemantics::ComputeGraphMetrics(Result);

	TestFalse(TEXT("Room 0 single floor"), Contexts[0].bSpansMultipleFloors);
	TestTrue(TEXT("Room 1 multi-floor"), Contexts[1].bSpansMultipleFloors);
	TestFalse(TEXT("Room 2 single floor"), Contexts[2].bSpansMultipleFloors);

	return true;
}

// ============================================================================
// TYPE ASSIGNMENT TESTS (5)
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomSemBossAtFarthest, "Dungeon.RoomSemantics.TypeAssign.BossAtFarthest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoomSemBossAtFarthest::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = RoomSemanticsTestHelpers::CreateConfig();

	FDungeonRoomTypeRule BossRule;
	BossRule.RoomType = EDungeonRoomType::Boss;
	BossRule.Count = 1;
	BossRule.Priority = 100;
	BossRule.MinGraphDistanceFromEntrance = 0.8f;
	BossRule.bPreferMainPath = true;
	Config->RoomTypeRules.Add(BossRule);

	FDungeonResult Result = RoomSemanticsTestHelpers::CreateLinearResult(5);
	TArray<FRoomSemanticContext> Contexts = FRoomSemantics::ComputeGraphMetrics(Result);

	FDungeonSeed Seed(42);
	FRoomSemantics::AssignRoomTypes(Result, *Config, Contexts, Seed);

	// Room 4 is at NormalizedDistance=1.0, should be Boss
	TestEqual(TEXT("Room 4 is Boss"), Result.Rooms[4].RoomType, EDungeonRoomType::Boss);
	TestEqual(TEXT("Room 0 is still Entrance"), Result.Rooms[0].RoomType, EDungeonRoomType::Entrance);

	RoomSemanticsTestHelpers::CleanupConfig(Config);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomSemTreasurePrefersLeaf, "Dungeon.RoomSemantics.TypeAssign.TreasurePrefersLeaf",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoomSemTreasurePrefersLeaf::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = RoomSemanticsTestHelpers::CreateConfig();

	FDungeonRoomTypeRule TreasureRule;
	TreasureRule.RoomType = EDungeonRoomType::Treasure;
	TreasureRule.Count = 1;
	TreasureRule.Priority = 50;
	TreasureRule.bPreferLeafNodes = true;
	Config->RoomTypeRules.Add(TreasureRule);

	FDungeonResult Result = RoomSemanticsTestHelpers::CreateBranchingResult();
	TArray<FRoomSemanticContext> Contexts = FRoomSemantics::ComputeGraphMetrics(Result);

	FDungeonSeed Seed(42);
	FRoomSemantics::AssignRoomTypes(Result, *Config, Contexts, Seed);

	// Find which room is Treasure — should be a leaf (rooms 1-4)
	int32 TreasureIdx = -1;
	for (int32 i = 0; i < Result.Rooms.Num(); ++i)
	{
		if (Result.Rooms[i].RoomType == EDungeonRoomType::Treasure)
		{
			TreasureIdx = i;
			break;
		}
	}

	TestTrue(TEXT("Treasure room found"), TreasureIdx >= 1 && TreasureIdx <= 4);
	if (TreasureIdx >= 0)
	{
		TestTrue(TEXT("Treasure room is a leaf"), Contexts[TreasureIdx].bIsLeafNode);
	}

	RoomSemanticsTestHelpers::CleanupConfig(Config);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomSemMultipleRulesRespected, "Dungeon.RoomSemantics.TypeAssign.MultipleRulesRespected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoomSemMultipleRulesRespected::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = RoomSemanticsTestHelpers::CreateConfig();

	FDungeonRoomTypeRule BossRule;
	BossRule.RoomType = EDungeonRoomType::Boss;
	BossRule.Count = 1;
	BossRule.Priority = 100;
	BossRule.MinGraphDistanceFromEntrance = 0.7f;
	Config->RoomTypeRules.Add(BossRule);

	FDungeonRoomTypeRule TreasureRule;
	TreasureRule.RoomType = EDungeonRoomType::Treasure;
	TreasureRule.Count = 1;
	TreasureRule.Priority = 50;
	TreasureRule.bPreferLeafNodes = true;
	Config->RoomTypeRules.Add(TreasureRule);

	FDungeonRoomTypeRule RestRule;
	RestRule.RoomType = EDungeonRoomType::Rest;
	RestRule.Count = 1;
	RestRule.Priority = 25;
	Config->RoomTypeRules.Add(RestRule);

	FDungeonResult Result = RoomSemanticsTestHelpers::CreateLinearResult(5);
	TArray<FRoomSemanticContext> Contexts = FRoomSemantics::ComputeGraphMetrics(Result);

	FDungeonSeed Seed(42);
	FRoomSemantics::AssignRoomTypes(Result, *Config, Contexts, Seed);

	// Count types
	int32 EntranceCount = 0, BossCount = 0, TreasureCount = 0, RestCount = 0, GenericCount = 0;
	for (const FDungeonRoom& Room : Result.Rooms)
	{
		switch (Room.RoomType)
		{
		case EDungeonRoomType::Entrance: EntranceCount++; break;
		case EDungeonRoomType::Boss: BossCount++; break;
		case EDungeonRoomType::Treasure: TreasureCount++; break;
		case EDungeonRoomType::Rest: RestCount++; break;
		case EDungeonRoomType::Generic: GenericCount++; break;
		default: break;
		}
	}

	TestEqual(TEXT("1 Entrance"), EntranceCount, 1);
	TestEqual(TEXT("1 Boss"), BossCount, 1);
	TestEqual(TEXT("1 Treasure"), TreasureCount, 1);
	TestEqual(TEXT("1 Rest"), RestCount, 1);
	TestEqual(TEXT("1 Generic"), GenericCount, 1);

	RoomSemanticsTestHelpers::CleanupConfig(Config);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomSemBossGuaranteeFallback, "Dungeon.RoomSemantics.TypeAssign.BossGuaranteeFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoomSemBossGuaranteeFallback::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = RoomSemanticsTestHelpers::CreateConfig();
	Config->bGuaranteeBossRoom = true;
	// No Boss rule in RoomTypeRules — the fallback should auto-assign one

	FDungeonResult Result = RoomSemanticsTestHelpers::CreateLinearResult(5);
	TArray<FRoomSemanticContext> Contexts = FRoomSemantics::ComputeGraphMetrics(Result);

	FDungeonSeed Seed(42);
	FRoomSemantics::AssignRoomTypes(Result, *Config, Contexts, Seed);

	bool bHasBoss = false;
	for (const FDungeonRoom& Room : Result.Rooms)
	{
		if (Room.RoomType == EDungeonRoomType::Boss)
		{
			bHasBoss = true;
			break;
		}
	}

	TestTrue(TEXT("Boss room assigned via guarantee fallback"), bHasBoss);

	RoomSemanticsTestHelpers::CleanupConfig(Config);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomSemEntranceAlwaysAssigned, "Dungeon.RoomSemantics.TypeAssign.EntranceAlwaysAssigned",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoomSemEntranceAlwaysAssigned::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = RoomSemanticsTestHelpers::CreateConfig();

	FDungeonResult Result = RoomSemanticsTestHelpers::CreateLinearResult(3);
	TArray<FRoomSemanticContext> Contexts = FRoomSemantics::ComputeGraphMetrics(Result);

	FDungeonSeed Seed(42);
	FRoomSemantics::AssignRoomTypes(Result, *Config, Contexts, Seed);

	TestEqual(TEXT("Room 0 is Entrance"), Result.Rooms[0].RoomType, EDungeonRoomType::Entrance);

	RoomSemanticsTestHelpers::CleanupConfig(Config);
	return true;
}

// ============================================================================
// INTEGRATION TESTS (2)
// ============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomSemPipelineDeterminism, "Dungeon.RoomSemantics.Integration.PipelineDeterminism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoomSemPipelineDeterminism::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = NewObject<UDungeonConfiguration>();
	Config->AddToRoot();
	Config->GridSize = FIntVector(30, 30, 3);
	Config->RoomCount = 6;
	Config->MinRoomSize = FIntVector(3, 3, 1);
	Config->MaxRoomSize = FIntVector(7, 7, 1);
	Config->RoomBuffer = 1;

	UDungeonGenerator* Generator = NewObject<UDungeonGenerator>();
	Generator->AddToRoot();

	FDungeonResult ResultA = Generator->Generate(Config, 12345);
	FDungeonResult ResultB = Generator->Generate(Config, 12345);

	TestTrue(TEXT("Same room count"), ResultA.Rooms.Num() == ResultB.Rooms.Num());

	// Compare room types
	bool bTypesMatch = true;
	for (int32 i = 0; i < ResultA.Rooms.Num() && i < ResultB.Rooms.Num(); ++i)
	{
		if (ResultA.Rooms[i].RoomType != ResultB.Rooms[i].RoomType)
		{
			bTypesMatch = false;
			AddError(FString::Printf(TEXT("Room %d type mismatch: %d vs %d"),
				i, static_cast<int32>(ResultA.Rooms[i].RoomType),
				static_cast<int32>(ResultB.Rooms[i].RoomType)));
		}
	}
	TestTrue(TEXT("Room types are deterministic"), bTypesMatch);

	// Compare graph distances
	bool bDistancesMatch = true;
	for (int32 i = 0; i < ResultA.Rooms.Num() && i < ResultB.Rooms.Num(); ++i)
	{
		if (ResultA.Rooms[i].GraphDistanceFromEntrance != ResultB.Rooms[i].GraphDistanceFromEntrance)
		{
			bDistancesMatch = false;
		}
	}
	TestTrue(TEXT("Graph distances are deterministic"), bDistancesMatch);

	Generator->RemoveFromRoot();
	RoomSemanticsTestHelpers::CleanupConfig(Config);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoomSemValidatorPasses, "Dungeon.RoomSemantics.Integration.ValidatorPassesOnGenerated",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FRoomSemValidatorPasses::RunTest(const FString& Parameters)
{
	UDungeonConfiguration* Config = NewObject<UDungeonConfiguration>();
	Config->AddToRoot();
	Config->GridSize = FIntVector(30, 30, 3);
	Config->RoomCount = 6;
	Config->MinRoomSize = FIntVector(3, 3, 1);
	Config->MaxRoomSize = FIntVector(7, 7, 1);
	Config->RoomBuffer = 1;

	UDungeonGenerator* Generator = NewObject<UDungeonGenerator>();
	Generator->AddToRoot();

	FDungeonResult Result = Generator->Generate(Config, 42);
	TestTrue(TEXT("Generation produced rooms"), Result.Rooms.Num() >= 2);

	// Run full validation including room semantics
	FDungeonValidationResult Validation = FDungeonValidator::ValidateAll(Result, *Config);
	if (!Validation.bPassed)
	{
		AddError(FString::Printf(TEXT("Validation failed: %s"), *Validation.GetSummary()));
	}
	TestTrue(TEXT("Full validation passes with room semantics"), Validation.bPassed);

	// Verify semantics specifically
	TArray<FDungeonValidationIssue> SemanticsIssues;
	FDungeonValidator::ValidateRoomSemantics(Result, *Config, SemanticsIssues);
	TestEqual(TEXT("No semantics issues"), SemanticsIssues.Num(), 0);

	Generator->RemoveFromRoot();
	RoomSemanticsTestHelpers::CleanupConfig(Config);
	return true;
}
