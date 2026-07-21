#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DungeonTileMapper.h" // EDungeonTileType
#include "DungeonTileModule.h" // UDungeonTileModule
#include "DungeonTileSet.generated.h"

/**
 * Everything one dungeon tile type needs, in one place: a single mesh (auto-fit to the cell), an
 * optional multi-mesh module that REPLACES the mesh (placed at one uniform cell scale — see
 * UDungeonTileModule), and orientation/scale corrections for a mesh imported non-canonically.
 *
 * Convention reminders (for the single-mesh path):
 *   Floor/Ceiling mesh local axes: X=width, Y=depth, Z=thickness (flat slab).
 *   Wall/Door/Entrance mesh local axes: X=depth(thin), Y=width, Z=height; finished face toward +X.
 */
USTRUCT(BlueprintType)
struct DUNGEONOUTPUT_API FDungeonTileSlot
{
	GENERATED_BODY()

	/** Single mesh for this tile, auto-fit to the cell. Ignored when Module is set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slot")
	TSoftObjectPtr<UStaticMesh> Mesh;

	/** Multi-mesh module. When set, REPLACES Mesh: pieces are placed at one uniform cell scale. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slot")
	TSoftObjectPtr<UDungeonTileModule> Module;

	/** Rotation offset composed with the placement rotation (e.g. Yaw=180 to flip a wall's face). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slot")
	FRotator RotationOffset = FRotator::ZeroRotator;

	/** Scale multiplier applied on top of the auto-fit scale (single-mesh path). (1,1,1) = exact fit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slot")
	FVector ScaleMultiplier = FVector::OneVector;

	bool HasMesh() const { return !Mesh.IsNull(); }
	bool HasModule() const { return !Module.IsNull(); }
	bool IsActive() const { return HasMesh() || HasModule(); }
};

/**
 * Maps each dungeon tile type to its geometry (mesh or module) + orientation, via one
 * FDungeonTileSlot per type (Slots). Assign this to ADungeonActor to control dungeon appearance.
 *
 * Legacy tilesets authored with the old parallel fields (RoomFloor, WallSegmentRotationOffset,
 * TileModules, …) auto-migrate into Slots on load — the old fields are kept hidden purely for that
 * migration and should not be authored against.
 */
UCLASS(BlueprintType)
class DUNGEONOUTPUT_API UDungeonTileSet : public UDataAsset
{
	GENERATED_BODY()

public:
	UDungeonTileSet();

	/** Per-tile-type geometry + orientation. The single authoring surface. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet")
	TMap<EDungeonTileType, FDungeonTileSlot> Slots;

	/** When true, only adjacent Hallway cells count as connected for hallway-variant selection;
	 *  doors, entrances, and staircases are treated as walls, producing end caps at transitions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TileSet")
	bool bHallwayVariantsHallwayOnly = false;

	// --- Accessors (read Slots; safe for missing types) ---

	/** The slot for Type, or a default (empty) slot if none is configured. */
	const FDungeonTileSlot& GetSlot(EDungeonTileType Type) const;

	TSoftObjectPtr<UStaticMesh> GetMesh(EDungeonTileType Type) const { return GetSlot(Type).Mesh; }
	TSoftObjectPtr<UDungeonTileModule> GetModule(EDungeonTileType Type) const { return GetSlot(Type).Module; }
	FRotator GetRotationOffset(EDungeonTileType Type) const { return GetSlot(Type).RotationOffset; }
	FVector GetScaleMultiplier(EDungeonTileType Type) const { return GetSlot(Type).ScaleMultiplier; }

	/** Returns true if at least one slot has a mesh or module. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "TileSet")
	bool IsValid() const;

	/** Collects all non-null slot meshes with their tile-type names (single-mesh slots only). */
	void GetAllUniqueMeshes(TArray<TPair<FName, TSoftObjectPtr<UStaticMesh>>>& OutMeshes) const;

	// UObject
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;

private:
	/** Populate Slots with the base tile types pointing at the engine cube (fresh tilesets only). */
	void PopulateDefaultSlots();

	/** One-time upgrade of a legacy tileset: copy the deprecated parallel fields into Slots. */
	void MigrateLegacyFieldsToSlots();

