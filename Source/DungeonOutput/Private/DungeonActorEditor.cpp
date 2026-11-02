// Copyright Daniel Raquel. All Rights Reserved.

// ADungeonActor's editor-only half: property changes, the debug drawing's tick and the debug drawing (a file of the class, code review CR4).

#include "DungeonActor.h"
#include "DrawDebugHelpers.h"
#include "DungeonOutputModule.h"
#include "DungeonDoorActor.h"
#include "DungeonGenerator.h"
#include "DungeonConfiguration.h"
#include "DungeonInteractable.h"
#include "DungeonTileSet.h"
#include "DungeonTileMapper.h"
#include "DungeonTileModule.h"
#include "DungeonTorchActor.h"
#include "Engine/World.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PostProcessComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"

#if WITH_EDITOR
void ADungeonActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(ADungeonActor, bShowDebugVisualization))
	{
		UpdateTickState();
	}

	if (bAutoRegenerate && DungeonConfig && TileSet && TileSet->IsValid())
	{
		if (PropertyName == GET_MEMBER_NAME_CHECKED(ADungeonActor, DungeonConfig)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(ADungeonActor, TileSet)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(ADungeonActor, Seed))
		{
			GenerateDungeon();
		}
	}
}

void ADungeonActor::UpdateTickState()
{
	const bool bShouldTick = bShowDebugVisualization && bHasDungeon;
	SetActorTickEnabled(bShouldTick);
}

void ADungeonActor::DrawDebugVisualization()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector ActorLoc = GetActorLocation();
	const float CellSize = CachedResult.CellWorldSize;
	const float HalfCell = CellSize * 0.5f;

	// Helper: convert grid coord to world center
	auto GridToWorldCenter = [&](const FIntVector& GridCoord) -> FVector
	{
		return CachedResult.GridToWorld(GridCoord) + ActorLoc + FVector(HalfCell, HalfCell, HalfCell);
	};

	// --- Grid Bounds ---
	if (bShowGridBounds)
	{
		const FVector GridMin = ActorLoc;
		const FVector GridMax = ActorLoc + FVector(
			CachedResult.GridSize.X * CellSize,
			CachedResult.GridSize.Y * CellSize,
			CachedResult.GridSize.Z * CellSize);
		const FVector GridCenter = (GridMin + GridMax) * 0.5f;
		const FVector GridExtent = (GridMax - GridMin) * 0.5f;

		DrawDebugBox(World, GridCenter, GridExtent, FColor(128, 128, 128), false, 0.0f, 0, 1.0f);
	}

	// Coverage problems (E5): red boxes where a needed boundary has no piece or two; wall-light
	// fixtures as yellow points so the light rule can be eyeballed against the geometry.
	if (bShowCoverageProblems)
	{
		const FDungeonCoverageReport Report = GetCoverageReport();
		for (const FVector& P : Report.ProblemLocations)
		{
			DrawDebugBox(World, P, FVector(CellSize * 0.2f), FColor::Red, false, 0.0f, 0, DebugLineThickness);
		}
		for (const FDungeonFixture& F : CachedTileMap.Fixtures)
		{
			DrawDebugPoint(World, F.Anchor.GetLocation(), 12.0f, FColor::Yellow, false, 0.0f, 0);
		}
	}

	// --- Rooms ---
	if (bShowRooms || bShowRoomLabels)
	{
		for (const FDungeonRoom& Room : CachedResult.Rooms)
		{
			const FColor RoomColor = GetRoomTypeColor(Room.RoomType);
			const FVector RoomMin = CachedResult.GridToWorld(Room.Position) + ActorLoc;
			const FVector RoomMax = RoomMin + FVector(
				Room.Size.X * CellSize,
				Room.Size.Y * CellSize,
				Room.Size.Z * CellSize);
			const FVector RoomCenter = (RoomMin + RoomMax) * 0.5f;
			const FVector RoomExtent = (RoomMax - RoomMin) * 0.5f;

			if (bShowRooms)
			{
				DrawDebugBox(World, RoomCenter, RoomExtent, RoomColor, false, 0.0f, 0, DebugLineThickness);
			}

			if (bShowRoomLabels)
			{
				const FString Label = FString::Printf(TEXT("R%d:%s F%d"),
					Room.RoomIndex, *GetRoomTypeName(Room.RoomType), Room.FloorLevel);
				DrawDebugString(World, RoomCenter, Label, nullptr, RoomColor, 0.0f, true, 1.2f);
			}
		}
	}

	// --- Hallways ---
	if (bShowHallways)
	{
		for (const FDungeonHallway& Hallway : CachedResult.Hallways)
		{
			const FColor HallColor = Hallway.bIsFromMST ? FColor(255, 140, 0) : FColor(135, 206, 250);

			for (int32 i = 1; i < Hallway.PathCells.Num(); ++i)
			{
				const FVector Start = GridToWorldCenter(Hallway.PathCells[i - 1]);
				const FVector End = GridToWorldCenter(Hallway.PathCells[i]);
				DrawDebugLine(World, Start, End, HallColor, false, 0.0f, 0, DebugLineThickness);
			}
		}
	}

	// --- Graph Edges ---
	if (bShowGraphEdges)
	{
		// Delaunay edges (dark gray, thin)
		for (const auto& Edge : CachedResult.DelaunayEdges)
		{
			if (Edge.Key < CachedResult.Rooms.Num() && Edge.Value < CachedResult.Rooms.Num())
			{
				const FVector Start = GridToWorldCenter(CachedResult.Rooms[Edge.Key].Center);
				const FVector End = GridToWorldCenter(CachedResult.Rooms[Edge.Value].Center);
				DrawDebugLine(World, Start, End, FColor(80, 80, 80), false, 0.0f, 0, 1.0f);
			}
		}

		// MST edges (green, normal)
		for (const auto& Edge : CachedResult.MSTEdges)
		{
			if (Edge.Key < CachedResult.Rooms.Num() && Edge.Value < CachedResult.Rooms.Num())
			{
				const FVector Start = GridToWorldCenter(CachedResult.Rooms[Edge.Key].Center);
				const FVector End = GridToWorldCenter(CachedResult.Rooms[Edge.Value].Center);
				DrawDebugLine(World, Start, End, FColor::Green, false, 0.0f, 0, DebugLineThickness);
			}
		}

		// Final re-added edges (yellow, thick)
		for (const auto& Edge : CachedResult.FinalEdges)
		{
			if (Edge.Key < CachedResult.Rooms.Num() && Edge.Value < CachedResult.Rooms.Num())
			{
				// Only draw edges that are NOT in MST (the re-added ones)
				bool bIsMST = false;
				for (const auto& MSTEdge : CachedResult.MSTEdges)
				{
					if ((MSTEdge.Key == Edge.Key && MSTEdge.Value == Edge.Value)
						|| (MSTEdge.Key == Edge.Value && MSTEdge.Value == Edge.Key))
					{
						bIsMST = true;
						break;
					}
				}

				if (!bIsMST)
				{
					const FVector Start = GridToWorldCenter(CachedResult.Rooms[Edge.Key].Center);
					const FVector End = GridToWorldCenter(CachedResult.Rooms[Edge.Value].Center);
					DrawDebugLine(World, Start, End, FColor::Yellow, false, 0.0f, 0, DebugLineThickness * 1.5f);
				}
			}
		}
	}

	// --- Entrance ---
	if (bShowEntrance && CachedResult.EntranceRoomIndex >= 0)
	{
		const FVector EntrancePos = GridToWorldCenter(CachedResult.EntranceCell);
		DrawDebugSphere(World, EntrancePos, CellSize * 0.8f, 12, FColor::Green, false, 0.0f, 0, DebugLineThickness);
		DrawDebugString(World, EntrancePos + FVector(0, 0, CellSize), TEXT("ENTRANCE"), nullptr, FColor::Green, 0.0f, true, 1.5f);
	}

	// --- Staircases ---
	if (bShowStaircases)
	{
		for (const FDungeonStaircase& Staircase : CachedResult.Staircases)
		{
			const FVector Bottom = GridToWorldCenter(Staircase.BottomCell);
			const FVector Top = GridToWorldCenter(Staircase.TopCell);
			DrawDebugDirectionalArrow(World, Bottom, Top, CellSize * 0.5f, FColor::Cyan, false, 0.0f, 0, DebugLineThickness);
		}
	}
}

