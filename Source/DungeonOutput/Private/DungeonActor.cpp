#include "DungeonActor.h"
#include "DungeonOutput.h"
#include "DungeonDoorActor.h"
#include "DungeonGenerator.h"
#include "DungeonConfig.h"
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

	// Interior post-process (E5): a box over the grid carries the exposure clamp / AO the tileset
	// asks for. The post-process component takes its bounds from its parent shape; both stay
	// inert until a build sizes the box and enables the volume.
	InteriorBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("InteriorBounds"));
	InteriorBounds->SetupAttachment(Root);
	// The post-process component answers "is the view inside?" through its parent shape's PHYSICS
	// body (UShapeComponent::GetSquaredDistanceToCollision), so the box must own one: query-only,
	// ignoring every channel, it never blocks, overlaps or traces anything but still exists.
	InteriorBounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteriorBounds->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteriorBounds->SetGenerateOverlapEvents(false);
	InteriorBounds->SetCanEverAffectNavigation(false);
	InteriorBounds->SetBoxExtent(FVector(50.0f));

	InteriorPostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("InteriorPostProcess"));
	InteriorPostProcess->SetupAttachment(InteriorBounds);
	InteriorPostProcess->bUnbound = false;
	InteriorPostProcess->bEnabled = false;

	// The plugin's own door and torch; game layers swap in subclasses or Blueprints.
	DoorActorClass = ADungeonDoorActor::StaticClass();
	WallLightActorClass = ADungeonTorchActor::StaticClass();
}

void ADungeonActor::BeginPlay()
{
	Super::BeginPlay();
	// A dungeon built before play (a level script, construction, or a build that ran on a
	// not-yet-begun world) gets its gameplay half now; builds after this spawn their own.
	if (bHasDungeon && bSpawnInteractables && Interactables.Num() == 0)
	{
		SpawnInteractables();
	}
}

void ADungeonActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DestroyInteractables();
	Super::EndPlay(EndPlayReason);
}

AActor* ADungeonActor::SpawnInteractable(TSubclassOf<AActor> Class, const FTransform& Transform, const FDungeonOpening* Opening, const FDungeonFixture* Fixture)
{
	UWorld* World = GetWorld();
	if (!World || !Class)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Owner = this;
	// Unscaled: the record's scale is carried by the actor's own leaf / mesh scale (see the
	// Setup functions), so the swing and the light never run under a non-uniform transform.
	const FTransform Unscaled(Transform.GetRotation(), Transform.GetLocation());
	AActor* Actor = World->SpawnActor<AActor>(Class, Unscaled, Params);
	if (!Actor)
	{
		return nullptr;
	}
	if (Actor->GetClass()->ImplementsInterface(UDungeonInteractable::StaticClass()))
	{
		if (Opening)
		{
			IDungeonInteractable::Execute_SetupFromOpening(Actor, *Opening, TileSet);
		}
		else if (Fixture)
		{
			IDungeonInteractable::Execute_SetupFromFixture(Actor, *Fixture, TileSet);
		}
	}
	Interactables.Add(Actor);
	OnInteractableSpawned.Broadcast(this, Actor, Opening != nullptr);
	return Actor;
}

int32 ADungeonActor::SpawnInteractables()
{
	DestroyInteractables();
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || !HasAuthority() || !bHasDungeon)
	{
		return 0;
	}
	int32 Doorways = 0, Doors = 0, Torches = 0;
	if (DoorActorClass)
	{
		for (const FDungeonOpening& Opening : CachedTileMap.Openings)
		{
			if (Opening.Kind != EDungeonOpeningKind::Doorway)
			{
				continue;
			}
			++Doorways;
			if (SpawnInteractable(DoorActorClass, Opening.LeafHinge, &Opening, nullptr))
			{
				++Doors;
			}
		}
	}
	if (WallLightActorClass)
	{
		for (const FDungeonFixture& Fixture : CachedTileMap.Fixtures)
		{
			if (Fixture.Kind == EDungeonFixtureKind::WallLight && SpawnInteractable(WallLightActorClass, Fixture.Anchor, nullptr, &Fixture))
			{
				++Torches;
			}
		}
	}
	UE_LOG(LogDungeonOutput, Log, TEXT("ADungeonActor %s: %d door(s) in %d doorway(s), %d torch(es) on %d fixture(s)."),
		*GetName(), Doors, Doorways, Torches, CachedTileMap.Fixtures.Num());
	return Interactables.Num();
}

