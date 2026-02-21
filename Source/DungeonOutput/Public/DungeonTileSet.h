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

	// --- Hallway Floor Variants (optional — null falls back to HallwayFloor) ---

	/** When true, only adjacent Hallway cells count as connected for variant selection.
	 *  Doors, entrances, and staircases are treated as walls, producing end caps at transitions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Floors|Hallway Variants")
	bool bHallwayVariantsHallwayOnly = false;

	/** Hallway floor for straight sections (2 opposite neighbors). Runs along +Y. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Floors|Hallway Variants")
	TSoftObjectPtr<UStaticMesh> HallwayFloorStraight;

	/** Hallway floor for corners (2 adjacent neighbors). Connects +X and +Y. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Floors|Hallway Variants")
	TSoftObjectPtr<UStaticMesh> HallwayFloorCorner;

	/** Hallway floor for T-junctions (3 neighbors). Missing side is -Y. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Floors|Hallway Variants")
	TSoftObjectPtr<UStaticMesh> HallwayFloorTJunction;

	/** Rotation offset for T-junction meshes with non-standard native orientation.
	 *  Default convention: closed (missing) side faces -Y. E.g., if yours faces +X, set Yaw=-90. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Floors|Hallway Variants")
	FRotator HallwayFloorTJunctionRotationOffset;

	/** Hallway floor for crossroads (4 neighbors). 4-way symmetric. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Floors|Hallway Variants")
	TSoftObjectPtr<UStaticMesh> HallwayFloorCrossroad;

	/** Hallway floor for dead ends (1 neighbor). Open side faces +Y. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Floors|Hallway Variants")
	TSoftObjectPtr<UStaticMesh> HallwayFloorEndCap;

	// --- Stairs ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Stairs")
	TSoftObjectPtr<UStaticMesh> StaircaseMesh;

	/** Rotation offset for staircase meshes with non-standard native orientation.
	 *  Default convention: mesh slopes down along +Y. E.g., if yours slopes down +X, set Yaw=-90. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Stairs")
	FRotator StaircaseMeshRotationOffset;

	/** Returns true if at least one mesh slot is non-null. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "TileSet")
	bool IsValid() const;

	/** Collects all non-null mesh slots with their names. */
	void GetAllUniqueMeshes(TArray<TPair<FName, TSoftObjectPtr<UStaticMesh>>>& OutMeshes) const;
};