	// ========================================================================
	// DEPRECATED — legacy parallel fields, kept ONLY so pre-Slots tilesets migrate on load.
	// Not editable; do not author against these. Removed once all assets are re-saved.
	// ========================================================================
	UPROPERTY() TSoftObjectPtr<UStaticMesh> RoomFloor;
	UPROPERTY() FRotator RoomFloorRotationOffset;
	UPROPERTY() FVector RoomFloorScaleMultiplier;
	UPROPERTY() TSoftObjectPtr<UStaticMesh> HallwayFloor;
	UPROPERTY() FRotator HallwayFloorRotationOffset;
	UPROPERTY() FVector HallwayFloorScaleMultiplier;
	UPROPERTY() TSoftObjectPtr<UStaticMesh> RoomCeiling;
	UPROPERTY() FRotator RoomCeilingRotationOffset;
	UPROPERTY() FVector RoomCeilingScaleMultiplier;
	UPROPERTY() TSoftObjectPtr<UStaticMesh> HallwayCeiling;
	UPROPERTY() FRotator HallwayCeilingRotationOffset;
	UPROPERTY() FVector HallwayCeilingScaleMultiplier;
	UPROPERTY() TSoftObjectPtr<UStaticMesh> WallSegment;
	UPROPERTY() FRotator WallSegmentRotationOffset;
	UPROPERTY() FVector WallSegmentScaleMultiplier;
	UPROPERTY() TSoftObjectPtr<UStaticMesh> DoorFrame;
	UPROPERTY() FRotator DoorFrameRotationOffset;
	UPROPERTY() FVector DoorFrameScaleMultiplier;
	UPROPERTY() TSoftObjectPtr<UStaticMesh> EntranceFrame;
	UPROPERTY() FRotator EntranceFrameRotationOffset;
	UPROPERTY() FVector EntranceFrameScaleMultiplier;
	UPROPERTY() TSoftObjectPtr<UStaticMesh> HallwayFloorStraight;
	UPROPERTY() FRotator HallwayFloorStraightRotationOffset;
	UPROPERTY() FVector HallwayFloorStraightScaleMultiplier;
	UPROPERTY() TSoftObjectPtr<UStaticMesh> HallwayFloorCorner;
	UPROPERTY() FRotator HallwayFloorCornerRotationOffset;
	UPROPERTY() FVector HallwayFloorCornerScaleMultiplier;
	UPROPERTY() TSoftObjectPtr<UStaticMesh> HallwayFloorTJunction;
	UPROPERTY() FRotator HallwayFloorTJunctionRotationOffset;
	UPROPERTY() FVector HallwayFloorTJunctionScaleMultiplier;
	UPROPERTY() TSoftObjectPtr<UStaticMesh> HallwayFloorCrossroad;
	UPROPERTY() FRotator HallwayFloorCrossroadRotationOffset;
	UPROPERTY() FVector HallwayFloorCrossroadScaleMultiplier;
	UPROPERTY() TSoftObjectPtr<UStaticMesh> HallwayFloorEndCap;
	UPROPERTY() FRotator HallwayFloorEndCapRotationOffset;
	UPROPERTY() FVector HallwayFloorEndCapScaleMultiplier;
	UPROPERTY() TSoftObjectPtr<UStaticMesh> HallwayCeilingStraight;
	UPROPERTY() FRotator HallwayCeilingStraightRotationOffset;
	UPROPERTY() FVector HallwayCeilingStraightScaleMultiplier;
	UPROPERTY() TSoftObjectPtr<UStaticMesh> HallwayCeilingCorner;
	UPROPERTY() FRotator HallwayCeilingCornerRotationOffset;
	UPROPERTY() FVector HallwayCeilingCornerScaleMultiplier;
	UPROPERTY() TSoftObjectPtr<UStaticMesh> HallwayCeilingTJunction;
	UPROPERTY() FRotator HallwayCeilingTJunctionRotationOffset;
	UPROPERTY() FVector HallwayCeilingTJunctionScaleMultiplier;
	UPROPERTY() TSoftObjectPtr<UStaticMesh> HallwayCeilingCrossroad;
	UPROPERTY() FRotator HallwayCeilingCrossroadRotationOffset;
	UPROPERTY() FVector HallwayCeilingCrossroadScaleMultiplier;
	UPROPERTY() TSoftObjectPtr<UStaticMesh> HallwayCeilingEndCap;
	UPROPERTY() FRotator HallwayCeilingEndCapRotationOffset;
	UPROPERTY() FVector HallwayCeilingEndCapScaleMultiplier;
	UPROPERTY() TSoftObjectPtr<UStaticMesh> StaircaseMesh;
	UPROPERTY() FRotator StaircaseMeshRotationOffset;
	UPROPERTY() TMap<EDungeonTileType, TSoftObjectPtr<UDungeonTileModule>> TileModules;
};