void ADungeonActor::DestroyInteractables()
{
	for (AActor* Actor : Interactables)
	{
		if (IsValid(Actor))
		{
			Actor->Destroy();
		}
	}
	Interactables.Reset();
}

TArray<AActor*> ADungeonActor::GetInteractables() const
{
	TArray<AActor*> Out;
	Out.Reserve(Interactables.Num());
	for (AActor* Actor : Interactables)
	{
		if (IsValid(Actor))
		{
			Out.Add(Actor);
		}
	}
	return Out;
}

void ADungeonActor::ApplyInteriorPostProcess(bool bEnable)
{
	if (!InteriorBounds || !InteriorPostProcess)
	{
		return;
	}
	const bool bOn = bEnable && bHasDungeon && TileSet && TileSet->InteriorLighting.bInteriorPostProcess
		&& CachedResult.CellWorldSize > 0.0f;
	InteriorPostProcess->bEnabled = bOn;
	if (!bOn)
	{
		return;
	}
	const FDungeonInteriorLighting& L = TileSet->InteriorLighting;
	const FVector GridExtent = FVector(CachedResult.GridSize) * CachedResult.CellWorldSize * 0.5f;
	InteriorBounds->SetRelativeLocation(GridExtent);
	InteriorBounds->SetBoxExtent(GridExtent);

	FPostProcessSettings& S = InteriorPostProcess->Settings;
	S.bOverride_AutoExposureMinBrightness = true;
	S.AutoExposureMinBrightness = L.MinExposureEV100;
	S.bOverride_AutoExposureMaxBrightness = true;
	S.AutoExposureMaxBrightness = FMath::Max(L.MaxExposureEV100, L.MinExposureEV100);
	S.bOverride_AutoExposureBias = true;
	S.AutoExposureBias = L.ExposureCompensation;
	S.bOverride_AutoExposureSpeedUp = true;
	S.AutoExposureSpeedUp = L.ExposureSpeedUp;
	S.bOverride_AutoExposureSpeedDown = true;
	S.AutoExposureSpeedDown = L.ExposureSpeedDown;
	S.bOverride_LumenAmbientOcclusionIntensity = true;
	S.LumenAmbientOcclusionIntensity = L.AmbientOcclusionIntensity;
	InteriorPostProcess->BlendRadius = L.BlendRadius;
	InteriorPostProcess->Priority = L.Priority;
	InteriorPostProcess->BlendWeight = 1.0f;
}

FDungeonCoverageReport ADungeonActor::GetCoverageReport() const
{
	if (!bHasDungeon)
	{
		return FDungeonCoverageReport();
	}
	return FDungeonCoverage::Analyse(CachedResult, CachedTileMap, GetActorLocation(), bOpenEntranceCeiling);
}

