#include "DungeonActor.h"
#include "DungeonOutput.h"
#include "DungeonGenerator.h"
#include "DungeonConfig.h"
#include "DungeonTileSet.h"
#include "DungeonTileMapper.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"

ADungeonActor::ADungeonActor()
{
	PrimaryActorTick.bCanEverTick = false;

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
