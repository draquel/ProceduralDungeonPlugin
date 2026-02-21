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

	/** Rotation offset for straight floor meshes with non-standard native orientation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Floors|Hallway Variants")
	FRotator HallwayFloorStraightRotationOffset;

	/** Scale multiplier applied on top of auto-fit scaling. (1,1,1) = exact cell fit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Floors|Hallway Variants")
	FVector HallwayFloorStraightScaleMultiplier;

	/** Hallway floor for corners (2 adjacent neighbors). Connects +X and +Y. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Floors|Hallway Variants")
	TSoftObjectPtr<UStaticMesh> HallwayFloorCorner;

	/** Rotation offset for corner floor meshes with non-standard native orientation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Floors|Hallway Variants")
	FRotator HallwayFloorCornerRotationOffset;

	/** Scale multiplier applied on top of auto-fit scaling. (1,1,1) = exact cell fit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Floors|Hallway Variants")
	FVector HallwayFloorCornerScaleMultiplier;

	/** Hallway floor for T-junctions (3 neighbors). Missing side is -Y. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Floors|Hallway Variants")
	TSoftObjectPtr<UStaticMesh> HallwayFloorTJunction;

	/** Rotation offset for T-junction meshes with non-standard native orientation.
	 *  Default convention: closed (missing) side faces -Y. E.g., if yours faces +X, set Yaw=-90. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Floors|Hallway Variants")
	FRotator HallwayFloorTJunctionRotationOffset;

	/** Scale multiplier applied on top of auto-fit scaling. (1,1,1) = exact cell fit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Floors|Hallway Variants")
	FVector HallwayFloorTJunctionScaleMultiplier;

	/** Hallway floor for crossroads (4 neighbors). 4-way symmetric. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Floors|Hallway Variants")
	TSoftObjectPtr<UStaticMesh> HallwayFloorCrossroad;

	/** Rotation offset for crossroad floor meshes with non-standard native orientation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Floors|Hallway Variants")
	FRotator HallwayFloorCrossroadRotationOffset;

	/** Scale multiplier applied on top of auto-fit scaling. (1,1,1) = exact cell fit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Floors|Hallway Variants")
	FVector HallwayFloorCrossroadScaleMultiplier;

	/** Hallway floor for dead ends (1 neighbor). Open side faces +Y. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Floors|Hallway Variants")
	TSoftObjectPtr<UStaticMesh> HallwayFloorEndCap;

	/** Rotation offset for end cap floor meshes with non-standard native orientation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Floors|Hallway Variants")
	FRotator HallwayFloorEndCapRotationOffset;

	/** Scale multiplier applied on top of auto-fit scaling. (1,1,1) = exact cell fit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Floors|Hallway Variants")
	FVector HallwayFloorEndCapScaleMultiplier;

	// --- Hallway Ceiling Variants (optional — null falls back to HallwayCeiling) ---

	/** Hallway ceiling for straight sections (2 opposite neighbors). Runs along +Y. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Ceilings|Hallway Variants")
	TSoftObjectPtr<UStaticMesh> HallwayCeilingStraight;

	/** Rotation offset for straight ceiling meshes with non-standard native orientation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Ceilings|Hallway Variants")
	FRotator HallwayCeilingStraightRotationOffset;

	/** Scale multiplier applied on top of auto-fit scaling. (1,1,1) = exact cell fit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Ceilings|Hallway Variants")
	FVector HallwayCeilingStraightScaleMultiplier;

	/** Hallway ceiling for corners (2 adjacent neighbors). Connects +X and +Y. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Ceilings|Hallway Variants")
	TSoftObjectPtr<UStaticMesh> HallwayCeilingCorner;

	/** Rotation offset for corner ceiling meshes with non-standard native orientation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Ceilings|Hallway Variants")
	FRotator HallwayCeilingCornerRotationOffset;

	/** Scale multiplier applied on top of auto-fit scaling. (1,1,1) = exact cell fit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Ceilings|Hallway Variants")
	FVector HallwayCeilingCornerScaleMultiplier;

	/** Hallway ceiling for T-junctions (3 neighbors). Missing side is -Y. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Ceilings|Hallway Variants")
	TSoftObjectPtr<UStaticMesh> HallwayCeilingTJunction;

	/** Rotation offset for T-junction ceiling meshes with non-standard native orientation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Ceilings|Hallway Variants")
	FRotator HallwayCeilingTJunctionRotationOffset;

	/** Scale multiplier applied on top of auto-fit scaling. (1,1,1) = exact cell fit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Ceilings|Hallway Variants")
	FVector HallwayCeilingTJunctionScaleMultiplier;

	/** Hallway ceiling for crossroads (4 neighbors). 4-way symmetric. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Ceilings|Hallway Variants")
	TSoftObjectPtr<UStaticMesh> HallwayCeilingCrossroad;

	/** Rotation offset for crossroad ceiling meshes with non-standard native orientation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Ceilings|Hallway Variants")
	FRotator HallwayCeilingCrossroadRotationOffset;

	/** Scale multiplier applied on top of auto-fit scaling. (1,1,1) = exact cell fit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Ceilings|Hallway Variants")
	FVector HallwayCeilingCrossroadScaleMultiplier;

	/** Hallway ceiling for dead ends (1 neighbor). Open side faces +Y. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Ceilings|Hallway Variants")
	TSoftObjectPtr<UStaticMesh> HallwayCeilingEndCap;

	/** Rotation offset for end cap ceiling meshes with non-standard native orientation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Ceilings|Hallway Variants")
	FRotator HallwayCeilingEndCapRotationOffset;

	/** Scale multiplier applied on top of auto-fit scaling. (1,1,1) = exact cell fit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Ceilings|Hallway Variants")
	FVector HallwayCeilingEndCapScaleMultiplier;

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
