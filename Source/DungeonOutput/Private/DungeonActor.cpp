#include "DungeonActor.h"
#include "DungeonOutput.h"
#include "DungeonGenerator.h"
#include "DungeonConfig.h"
#include "DungeonTileSet.h"
#include "DungeonTileMapper.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"

#if WITH_EDITOR
#include "DrawDebugHelpers.h"
#include "Editor.h"
#include "LevelEditorViewport.h"
#endif

ADungeonActor::ADungeonActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void ADungeonActor::GenerateDungeon()
{
	if (!DungeonConfig)
	{
		UE_LOG(LogDungeonOutput, Error, TEXT("ADungeonActor::GenerateDungeon — DungeonConfig is null"));
		return;
	}

	if (!TileSet)
	{
		UE_LOG(LogDungeonOutput, Error, TEXT("ADungeonActor::GenerateDungeon — TileSet is null"));
		return;
	}

	if (!TileSet->IsValid())
	{
		UE_LOG(LogDungeonOutput, Error, TEXT("ADungeonActor::GenerateDungeon — TileSet has no valid meshes"));
		return;
	}

	// Clear previous generation
	if (bHasDungeon)
	{
		ClearDungeon();
	}

	// Generate dungeon data
	UDungeonGenerator* Generator = NewObject<UDungeonGenerator>();
	CachedResult = Generator->Generate(DungeonConfig, Seed);

	UE_LOG(LogDungeonOutput, Log, TEXT("Generated dungeon: %d rooms, %d hallways, %d staircases in %.1fms"),
		CachedResult.Rooms.Num(), CachedResult.Hallways.Num(),
		CachedResult.Staircases.Num(), CachedResult.GenerationTimeMs);

	// Map grid to tile transforms
	FDungeonTileMapResult TileMap = FDungeonTileMapper::MapToTiles(
		CachedResult, *TileSet, GetActorLocation());

	// Resolve TileSet slots to mesh pointers (order must match EDungeonTileType)
	struct FTileSlot
	{
		EDungeonTileType Type;
		TSoftObjectPtr<UStaticMesh> Mesh;
		FName Name;
	};

	const FTileSlot Slots[] =
	{
		{ EDungeonTileType::RoomFloor,      TileSet->RoomFloor,      TEXT("RoomFloor") },
		{ EDungeonTileType::HallwayFloor,   TileSet->HallwayFloor,   TEXT("HallwayFloor") },
		{ EDungeonTileType::RoomCeiling,    TileSet->RoomCeiling,    TEXT("RoomCeiling") },
		{ EDungeonTileType::HallwayCeiling, TileSet->HallwayCeiling, TEXT("HallwayCeiling") },
		{ EDungeonTileType::WallSegment,    TileSet->WallSegment,    TEXT("WallSegment") },
		{ EDungeonTileType::DoorFrame,      TileSet->DoorFrame,      TEXT("DoorFrame") },
		{ EDungeonTileType::EntranceFrame,  TileSet->EntranceFrame,  TEXT("EntranceFrame") },
		{ EDungeonTileType::StaircaseMesh,  TileSet->StaircaseMesh,  TEXT("StaircaseMesh") },
	};

	for (const FTileSlot& Slot : Slots)
	{
		const int32 TypeIdx = static_cast<int32>(Slot.Type);
		const TArray<FTransform>& Transforms = TileMap.Transforms[TypeIdx];

		if (Transforms.Num() == 0 || Slot.Mesh.IsNull())
		{
			continue;
		}

		// Load mesh synchronously
		UStaticMesh* LoadedMesh = Slot.Mesh.LoadSynchronous();
		if (!LoadedMesh)
		{
			UE_LOG(LogDungeonOutput, Warning, TEXT("Failed to load mesh for tile type %s"), *Slot.Name.ToString());
			continue;
		}

		// Create HISMC
		UHierarchicalInstancedStaticMeshComponent* HISMC = NewObject<UHierarchicalInstancedStaticMeshComponent>(
			this, Slot.Name, RF_Transient);
		HISMC->SetStaticMesh(LoadedMesh);
		HISMC->SetMobility(EComponentMobility::Static);
		HISMC->SetCastShadow(true);
		HISMC->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		HISMC->SetCollisionResponseToAllChannels(ECR_Block);
		HISMC->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		HISMC->RegisterComponent();

		// Add all instances (world-space transforms)
		HISMC->AddInstances(Transforms, false, true);

		TileComponents.Add(static_cast<uint8>(Slot.Type), HISMC);

		UE_LOG(LogDungeonOutput, Verbose, TEXT("  %s: %d instances"), *Slot.Name.ToString(), Transforms.Num());
	}

	bHasDungeon = true;

#if WITH_EDITOR
	UpdateTickState();
#endif

	UE_LOG(LogDungeonOutput, Log, TEXT("Dungeon visualization complete: %d total instances, %d HISMC components"),
		TileMap.GetTotalInstanceCount(), TileComponents.Num());
}

