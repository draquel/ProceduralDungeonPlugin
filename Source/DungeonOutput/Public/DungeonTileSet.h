#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DungeonTileMapper.h" // EDungeonTileType
#include "DungeonTileModule.h" // UDungeonTileModule (TMap value type)
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

	// Base-slot orientation corrections. These give the base tiles the same knobs the hallway
	// variants already have: a mesh imported with a non-canonical orientation (e.g. a directional
	// wall whose finished face points the wrong way) can be rotated/scaled in data rather than
	// forcing a re-author. Defaults are identity (no change), so existing tilesets are unaffected.
	// Convention reminders:
	//   Floor/Ceiling mesh local axes: X=width, Y=depth, Z=thickness (flat slab).
	//   Wall/Door/Entrance mesh local axes: X=depth(thin), Y=width, Z=height; finished face toward +X.

	// --- Floors ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Floors")
	TSoftObjectPtr<UStaticMesh> RoomFloor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Floors")
	FRotator RoomFloorRotationOffset;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Floors")
	FVector RoomFloorScaleMultiplier;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Floors")
	TSoftObjectPtr<UStaticMesh> HallwayFloor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Floors")
	FRotator HallwayFloorRotationOffset;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Floors")
	FVector HallwayFloorScaleMultiplier;

	// --- Ceilings ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Ceilings")
	TSoftObjectPtr<UStaticMesh> RoomCeiling;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Ceilings")
	FRotator RoomCeilingRotationOffset;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Ceilings")
	FVector RoomCeilingScaleMultiplier;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Ceilings")
	TSoftObjectPtr<UStaticMesh> HallwayCeiling;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Ceilings")
	FRotator HallwayCeilingRotationOffset;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Ceilings")
	FVector HallwayCeilingScaleMultiplier;

	// --- Walls ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Walls")
	TSoftObjectPtr<UStaticMesh> WallSegment;

	/** Rotation offset for wall meshes with non-standard native orientation.
	 *  Default convention: the finished face points toward +X (into the room). If your wall faces
	 *  the wrong way, set Yaw=180 to flip it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Walls")
	FRotator WallSegmentRotationOffset;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Walls")
	FVector WallSegmentScaleMultiplier;

	// --- Doors ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Doors")
	TSoftObjectPtr<UStaticMesh> DoorFrame;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Doors")
	FRotator DoorFrameRotationOffset;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Doors")
	FVector DoorFrameScaleMultiplier;

	// --- Entrance ---

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Entrance")
	TSoftObjectPtr<UStaticMesh> EntranceFrame;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Entrance")
	FRotator EntranceFrameRotationOffset;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Entrance")
	FVector EntranceFrameScaleMultiplier;

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

	// --- Modules ---

	/**
	 * Multi-mesh module override per tile type. A module assigned here REPLACES that type's single
	 * mesh slot: instead of auto-fitting one mesh to the cell, the module's pieces are placed at a
	 * single uniform cell scale (see UDungeonTileModule). Types with no entry use their mesh slot as
	 * before. Modules bake to shared instances, so this stays instanced.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet|Modules")
	TMap<EDungeonTileType, TSoftObjectPtr<UDungeonTileModule>> TileModules;

	/** Returns true if at least one mesh slot is non-null. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "TileSet")
	bool IsValid() const;

	/** Collects all non-null mesh slots with their names. */
	void GetAllUniqueMeshes(TArray<TPair<FName, TSoftObjectPtr<UStaticMesh>>>& OutMeshes) const;
};