FString ADungeonActor::DescribeCoverage() const
{
	return bHasDungeon ? GetCoverageReport().Describe() : FString(TEXT("no dungeon generated"));
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

	// Generate dungeon data + map to tiles through the ONE shared build (see BuildDungeon).
	if (!BuildDungeon(DungeonConfig, TileSet, Seed, bUseEntranceOverride, EntranceOverride, bOpenEntranceCeiling,
		GetActorLocation(), CachedResult, CachedTileMap))
	{
		return;
	}
	const FDungeonTileMapResult& TileMap = CachedTileMap;

	// --- Resolve instances to render batches (mesh + material identity), by PIECE ---
	// Every instance carries a piece id (E4: a slot's own geometry, a weighted variant, or a
	// room-type override). One HISM is created per unique (mesh, material) across the whole map,
	// so identical geometry — from different tile types, pieces or module elements — shares one
	// instanced component. A single-mesh piece emits one instance; a module piece expands to one
	// instance per element at (ModuleElement.RelativeTransform * TileAnchor). See
	// Documentation/TILE_MODULE_SYSTEM_PLAN.md.
	struct FRenderBatch
	{
		UStaticMesh* Mesh = nullptr;
		UMaterialInterface* Material = nullptr; // null = mesh defaults
		TArray<FTransform> Instances;
	};

	auto BatchKey = [](const UStaticMesh* Mesh, const UMaterialInterface* Material) -> FName
	{
		const FString MeshPath = Mesh ? Mesh->GetPathName() : FString();
		const FString MatPath = Material ? Material->GetPathName() : FString();
		return FName(*(MeshPath + TEXT("|") + MatPath));
	};

	TMap<FName, FRenderBatch> Batches;
	InstanceLookup.Empty();
	TMap<int32, UDungeonTileModule*> ModuleByPiece;
	TMap<int32, UStaticMesh*> MeshByPiece;
	TMap<FSoftObjectPath, UStaticMesh*> ElementMeshes;
	const UEnum* TypeEnum = StaticEnum<EDungeonTileType>();

	for (int32 TypeIdx = 0; TypeIdx < FDungeonTileMapResult::TypeCount; ++TypeIdx)
	{
		const TArray<FTransform>& Transforms = TileMap.Transforms[TypeIdx];
		const TArray<int32>& Ids = TileMap.PieceIds[TypeIdx];
		const FString TypeName = TypeEnum ? TypeEnum->GetNameStringByValue(TypeIdx) : FString::FromInt(TypeIdx);
		for (int32 i = 0; i < Transforms.Num(); ++i)
		{
			const int32 PieceId = Ids.IsValidIndex(i) ? Ids[i] : INDEX_NONE;
			if (!TileMap.Pieces.IsValidIndex(PieceId))
			{
				UE_LOG(LogDungeonOutput, Warning, TEXT("Tile type %s instance %d has no piece — skipped"), *TypeName, i);
				continue;
			}
			const FDungeonTilePiece& Piece = TileMap.Pieces[PieceId];
			if (!Piece.Module.IsNull())
			{
				UDungeonTileModule* Module = nullptr;
				if (UDungeonTileModule** Cached = ModuleByPiece.Find(PieceId))
				{
					Module = *Cached;
				}
				else
				{
					Module = Piece.Module.LoadSynchronous();
					ModuleByPiece.Add(PieceId, Module);
					if (!Module)
					{
						UE_LOG(LogDungeonOutput, Warning, TEXT("Module for %s: failed to load"), *TypeName);
					}
				}
				if (!Module)
				{
					continue;
				}
				for (const FDungeonModuleElement& Element : Module->Elements)
				{
					if (Element.Mesh.IsNull())
					{
						continue;
					}
					UStaticMesh* ElementMesh = nullptr;
					if (UStaticMesh** Cached = ElementMeshes.Find(Element.Mesh.ToSoftObjectPath()))
					{
						ElementMesh = *Cached;
					}
					else
					{
						ElementMesh = Element.Mesh.LoadSynchronous();
						ElementMeshes.Add(Element.Mesh.ToSoftObjectPath(), ElementMesh);
						if (!ElementMesh)
						{
							UE_LOG(LogDungeonOutput, Warning, TEXT("Module for %s: failed to load element mesh"), *TypeName);
						}
					}
					if (!ElementMesh)
					{
						continue;
					}
					UMaterialInterface* ElementMat = Element.MaterialOverride.IsNull()
						? nullptr : Element.MaterialOverride.LoadSynchronous();
					const FName ElementKey = BatchKey(ElementMesh, ElementMat);
					FRenderBatch& Batch = Batches.FindOrAdd(ElementKey);
					Batch.Mesh = ElementMesh;
					Batch.Material = ElementMat;
					// child-local * parent-world = world; the anchor carries the uniform cell scale.
					Batch.Instances.Add(Element.RelativeTransform * Transforms[i]);
					InstanceLookup.FindOrAdd(InstanceLookupKey(TypeIdx, i)).Add({ ElementKey, Batch.Instances.Num() - 1, Batch.Instances.Last() });
				}
			}
			else
			{
				UStaticMesh* LoadedMesh = nullptr;
				if (UStaticMesh** Cached = MeshByPiece.Find(PieceId))
				{
					LoadedMesh = *Cached;
				}
				else
				{
					LoadedMesh = Piece.Mesh.LoadSynchronous();
					MeshByPiece.Add(PieceId, LoadedMesh);
					if (!LoadedMesh)
					{
						UE_LOG(LogDungeonOutput, Warning, TEXT("Failed to load mesh for tile type %s"), *TypeName);
					}
				}
				if (!LoadedMesh)
				{
					continue;
				}
				const FName MeshKey = BatchKey(LoadedMesh, nullptr);
				FRenderBatch& Batch = Batches.FindOrAdd(MeshKey);
				Batch.Mesh = LoadedMesh;
				Batch.Instances.Add(Transforms[i]);
				InstanceLookup.FindOrAdd(InstanceLookupKey(TypeIdx, i)).Add({ MeshKey, Batch.Instances.Num() - 1, Transforms[i] });
			}
		}
	}

	// --- Create one HISM per batch ---
	// Mobility must MATCH THE ROOT's: UE refuses to attach a Static component to a non-Static parent
	// and aborts the attach ("AttachTo: 'Root' is not static, cannot attach 'X'..."). An editor-
	// placed ADungeonActor has a Static root, but a POI-streamed one is spawned during play with a
	// Movable root — hard-coding Static silently orphaned every tile component.
	USceneComponent* Root = GetRootComponent();
	const EComponentMobility::Type TileMobility =
		Root ? Root->Mobility.GetValue() : EComponentMobility::Movable;

	for (TPair<FName, FRenderBatch>& Pair : Batches)
	{
		FRenderBatch& Batch = Pair.Value;
		if (!Batch.Mesh || Batch.Instances.Num() == 0)
		{
			continue;
		}

		UHierarchicalInstancedStaticMeshComponent* HISMC = NewObject<UHierarchicalInstancedStaticMeshComponent>(
			this, NAME_None, RF_Transient);
		HISMC->SetStaticMesh(Batch.Mesh);
		if (Batch.Material)
		{
			HISMC->SetMaterial(0, Batch.Material);
		}
		HISMC->SetMobility(TileMobility);
		HISMC->SetCastShadow(true);
		HISMC->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		HISMC->SetCollisionResponseToAllChannels(ECR_Block);
		HISMC->AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
		HISMC->RegisterComponent();
		HISMC->AddInstances(Batch.Instances, false, true);

		TileComponents.Add(Pair.Key, HISMC);

		UE_LOG(LogDungeonOutput, Verbose, TEXT("  batch %s: %d instances"), *Batch.Mesh->GetName(), Batch.Instances.Num());
	}

	bHasDungeon = true;
	ApplyInteriorPostProcess(true);

#if WITH_EDITOR
	UpdateTickState();
#endif

	UE_LOG(LogDungeonOutput, Log, TEXT("Dungeon visualization complete: %d total instances, %d HISMC components"),
		TileMap.GetTotalInstanceCount(), TileComponents.Num());

	// The gameplay half (game worlds, authority; a no-op elsewhere, and before BeginPlay the
	// actor picks it up there).
	if (bSpawnInteractables && HasActorBegunPlay())
	{
		SpawnInteractables();
	}
}