void ADungeonActor::ClearDungeon()
{
	for (auto& Pair : TileComponents)
	{
		if (UHierarchicalInstancedStaticMeshComponent* HISMC = Pair.Value)
		{
			HISMC->ClearInstances();
			HISMC->DestroyComponent();
		}
	}
	TileComponents.Empty();
	bHasDungeon = false;

#if WITH_EDITOR
	UpdateTickState();
#endif
}

void ADungeonActor::RandomizeSeed()
{
	Seed = FMath::RandRange(1, MAX_int32);

	if (DungeonConfig && TileSet && TileSet->IsValid())
	{
		GenerateDungeon();
	}
}

FVector ADungeonActor::GetEntranceWorldPosition() const
{
	if (CachedResult.EntranceRoomIndex >= 0)
	{
		return CachedResult.GridToWorld(CachedResult.EntranceCell)
			+ GetActorLocation()
			+ FVector(CachedResult.CellWorldSize * 0.5f, CachedResult.CellWorldSize * 0.5f, 0.0f);
	}
	return FVector::ZeroVector;
}

void ADungeonActor::GoToEntrance()
{
#if WITH_EDITOR
	if (!bHasDungeon)
	{
		UE_LOG(LogDungeonOutput, Warning, TEXT("GoToEntrance: No dungeon generated."));
		return;
	}

	const FVector Pos = GetEntranceWorldPosition();
	const float ViewDistance = CachedResult.CellWorldSize * 3.0f;

	// Position camera slightly above and behind the entrance, looking down at it
	const FVector CamPos = Pos + FVector(-ViewDistance, 0.0f, ViewDistance);
	const FRotator CamRot = (Pos - CamPos).Rotation();

	if (GEditor && GEditor->GetActiveViewport())
	{
		FEditorViewportClient* ViewportClient = static_cast<FEditorViewportClient*>(GEditor->GetActiveViewport()->GetClient());
		if (ViewportClient)
		{
			ViewportClient->SetViewLocation(CamPos);
			ViewportClient->SetViewRotation(CamRot);
			ViewportClient->Invalidate();
		}
	}

	UE_LOG(LogDungeonOutput, Log, TEXT("GoToEntrance: Camera moved to entrance at (%.0f, %.0f, %.0f)"), Pos.X, Pos.Y, Pos.Z);
#endif
}

int32 ADungeonActor::GetTotalInstanceCount() const
{
	int32 Total = 0;
	for (const auto& Pair : TileComponents)
	{
		if (Pair.Value)
		{
			Total += Pair.Value->GetInstanceCount();
		}
	}
	return Total;
}

void ADungeonActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

#if WITH_EDITOR
	if (bShowDebugVisualization && bHasDungeon)
	{
		DrawDebugVisualization();
	}
#endif
}

bool ADungeonActor::ShouldTickIfViewportsOnly() const
{
#if WITH_EDITORONLY_DATA
	return bShowDebugVisualization;
#else
	return false;
#endif
}

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
