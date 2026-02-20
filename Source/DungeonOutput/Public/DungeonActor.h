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
	UFUNCTION(BlueprintCallable, Category = "Dungeon")
	void GenerateDungeon();

	/** Destroy all tile geometry. */
	UFUNCTION(BlueprintCallable, Category = "Dungeon")
	void ClearDungeon();

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

private:
	UPROPERTY()
	FDungeonResult CachedResult;

	UPROPERTY(Transient)
	TMap<uint8, TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> TileComponents;

	bool bHasDungeon = false;
};