FColor ADungeonActor::GetRoomTypeColor(EDungeonRoomType Type) const
{
	switch (Type)
	{
	case EDungeonRoomType::Generic:   return FColor(180, 180, 180);
	case EDungeonRoomType::Entrance:  return FColor(0, 255, 0);
	case EDungeonRoomType::Boss:      return FColor(255, 0, 0);
	case EDungeonRoomType::Treasure:  return FColor(255, 215, 0);
	case EDungeonRoomType::Spawn:     return FColor(0, 128, 255);
	case EDungeonRoomType::Rest:      return FColor(0, 200, 100);
	case EDungeonRoomType::Secret:    return FColor(160, 32, 240);
	case EDungeonRoomType::Corridor:  return FColor(128, 128, 128);
	case EDungeonRoomType::Stairwell: return FColor(255, 140, 0);
	case EDungeonRoomType::Custom:    return FColor(255, 255, 255);
	default:                          return FColor::White;
	}
}

FString ADungeonActor::GetRoomTypeName(EDungeonRoomType Type) const
{
	switch (Type)
	{
	case EDungeonRoomType::Generic:   return TEXT("Generic");
	case EDungeonRoomType::Entrance:  return TEXT("Entrance");
	case EDungeonRoomType::Boss:      return TEXT("Boss");
	case EDungeonRoomType::Treasure:  return TEXT("Treasure");
	case EDungeonRoomType::Spawn:     return TEXT("Spawn");
	case EDungeonRoomType::Rest:      return TEXT("Rest");
	case EDungeonRoomType::Secret:    return TEXT("Secret");
	case EDungeonRoomType::Corridor:  return TEXT("Corridor");
	case EDungeonRoomType::Stairwell: return TEXT("Stairwell");
	case EDungeonRoomType::Custom:    return TEXT("Custom");
	default:                          return TEXT("Unknown");
	}
}
#endif
