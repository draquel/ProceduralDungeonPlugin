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

	/**
	 * Number of voxel layers for walls, floors, and ceilings: the stone jacket around every closed face.
	 *
	 * The voxel-lined mode (CarveUnderground): the first InnerLiningLayers line the face
	 * from INSIDE the cell (their surface is the floor, wall or ceiling) and the rest wrap it from OUTSIDE, never into
	 * another open cell, so a thick jacket keeps the room's interior. CarveOnly (tile-dressed): every layer is the outer
	 * seal, past CarveMarginVoxels.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scale", meta = (ClampMin = "1", ClampMax = "8"))
	int32 WallThickness = 1;

	/**
	 * Voxel layers of the jacket that line a closed face from inside an open cell in the voxel-lined modes. One layer is
	 * the surface content stands on (a slab a voxel thick has a sample on every lattice line through it) and the most a
	 * cell can spare: lining all of WallThickness from inside filled every one-storey cell once WallThickness * VoxelSize
	 * reached half of CellWorldSize (the demo's 3 layers of 75 on 400 cells stamped its rooms solid, so nothing could
	 * stand in them: review finding G24).
	 */
	static constexpr int32 InnerLiningLayers = 1;

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

	/** Voxel-lined modes: the jacket's layers inside the cell (InnerLiningLayers, or fewer for a thinner jacket). */
	int32 GetInnerLiningLayers() const;

	/** Voxel-lined modes: the jacket's layers outside the cell (WallThickness less the inner lining, at least 0). */
	int32 GetOuterJacketLayers() const;

	/**
	 * Voxel-lined modes: how far the inner lining reaches into the cell from each closed face, in world units. A
	 * voxel-carved dungeon's floor lies this far above its cell's bottom plane, within half a voxel (the lattice's phase
	 * against the grid), so content standing on a floor (chests, the boss, the elevator's stop) is placed by it.
	 *
	 * @param VoxelSize The voxel world's voxel size.
	 * @return The inner lining's thickness.
	 */
	float GetInnerLiningThickness(float VoxelSize) const;

	/**
	 * Returns the appropriate voxel material ID for a given cell context.
	 * @param CellType The dungeon cell type.
	 * @param RoomType The room type (only used if cell is room-family).
	 * @param BoundaryFace 0-5: +X,-X,+Y,-Y,+Z(ceiling),-Z(floor). -1 = interior/carve.
	 */
	uint8 GetMaterialForCell(EDungeonCellType CellType, EDungeonRoomType RoomType, int32 BoundaryFace) const;
};
