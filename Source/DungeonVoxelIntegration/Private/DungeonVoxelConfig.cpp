#include "DungeonVoxelConfig.h"

int32 UDungeonVoxelConfig::GetEffectiveVoxelsPerCell(float CellWorldSize, float VoxelSize) const
{
	if (VoxelsPerCellOverride > 0)
	{
		return VoxelsPerCellOverride;
	}
	return FMath::Max(1, FMath::RoundToInt32(CellWorldSize / VoxelSize));
}

uint8 UDungeonVoxelConfig::GetMaterialForCell(EDungeonCellType CellType, EDungeonRoomType RoomType, int32 BoundaryFace) const
{
	// Check room-type override first
	if (CellType == EDungeonCellType::Room || CellType == EDungeonCellType::Door || CellType == EDungeonCellType::Entrance)
	{
		if (const uint8* Override = RoomTypeMaterialOverrides.Find(RoomType))
		{
			return *Override;
		}
	}

	// Staircase surfaces
	if (CellType == EDungeonCellType::Staircase || CellType == EDungeonCellType::StaircaseHead)
	{
		return StaircaseMaterialID;
	}

	// Door frame
	if (CellType == EDungeonCellType::Door)
	{
		return DoorFrameMaterialID;
	}

	// Boundary face: floor (-Z=5) or ceiling (+Z=4)
	if (BoundaryFace == 5)
	{
		return FloorMaterialID;
	}
	if (BoundaryFace == 4)
	{
		return CeilingMaterialID;
	}

	// Walls (faces 0-3) or default
	return WallMaterialID;
}
