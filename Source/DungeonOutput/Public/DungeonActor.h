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

/**
 * Blueprint-exposed actor that generates and displays a dungeon.
 * Place in a level, assign a DungeonConfiguration and TileSet, then call GenerateDungeon.
 * Uses one HISMC per tile type for efficient instanced rendering.
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

	bool bHasDungeon = false;

#if WITH_EDITOR
	void DrawDebugVisualization();
	void UpdateTickState();
	FColor GetRoomTypeColor(EDungeonRoomType Type) const;
	FString GetRoomTypeName(EDungeonRoomType Type) const;
#endif
};
