#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DungeonTypes.h"
#include "DungeonTileMapper.h"
#include "DungeonActor.generated.h"

class UDungeonConfiguration;
class UDungeonTileSet;
class UHierarchicalInstancedStaticMeshComponent;

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

	/** Random seed. 0 = use current time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dungeon")
	int64 Seed = 0;

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

	/** Get the world-space position of the entrance cell. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Dungeon")
	FVector GetEntranceWorldPosition() const;

	/** Returns true if a dungeon has been generated. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Dungeon")
	bool HasDungeon() const { return bHasDungeon; }

	/** Get total number of mesh instances across all tile types. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Dungeon")
	int32 GetTotalInstanceCount() const;

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

	UPROPERTY(Transient)
	TMap<uint8, TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> TileComponents;

	bool bHasDungeon = false;

#if WITH_EDITOR
	void DrawDebugVisualization();
	void UpdateTickState();
	FColor GetRoomTypeColor(EDungeonRoomType Type) const;
	FString GetRoomTypeName(EDungeonRoomType Type) const;
#endif
};