bool ADungeonActor::BuildDungeon(UDungeonConfiguration* Config, const UDungeonTileSet* TileSet, int64 InSeed,
	bool bUseEntranceOverride, const FDungeonEntranceSpec& EntranceOverride, bool bOpenEntranceCeiling,
	const FVector& WorldOffset, FDungeonResult& OutResult, FDungeonTileMapResult& OutTileMap)
{
	if (!Config || !TileSet)
	{
		UE_LOG(LogDungeonOutput, Error, TEXT("ADungeonActor::BuildDungeon — %s is null"),
			Config ? TEXT("TileSet") : TEXT("DungeonConfig"));
		return false;
	}

	UDungeonGenerator* Generator = NewObject<UDungeonGenerator>();
	OutResult = bUseEntranceOverride
		? Generator->GenerateWithEntrance(Config, InSeed, EntranceOverride)
		: Generator->Generate(Config, InSeed);

	UE_LOG(LogDungeonOutput, Log, TEXT("Generated dungeon: %d rooms, %d hallways, %d staircases in %.1fms"),
		OutResult.Rooms.Num(), OutResult.Hallways.Num(),
		OutResult.Staircases.Num(), OutResult.GenerationTimeMs);

	OutTileMap = FDungeonTileMapper::MapToTiles(OutResult, *TileSet, WorldOffset, bOpenEntranceCeiling);
	return true;
}

