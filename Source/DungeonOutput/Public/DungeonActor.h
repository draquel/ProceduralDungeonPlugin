#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DungeonTypes.h"
#include "DungeonTileMapper.h"
#include "DungeonCoverage.h"
#include "DungeonActor.generated.h"

class UDungeonConfiguration;
class UDungeonTileSet;
class UHierarchicalInstancedStaticMeshComponent;
class UBoxComponent;
class UPostProcessComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnDungeonInteractableSpawned, ADungeonActor*, Dungeon, AActor*, Interactable, bool, bIsDoor);

/**
 * Blueprint-exposed actor that generates and displays a dungeon.
 * Place in a level, assign a DungeonConfiguration and TileSet, then call GenerateDungeon.
 * Uses one HISMC per tile type for efficient instanced rendering.
 *
 * The tiles are the visual half. The gameplay half — door leaves in the Doorway openings and
 * torches on the wall-light fixtures the mapper emits — are ACTORS, because they move, hold
 * state, replicate and take interactions, none of which an instance can do. In a game world the
 * actor spawns them itself on authority after every build (bSpawnInteractables, DoorActorClass /
 * WallLightActorClass, see IDungeonInteractable); a game layer that keeps its own records (the
 * POI system) turns that off and hangs its own subclasses from GetOpenings() / GetFixtures().
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "Dungeon Actor"))
class DUNGEONOUTPUT_API ADungeonActor : public AActor
{
	GENERATED_BODY()

public:
	ADungeonActor();

	/** Dungeon generation parameters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon")
	TObjectPtr<UDungeonConfiguration> DungeonConfig;

	/** Mesh mapping for tile visualization. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon")
	TObjectPtr<UDungeonTileSet> TileSet;

	/**
	 * Random seed. A non-zero value is always used as-is. 0 = use the config's FixedSeed when its
	 * bUseFixedSeed is on, otherwise the current time (see UDungeonConfiguration::ResolveSeed).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon")
	int64 Seed = 0;

	/**
	 * Leave the entrance opening unbuilt so a stitched passage (e.g. a voxel shaft or side
	 * tunnel) can enter the entrance room: the lid, floor or wall of the opening cell per the
	 * dungeon's entrance approach. Leave off for standalone dungeons.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon")
	bool bOpenEntranceCeiling = false;

	/**
	 * Generate with EntranceOverride instead of the config's Entrance spec. A tile actor dressing
	 * a voxel-stamped dungeon MUST use the same spec the stamp was generated with, or it rebuilds
	 * a different layout over the carved voids (the spec changes room placement).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon")
	bool bUseEntranceOverride = false;

	/** The entrance approach to honour when bUseEntranceOverride is set (see FDungeonEntranceSpec). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon", meta = (EditCondition = "bUseEntranceOverride"))
	FDungeonEntranceSpec EntranceOverride;

	/** Generate the dungeon and create tile geometry. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Dungeon")
	void GenerateDungeon();

	/** Destroy all tile geometry. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Dungeon")
	void ClearDungeon();

	/** Set a random seed and regenerate. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Dungeon")
	void RandomizeSeed();

	/** Move the editor viewport camera to the dungeon entrance cell. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Dungeon")
	void GoToEntrance();

	/** Get the cached generation result. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Dungeon")
	const FDungeonResult& GetDungeonResult() const { return CachedResult; }

	/** The tile map of the last GenerateDungeon (instances, openings, fixtures); empty before. */
	const FDungeonTileMapResult& GetTileMap() const { return CachedTileMap; }

	/** Framed openings of the last build (doorways, ramp entries, entrance openings), world space. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Dungeon|Interactables")
	TArray<FDungeonOpening> GetOpenings() const { return CachedTileMap.Openings; }

	/** Wall-mounted fixtures of the last build (wall lights per the tileset's rules), world space. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Dungeon|Interactables")
	TArray<FDungeonFixture> GetFixtures() const { return CachedTileMap.Fixtures; }

	/**
	 * Spawn the gameplay half in a game world on authority: one DoorActorClass per Doorway opening
	 * (at its LeafHinge) and one WallLightActorClass per WallLight fixture (at its Anchor), each
	 * told its record through IDungeonInteractable, then OnInteractableSpawned. Runs automatically
	 * after GenerateDungeon when bSpawnInteractables is set; call it yourself after changing the
	 * classes. Previously spawned interactables are destroyed first.
	 * @return Number of actors spawned (0 outside a game world, without authority, or with no dungeon).
	 */
	UFUNCTION(BlueprintCallable, Category = "Dungeon|Interactables")
	int32 SpawnInteractables();

	/** Destroy every interactable this actor spawned (ClearDungeon and EndPlay do this). */
	UFUNCTION(BlueprintCallable, Category = "Dungeon|Interactables")
	void DestroyInteractables();

	/** The interactables this actor spawned and still owns (doors first, then torches). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Dungeon|Interactables")
	TArray<AActor*> GetInteractables() const;

	/** Hang doors and wall lights automatically after each build (game worlds, authority). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon|Interactables")
	bool bSpawnInteractables = true;

	/** Door leaf actor per Doorway opening (ADungeonDoorActor or a subclass; implements IDungeonInteractable). None = no doors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon|Interactables")
	TSubclassOf<AActor> DoorActorClass;

	/** Torch actor per WallLight fixture (ADungeonTorchActor or a subclass; implements IDungeonInteractable). None = no lights. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon|Interactables")
	TSubclassOf<AActor> WallLightActorClass;

	/** Fires on authority for each interactable right after its Setup call: set a remembered state here. */
	UPROPERTY(BlueprintAssignable, Category = "Dungeon|Interactables")
	FOnDungeonInteractableSpawned OnInteractableSpawned;

	/**
	 * THE dungeon build: generate (with or without an entrance override) and map to tiles. Every
	 * consumer that must agree with the tile actor's layout — the actor itself, and gameplay that
	 * hangs doors / lights from the mapper's openings and fixtures — goes through this one
	 * function with the same inputs, so no second caller can drift (the entrance-placement P3
	 * lesson: the spec changes room placement).
	 * @param Config        Generation parameters (required).
	 * @param TileSet       Tile geometry + wall profile + fixture rules (required).
	 * @param InSeed        Seed (0 = config fallback, see UDungeonConfiguration::ResolveSeed).
	 * @param bUseEntranceOverride Generate with EntranceOverride instead of the config's spec.
	 * @param EntranceOverride     The spec when overriding.
	 * @param bOpenEntranceCeiling Leave the entrance opening unbuilt (see MapToTiles).
	 * @param WorldOffset   World-space offset of the grid origin (the tile actor's location).
	 * @param OutResult     The generated dungeon.
	 * @param OutTileMap    The mapped tiles, openings and fixtures.
	 * @return False (with a log) when Config or TileSet is missing.
	 */
	static bool BuildDungeon(UDungeonConfiguration* Config, const UDungeonTileSet* TileSet, int64 InSeed,
		bool bUseEntranceOverride, const FDungeonEntranceSpec& EntranceOverride, bool bOpenEntranceCeiling,
		const FVector& WorldOffset, FDungeonResult& OutResult, FDungeonTileMapResult& OutTileMap);

	/** Get the world-space position of the entrance cell. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Dungeon")
	FVector GetEntranceWorldPosition() const;

	/** Returns true if a dungeon has been generated. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Dungeon")
	bool HasDungeon() const { return bHasDungeon; }

	/** Get total number of mesh instances across all tile types. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Dungeon")
	int32 GetTotalInstanceCount() const;

	/**
	 * Light-tightness of the last build (E5): every needed wall / floor / ceiling matched against
	 * the placed pieces (see FDungeonCoverageReport). Empty before GenerateDungeon.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dungeon|Coverage")
	FDungeonCoverageReport GetCoverageReport() const;

	/** GetCoverageReport().Describe(): the summary line plus one line per problem. */
	UFUNCTION(BlueprintCallable, Category = "Dungeon|Coverage")
	FString DescribeCoverage() const;

	/**
	 * Debug dump of the grid cells in an inclusive grid-coordinate box, one line per cell:
	 * "(x,y,z) Type room=R hall=H floor=F stair=S". The grid itself is C++ only (too large for
	 * Blueprint), so this is the way to inspect cell neighbourhoods from Blueprint or Python when
	 * diagnosing tile / voxel boundary decisions. Out-of-bounds coordinates are reported as OOB.
	 *
	 * @param MinCell Inclusive min grid coordinate.
	 * @param MaxCell Inclusive max grid coordinate.
	 * @return Multi-line description; empty when no dungeon has been generated.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dungeon|Debug")
	FString DescribeGridRegion(FIntVector MinCell, FIntVector MaxCell) const;

#if WITH_EDITORONLY_DATA
	/** Master toggle for debug visualization in the viewport. */
	UPROPERTY(EditAnywhere, Category = "Dungeon|Debug Visualization")
	bool bShowDebugVisualization = false;

	/** Show wireframe boxes for rooms (colored by type). */
	UPROPERTY(EditAnywhere, Category = "Dungeon|Debug Visualization", meta = (EditCondition = "bShowDebugVisualization"))
	bool bShowRooms = true;

	/** Show hallway path lines. */
	UPROPERTY(EditAnywhere, Category = "Dungeon|Debug Visualization", meta = (EditCondition = "bShowDebugVisualization"))
	bool bShowHallways = true;

	/** Show Delaunay, MST, and final graph edges. */
	UPROPERTY(EditAnywhere, Category = "Dungeon|Debug Visualization", meta = (EditCondition = "bShowDebugVisualization"))
	bool bShowGraphEdges = true;

	/** Show room labels at room centers. */
	UPROPERTY(EditAnywhere, Category = "Dungeon|Debug Visualization", meta = (EditCondition = "bShowDebugVisualization"))
	bool bShowRoomLabels = true;

	/** Show entrance marker. */
	UPROPERTY(EditAnywhere, Category = "Dungeon|Debug Visualization", meta = (EditCondition = "bShowDebugVisualization"))
	bool bShowEntrance = true;

	/** Show staircase directional arrows. */
	UPROPERTY(EditAnywhere, Category = "Dungeon|Debug Visualization", meta = (EditCondition = "bShowDebugVisualization"))
	bool bShowStaircases = true;

	/** Show grid bounds wireframe. */
	UPROPERTY(EditAnywhere, Category = "Dungeon|Debug Visualization", meta = (EditCondition = "bShowDebugVisualization"))
	bool bShowGridBounds = true;

	/** Mark uncovered / doubled boundaries from the coverage report (red boxes) and wall-light fixtures (yellow). */
	UPROPERTY(EditAnywhere, Category = "Dungeon|Debug Visualization", meta = (EditCondition = "bShowDebugVisualization"))
	bool bShowCoverageProblems = true;

	/** Thickness of debug lines. */
	UPROPERTY(EditAnywhere, Category = "Dungeon|Debug Visualization", meta = (EditCondition = "bShowDebugVisualization", ClampMin = "0.5", ClampMax = "10.0"))
	float DebugLineThickness = 2.0f;

	/** Automatically regenerate when config, tileset, or seed changes. */
	UPROPERTY(EditAnywhere, Category = "Dungeon|Editor")
	bool bAutoRegenerate = true;
