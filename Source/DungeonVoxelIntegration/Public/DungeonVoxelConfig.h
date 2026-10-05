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

	/**
	 * CarveOnly (tile-dressed) dungeons only: how far past every cell plane the void is carved, in
	 * voxels. The meshed rock surface sits midway between the last carved sample and the first
	 * solid one, so with 0 it lies within half a voxel of the plane on EITHER side and can stand
	 * up to VoxelSize / 2 inside the cell, behind the tiles. 0.5 moves the whole band outward: the
	 * surface then lies in [plane, plane + VoxelSize) and rock never crosses a cell plane inward,
	 * so module faces may sit at any inset. The outer seal moves out with it, so its thickness is
	 * unchanged. Ignored by the voxel-lined modes, whose stone lining defines the walls.
	 *
	 * Size it to the DEEPEST wall-family piece: the surface lies in
	 * (plane + Margin - VoxelSize / 2, plane + Margin + VoxelSize / 2], so a module that extrudes
	 * D past the cell plane (the pack crypt wall: 70, its niches open onto the rock behind) needs
	 * Margin >= D + VoxelSize / 2 — 1.2 voxels at VoxelSize 100, 1.5 for headroom. With only 0.5
	 * the rock shows inside every niche. Keep Margin * 2 below the rock left between two open
	 * cells a buffer cell apart (CellWorldSize).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scale", meta = (ClampMin = "0", ClampMax = "3"))
	float CarveMarginVoxels = 0.0f;

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