bool ADungeonActor::SetTileInstanceHidden(EDungeonTileType Type, int32 InstanceIndex, bool bHidden)
{
	const TArray<FTileInstanceRef>* Refs = InstanceLookup.Find(InstanceLookupKey(static_cast<int32>(Type), InstanceIndex));
	if (!Refs || Refs->Num() == 0)
	{
		return false;
	}
	bool bAny = false;
	for (const FTileInstanceRef& Ref : *Refs)
	{
		const TObjectPtr<UHierarchicalInstancedStaticMeshComponent>* HISMC = TileComponents.Find(Ref.BatchKey);
		if (!HISMC || !*HISMC)
		{
			continue;
		}
		// Zero scale keeps indices stable (RemoveInstance would shift every later instance).
		FTransform Xf = Ref.Original;
		if (bHidden)
		{
			Xf.SetScale3D(FVector(KINDA_SMALL_NUMBER));
		}
		bAny |= (*HISMC)->UpdateInstanceTransform(Ref.BatchIndex, Xf, /*bWorldSpace=*/false, /*bMarkRenderStateDirty=*/true, /*bTeleport=*/true);
	}
	return bAny;
}

void ADungeonActor::ClearDungeon()
{
	DestroyInteractables();
	for (auto& Pair : TileComponents)
	{
		if (UHierarchicalInstancedStaticMeshComponent* HISMC = Pair.Value)
		{
			HISMC->ClearInstances();
			HISMC->DestroyComponent();
		}
	}
	TileComponents.Empty();
	InstanceLookup.Empty();
	bHasDungeon = false;
	ApplyInteriorPostProcess(false);

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

FString ADungeonActor::DescribeGridRegion(FIntVector MinCell, FIntVector MaxCell) const
{
	if (!bHasDungeon)
	{
		return FString();
	}

	static const TCHAR* TypeNames[] = {
		TEXT("Empty"), TEXT("Room"), TEXT("RoomWall"), TEXT("Hallway"),
		TEXT("Staircase"), TEXT("StaircaseHead"), TEXT("Door"), TEXT("Entrance"),
	};

	const FDungeonGrid& Grid = CachedResult.Grid;
	FString Out;
	for (int32 Z = MinCell.Z; Z <= MaxCell.Z; ++Z)
	{
		for (int32 Y = MinCell.Y; Y <= MaxCell.Y; ++Y)
		{
			for (int32 X = MinCell.X; X <= MaxCell.X; ++X)
			{
				if (!Grid.IsInBounds(X, Y, Z))
				{
					Out += FString::Printf(TEXT("(%d,%d,%d) OOB\n"), X, Y, Z);
					continue;
				}

				const FDungeonCell& Cell = Grid.GetCell(X, Y, Z);
				const int32 TypeIndex = static_cast<int32>(Cell.CellType);
				const TCHAR* TypeName = (TypeIndex >= 0 && TypeIndex < UE_ARRAY_COUNT(TypeNames))
					? TypeNames[TypeIndex] : TEXT("?");
				Out += FString::Printf(TEXT("(%d,%d,%d) %s room=%d hall=%d floor=%d stair=%d\n"),
					X, Y, Z, TypeName, Cell.RoomIndex, Cell.HallwayIndex, Cell.FloorIndex, Cell.StaircaseDirection);
			}
		}
	}
	return Out;
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
