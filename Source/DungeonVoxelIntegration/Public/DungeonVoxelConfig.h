#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DungeonTypes.h"
#include "DungeonVoxelConfig.generated.h"

/**
 * Configuration data asset for dungeon-to-voxel stamping.
 * Controls material mapping, scale bridging, and wall thickness.
 */
UCLASS(BlueprintType)
class DUNGEONVOXELINTEGRATION_API UDungeonVoxelConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Voxel material ID for dungeon walls. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Materials")
	uint8 WallMaterialID = 2;

	/** Voxel material ID for dungeon floors. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Materials")
	uint8 FloorMaterialID = 2;

	/** Voxel material ID for dungeon ceilings. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Materials")
	uint8 CeilingMaterialID = 2;

	/** Voxel material ID for staircase surfaces. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Materials")
	uint8 StaircaseMaterialID = 2;

	/** Voxel material ID for door frame boundaries. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Materials")
	uint8 DoorFrameMaterialID = 2;

	/** Per-room-type material overrides. If a room's type is in this map, its walls/floors use this material. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Materials")
	TMap<EDungeonRoomType, uint8> RoomTypeMaterialOverrides;

	/** Override for voxels per cell. 0 = auto-calculated from CellWorldSize / VoxelSize. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scale", meta = (ClampMin = "0", ClampMax = "32"))
	int32 VoxelsPerCellOverride = 0;

	/** Number of voxel layers for walls, floors, and ceilings. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scale", meta = (ClampMin = "1", ClampMax = "8"))
	int32 WallThickness = 1;

	/** Biome ID assigned to all dungeon voxels. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Biome")
	uint8 DungeonBiomeID = 0;

	/** Returns override value if set, otherwise auto-calculates from world sizes. */
	int32 GetEffectiveVoxelsPerCell(float CellWorldSize, float VoxelSize) const;

	/**
	 * Returns the appropriate voxel material ID for a given cell context.
	 * @param CellType The dungeon cell type.
	 * @param RoomType The room type (only used if cell is room-family).
	 * @param BoundaryFace 0-5: +X,-X,+Y,-Y,+Z(ceiling),-Z(floor). -1 = interior/carve.
	 */
	uint8 GetMaterialForCell(EDungeonCellType CellType, EDungeonRoomType RoomType, int32 BoundaryFace) const;
};
