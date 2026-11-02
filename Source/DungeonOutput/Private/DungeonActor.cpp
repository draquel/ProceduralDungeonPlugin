#include "DungeonActor.h"
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
#include "DrawDebugHelpers.h"
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
	// The records are in the tile map's space: the actor's frame when bMapInActorFrame.
	const FTransform Placed = bMapInActorFrame
		? FTransform(Transform.GetRotation(), Transform.GetLocation()) * GetActorTransform()
		: Transform;
	const FTransform Unscaled(Placed.GetRotation(), Placed.GetLocation());
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
	// The coverage report is in the tile map's own space (the actor's frame when bMapInActorFrame).
	return FDungeonCoverage::Analyse(CachedResult, CachedTileMap, GetMapOrigin(), bOpenEntranceOpening);
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
	if (!BuildDungeon(DungeonConfig, TileSet, Seed, bUseEntranceOverride, EntranceOverride, bOpenEntranceOpening,
		GetMapOrigin(), CachedResult, CachedTileMap))
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
	// Documentation/Archive/TILE_MODULE_SYSTEM_PLAN.md.
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
		// World-space instances from the actor's location, or relative to the actor in its own frame.
		HISMC->AddInstances(Batch.Instances, false, /*bWorldSpace=*/!bMapInActorFrame);

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
	bool bUseEntranceOverride, const FDungeonEntranceSpec& EntranceOverride, bool bOpenEntranceOpening,
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

	OutTileMap = FDungeonTileMapper::MapToTiles(OutResult, *TileSet, WorldOffset, bOpenEntranceOpening);
	return true;
}

bool ADungeonActor::SetTileInstanceHidden(EDungeonTileType Type, int32 InstanceIndex, bool bHide)
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
		if (bHide)
		{
			Xf.SetScale3D(FVector(KINDA_SMALL_NUMBER));
		}
		// The record is the transform the instance was added with: world space, or the actor's frame (bMapInActorFrame).
		bAny |= (*HISMC)->UpdateInstanceTransform(Ref.BatchIndex, Xf, /*bWorldSpace=*/!bMapInActorFrame, /*bMarkRenderStateDirty=*/true, /*bTeleport=*/true);
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
		const FVector InMap = CachedResult.GridToWorld(CachedResult.EntranceCell)
			+ GetMapOrigin()
			+ FVector(CachedResult.CellWorldSize * 0.5f, CachedResult.CellWorldSize * 0.5f, 0.0f);
		return bMapInActorFrame ? GetActorTransform().TransformPosition(InMap) : InMap;
	}
	return FVector::ZeroVector;
}

FVector ADungeonActor::GetMapOrigin() const
{
	return bMapInActorFrame ? FVector::ZeroVector : GetActorLocation();
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
