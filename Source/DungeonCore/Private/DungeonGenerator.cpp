#include "DungeonGenerator.h"
#include "DungeonConfig.h"
#include "DungeonSeed.h"
#include "RoomPlacement.h"
#include "DelaunayTetrahedralization.h"
#include "MinimumSpanningTree.h"
#include "HallwayPathfinder.h"
#include "RoomSemantics.h"
#include "DungeonValidator.h"

DEFINE_LOG_CATEGORY_STATIC(LogDungeonGenerator, Log, All);

TArray<FVector> UDungeonGenerator::GetCellWorldPositionsByType(const FDungeonResult& Result, EDungeonCellType CellType)
{
	TArray<FVector> Positions;
	const FDungeonGrid& Grid = Result.Grid;

	for (int32 Z = 0; Z < Grid.GridSize.Z; ++Z)
	{
		for (int32 Y = 0; Y < Grid.GridSize.Y; ++Y)
		{
			for (int32 X = 0; X < Grid.GridSize.X; ++X)
			{
				if (Grid.GetCell(X, Y, Z).CellType == CellType)
				{
					Positions.Add(Result.GridToWorld(FIntVector(X, Y, Z)));
				}
			}
		}
	}

	return Positions;
}

FDungeonResult UDungeonGenerator::Generate(UDungeonConfiguration* Config, int64 Seed)
{
	FDungeonResult Result;

	if (!Config)
	{
		UE_LOG(LogDungeonGenerator, Error, TEXT("Generate called with null Config"));
		return Result;
	}

	const double StartTime = FPlatformTime::Seconds();

	// Use current time if seed is 0
	if (Seed == 0)
	{
		Seed = static_cast<int64>(FPlatformTime::Cycles64());
	}

	Result.Seed = Seed;
	Result.GridSize = Config->GridSize;
	Result.CellWorldSize = Config->CellWorldSize;

	// =========================================================================
	// Step 1: Initialize Grid
	// =========================================================================
	Result.Grid.Initialize(Config->GridSize);

	// =========================================================================
	// Step 2: Seed RNG
	// =========================================================================
	FDungeonSeed MainSeed(Seed);

	// =========================================================================
	// Step 3: Place Rooms
	// =========================================================================
	if (!FRoomPlacement::PlaceRooms(Result.Grid, *Config, MainSeed, Result.Rooms))
	{
		UE_LOG(LogDungeonGenerator, Error,
			TEXT("Failed to place enough rooms (need >= 2, got %d)"), Result.Rooms.Num());
		return Result;
	}

	UE_LOG(LogDungeonGenerator, Warning, TEXT("Step 3: Placed %d rooms"), Result.Rooms.Num());
	for (int32 r = 0; r < Result.Rooms.Num(); ++r)
	{
		const FDungeonRoom& Rm = Result.Rooms[r];
		UE_LOG(LogDungeonGenerator, Warning, TEXT("  Room %d: Center=(%d,%d,%d) Size=(%d,%d,%d)"),
			r, Rm.Center.X, Rm.Center.Y, Rm.Center.Z, Rm.Size.X, Rm.Size.Y, Rm.Size.Z);
	}

	// =========================================================================
	// Step 4: Select Entrance Room
	// =========================================================================
	FDungeonSeed EntranceSeed = MainSeed.Fork(3);
	Result.EntranceRoomIndex = FRoomSemantics::SelectEntranceRoom(Result, *Config, EntranceSeed);
	if (Result.EntranceRoomIndex >= 0)
	{
		Result.Rooms[Result.EntranceRoomIndex].RoomType = EDungeonRoomType::Entrance;
		Result.EntranceCell = Result.Rooms[Result.EntranceRoomIndex].Center;
	}

	UE_LOG(LogDungeonGenerator, Log, TEXT("Step 4: Selected entrance room %d (placement=%d)"),
		Result.EntranceRoomIndex, static_cast<int32>(Config->EntrancePlacement));

	// =========================================================================
	// Step 5: Delaunay Tetrahedralization (3D)
	// =========================================================================
	TArray<FVector> RoomCenters3D;
	RoomCenters3D.Reserve(Result.Rooms.Num());
	for (const FDungeonRoom& Room : Result.Rooms)
	{
		RoomCenters3D.Add(FVector(Room.Center));
	}

	// Detect coplanar rooms (all on the same Z floor) and add jitter
	// to prevent degenerate tetrahedralization
	bool bAllCoplanar = true;
	if (RoomCenters3D.Num() > 1)
	{
		const float FirstZ = RoomCenters3D[0].Z;
		for (int32 i = 1; i < RoomCenters3D.Num(); ++i)
		{
			if (!FMath::IsNearlyEqual(RoomCenters3D[i].Z, FirstZ, 0.01f))
			{
				bAllCoplanar = false;
				break;
			}
		}
	}

	if (bAllCoplanar && RoomCenters3D.Num() >= 4)
	{
		FDungeonSeed JitterSeed = MainSeed.Fork(99);
		for (FVector& Center : RoomCenters3D)
		{
			Center.Z += JitterSeed.FRand() * 0.01f;
		}
	}

	TArray<TPair<int32, int32>> DelaunayEdgesInt;
	FDelaunayTetrahedralization::Tetrahedralize(RoomCenters3D, DelaunayEdgesInt);

	// Convert int32 edges to uint8 for storage
	Result.DelaunayEdges.Reserve(DelaunayEdgesInt.Num());
	for (const auto& Edge : DelaunayEdgesInt)
	{
		Result.DelaunayEdges.Add(TPair<uint8, uint8>(
			static_cast<uint8>(Edge.Key),
			static_cast<uint8>(Edge.Value)));
	}

	UE_LOG(LogDungeonGenerator, Warning, TEXT("Step 5: Delaunay produced %d edges (coplanar=%d)"),
		Result.DelaunayEdges.Num(), bAllCoplanar ? 1 : 0);
	for (const auto& Edge : Result.DelaunayEdges)
	{
		UE_LOG(LogDungeonGenerator, Warning, TEXT("  Edge: %d <-> %d"), Edge.Key, Edge.Value);
	}

	// =========================================================================
	// Step 6: Minimum Spanning Tree (Prim's)
	// =========================================================================
	TArray<TPair<int32, int32>> MSTEdgesInt;
	FMinimumSpanningTree::Compute(RoomCenters3D, DelaunayEdgesInt,
		Result.EntranceRoomIndex, MSTEdgesInt);

	Result.MSTEdges.Reserve(MSTEdgesInt.Num());
	for (const auto& Edge : MSTEdgesInt)
	{
		Result.MSTEdges.Add(TPair<uint8, uint8>(
			static_cast<uint8>(Edge.Key),
			static_cast<uint8>(Edge.Value)));
	}

	UE_LOG(LogDungeonGenerator, Warning, TEXT("Step 6: MST has %d edges"), Result.MSTEdges.Num());

	// =========================================================================
	// Step 7: Edge Re-addition (add some Delaunay edges back for loops)
	// =========================================================================
	FDungeonSeed EdgeSeed = MainSeed.Fork(2);
	Result.FinalEdges = Result.MSTEdges;

	for (const auto& Edge : Result.DelaunayEdges)
	{
		// Check if this edge is already in the MST
		bool bInMST = false;
		for (const auto& MSTEdge : Result.MSTEdges)
		{
			if ((MSTEdge.Key == Edge.Key && MSTEdge.Value == Edge.Value) ||
				(MSTEdge.Key == Edge.Value && MSTEdge.Value == Edge.Key))
			{
				bInMST = true;
				break;
			}
		}

		if (!bInMST && EdgeSeed.RandBool(Config->EdgeReadditionChance))
		{
			Result.FinalEdges.Add(Edge);
		}
	}

	UE_LOG(LogDungeonGenerator, Warning, TEXT("Step 7: Final graph has %d edges (%d MST + %d re-added)"),
		Result.FinalEdges.Num(), Result.MSTEdges.Num(),
		Result.FinalEdges.Num() - Result.MSTEdges.Num());

	// =========================================================================
	// Step 8: Graph Metrics + Room Type Assignment
	// =========================================================================
	TArray<FRoomSemanticContext> SemanticContexts = FRoomSemantics::ComputeGraphMetrics(Result);
	FDungeonSeed TypeSeed = MainSeed.Fork(4);
	FRoomSemantics::AssignRoomTypes(Result, *Config, SemanticContexts, TypeSeed);

	// =========================================================================
	// Step 9: A* Hallway Carving
	// =========================================================================
	uint8 HallwayIdx = 1;

	for (const auto& Edge : Result.FinalEdges)
	{
		const int32 RoomAIdx = Edge.Key;
		const int32 RoomBIdx = Edge.Value;

		if (RoomAIdx >= Result.Rooms.Num() || RoomBIdx >= Result.Rooms.Num())
		{
			continue;
		}

		const FDungeonRoom& RoomA = Result.Rooms[RoomAIdx];
		const FDungeonRoom& RoomB = Result.Rooms[RoomBIdx];

		// Check if this is an MST edge
		bool bIsMST = false;
		for (const auto& MSTEdge : Result.MSTEdges)
		{
			if ((MSTEdge.Key == Edge.Key && MSTEdge.Value == Edge.Value) ||
				(MSTEdge.Key == Edge.Value && MSTEdge.Value == Edge.Key))
			{
				bIsMST = true;
				break;
			}
		}

		UE_LOG(LogDungeonGenerator, Warning, TEXT("  Attempting hallway: room %d (%d,%d,%d) -> room %d (%d,%d,%d)"),
			RoomAIdx, RoomA.Center.X, RoomA.Center.Y, RoomA.Center.Z,
			RoomBIdx, RoomB.Center.X, RoomB.Center.Y, RoomB.Center.Z);

		TArray<FIntVector> PathCells;
		if (FHallwayPathfinder::FindPath(
				Result.Grid, RoomA.Center, RoomB.Center, *Config,
				RoomA.RoomIndex, RoomB.RoomIndex, PathCells))
		{
			TArray<FDungeonStaircase> HallwayStaircases;
			FHallwayPathfinder::CarveHallway(
				Result.Grid, PathCells, HallwayIdx,
				RoomA.RoomIndex, RoomB.RoomIndex, *Config, HallwayStaircases);

			UE_LOG(LogDungeonGenerator, Warning, TEXT("    SUCCESS: path=%d cells, staircases=%d"),
				PathCells.Num(), HallwayStaircases.Num());

			FDungeonHallway Hallway;
			Hallway.HallwayIndex = HallwayIdx;
			Hallway.RoomA = static_cast<uint8>(RoomAIdx);
			Hallway.RoomB = static_cast<uint8>(RoomBIdx);
			Hallway.PathCells = MoveTemp(PathCells);
			Hallway.bIsFromMST = bIsMST;
			Hallway.bHasStaircase = HallwayStaircases.Num() > 0;

			// Collect staircases into result
			for (FDungeonStaircase& Staircase : HallwayStaircases)
			{
				Result.Staircases.Add(MoveTemp(Staircase));
			}

			Result.Hallways.Add(MoveTemp(Hallway));

			// Update room connectivity
			Result.Rooms[RoomAIdx].ConnectedRoomIndices.AddUnique(static_cast<uint8>(RoomBIdx));
			Result.Rooms[RoomBIdx].ConnectedRoomIndices.AddUnique(static_cast<uint8>(RoomAIdx));

			HallwayIdx++;
		}
		else
		{
			UE_LOG(LogDungeonGenerator, Warning,
				TEXT("A* failed to find path between room %d and room %d"),
				RoomAIdx, RoomBIdx);
		}
	}

	UE_LOG(LogDungeonGenerator, Warning, TEXT("Step 9: Carved %d hallways, %d total staircases"),
		Result.Hallways.Num(), Result.Staircases.Num());

	// =========================================================================
	// Step 10: Place Entrances & Doors (doors handled by CarveHallway)
	// =========================================================================
	if (Result.EntranceRoomIndex >= 0)
	{
		const FDungeonRoom& EntranceRoom = Result.Rooms[Result.EntranceRoomIndex];

		// Mark entrance cell in the grid
		FDungeonCell& EntranceGridCell = Result.Grid.GetCell(Result.EntranceCell);
		if (EntranceGridCell.CellType == EDungeonCellType::Room ||
			EntranceGridCell.CellType == EDungeonCellType::Door)
		{
			EntranceGridCell.CellType = EDungeonCellType::Entrance;
			EntranceGridCell.Flags |= 0x01; // bIsEntrance flag
		}
	}

	// =========================================================================
	// Compute Metrics
	// =========================================================================
	Result.TotalRoomCells = 0;
	Result.TotalHallwayCells = 0;
	Result.TotalStaircaseCells = 0;

	for (const FDungeonCell& Cell : Result.Grid.Cells)
	{
		switch (Cell.CellType)
		{
		case EDungeonCellType::Room:
		case EDungeonCellType::Door:
		case EDungeonCellType::Entrance:
			Result.TotalRoomCells++;
			break;
		case EDungeonCellType::Hallway:
			Result.TotalHallwayCells++;
			break;
		case EDungeonCellType::Staircase:
		case EDungeonCellType::StaircaseHead:
			Result.TotalStaircaseCells++;
			break;
		default:
			break;
		}
	}

	// =========================================================================
	// Step 11: Validation (non-shipping builds only)
	// =========================================================================
#if !UE_BUILD_SHIPPING
	{
		FDungeonValidationResult Validation = FDungeonValidator::ValidateAll(Result, *Config);
		if (!Validation.bPassed)
		{
			UE_LOG(LogDungeonGenerator, Warning, TEXT("Validation: %s"), *Validation.GetSummary());
		}
	}
#endif

	const double EndTime = FPlatformTime::Seconds();
	Result.GenerationTimeMs = (EndTime - StartTime) * 1000.0;

	UE_LOG(LogDungeonGenerator, Log,
		TEXT("Generation complete: %d rooms, %d hallways, %d staircases, %d room cells, %d hallway cells, %d staircase cells in %.2fms (seed=%lld)"),
		Result.Rooms.Num(), Result.Hallways.Num(), Result.Staircases.Num(),
		Result.TotalRoomCells, Result.TotalHallwayCells, Result.TotalStaircaseCells,
		Result.GenerationTimeMs, Result.Seed);

	return Result;
}
