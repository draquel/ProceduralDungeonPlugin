#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DungeonTileSet.generated.h"

/**
 * Maps dungeon tile element types to static meshes.
 * Each non-null mesh slot gets one HISMC at runtime.
 * Assign this to ADungeonActor to control dungeon appearance.
 */
UCLASS(BlueprintType)
class DUNGEONOUTPUT_API UDungeonTileSet : public UDataAsset
{
	GENERATED_BODY()

public:
	UDungeonTileSet();

	// --- Floors ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Floors")
	TSoftObjectPtr<UStaticMesh> RoomFloor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Floors")
	TSoftObjectPtr<UStaticMesh> HallwayFloor;

	// --- Ceilings ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Ceilings")
	TSoftObjectPtr<UStaticMesh> RoomCeiling;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Ceilings")
	TSoftObjectPtr<UStaticMesh> HallwayCeiling;

	// --- Walls ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Walls")
	TSoftObjectPtr<UStaticMesh> WallSegment;

	// --- Doors ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Doors")
	TSoftObjectPtr<UStaticMesh> DoorFrame;

	// --- Entrance ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Entrance")
	TSoftObjectPtr<UStaticMesh> EntranceFrame;

	// --- Stairs ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Stairs")
	TSoftObjectPtr<UStaticMesh> StaircaseMesh;

	/** Returns true if at least one mesh slot is non-null. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "TileSet")
	bool IsValid() const;

	/** Collects all non-null mesh slots with their names. */
	void GetAllUniqueMeshes(TArray<TPair<FName, TSoftObjectPtr<UStaticMesh>>>& OutMeshes) const;
};