#endif

	// AActor interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	UPROPERTY()
	FDungeonResult CachedResult;

	/** Openings and fixtures of the last build (not a UPROPERTY: rebuilt with the tiles). */
	FDungeonTileMapResult CachedTileMap;

	/** Live tile HISMs, keyed by render batch (mesh+material identity), one per unique batch. */
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> TileComponents;

	/** The grid box the interior post-process is bounded to (E5). */
	UPROPERTY(VisibleAnywhere, Category = "Dungeon|Interior")
	TObjectPtr<UBoxComponent> InteriorBounds;

	/** Interior exposure clamp / AO from the tileset's InteriorLighting, applied inside InteriorBounds. */
	UPROPERTY(VisibleAnywhere, Category = "Dungeon|Interior")
	TObjectPtr<UPostProcessComponent> InteriorPostProcess;

	/** Size the box to the grid and push the tileset's interior settings (after a build); disable when cleared. */
	void ApplyInteriorPostProcess(bool bEnable);

	/** Doors then torches spawned by SpawnInteractables (authority, game worlds only). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> Interactables;

	/** Spawn one actor of Class at Transform, run its IDungeonInteractable setup, broadcast. */
	AActor* SpawnInteractable(TSubclassOf<AActor> Class, const FTransform& Transform, const FDungeonOpening* Opening, const FDungeonFixture* Fixture);

	bool bHasDungeon = false;

#if WITH_EDITOR
	void DrawDebugVisualization();
	void UpdateTickState();
	FColor GetRoomTypeColor(EDungeonRoomType Type) const;
	FString GetRoomTypeName(EDungeonRoomType Type) const;
#endif
};
