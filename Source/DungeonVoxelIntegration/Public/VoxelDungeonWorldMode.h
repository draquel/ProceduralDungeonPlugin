#pragma once

#include "CoreMinimal.h"
#include "IVoxelWorldMode.h"
#include "DungeonTypes.h"

class UDungeonVoxelConfig;

/**
 * Voxel world mode that generates dungeon geometry as a standalone SDF.
 *
 * Stores a copy of FDungeonGrid (no UObject references, thread-safe).
 * Open cells evaluate to negative density (air), solid/boundary cells evaluate
 * to positive density (solid). Boundary faces produce a gradient for smooth
 * surface transitions when using MarchingCubes or DualContouring meshing.
 *
 * Usage: Create an AVoxelWorld, set its world mode to an instance of this class
 * via configuration, and the dungeon will generate as a standalone voxel volume.
 */
class DUNGEONVOXELINTEGRATION_API FVoxelDungeonWorldMode : public IVoxelWorldMode
{
public:
	FVoxelDungeonWorldMode();

	/**
	 * Initialize with dungeon data. Takes a deep copy of the grid.
	 * @param InResult Source dungeon result (grid is copied, UObject refs are not stored).
	 * @param InWorldOffset World-space offset for the dungeon volume.
	 * @param InConfig Configuration for material and scale mapping. Values are copied.
	 * @param InVoxelSize Voxel size from the target VoxelWorldConfiguration.
	 */
	void Initialize(
		const FDungeonResult& InResult,
		const FVector& InWorldOffset,
		const UDungeonVoxelConfig* InConfig,
		float InVoxelSize);

	bool IsInitialized() const { return bInitialized; }

	// ==================== IVoxelWorldMode Interface ====================

	virtual float GetDensityAt(
		const FVector& WorldPos,
		int32 LODLevel,
		float NoiseValue) const override;

	virtual float GetTerrainHeightAt(
		float X,
		float Y,
		const FVoxelNoiseParams& NoiseParams) const override;

	virtual FIntVector WorldToChunkCoord(
		const FVector& WorldPos,
		int32 ChunkSize,
		float VoxelSize) const override;

	virtual FVector ChunkCoordToWorld(
		const FIntVector& ChunkCoord,
		int32 ChunkSize,
		float VoxelSize,
		int32 LODLevel) const override;

	virtual int32 GetMinZ() const override;
	virtual int32 GetMaxZ() const override;

	virtual EWorldMode GetWorldModeType() const override { return EWorldMode::InfinitePlane; }
	virtual bool IsHeightmapBased() const override { return false; }

	virtual uint8 GetMaterialAtDepth(
		const FVector& WorldPos,
		float SurfaceHeight,
		float DepthBelowSurface) const override;

private:
	/** Copied dungeon grid data (thread-safe, no UObject refs). */
	FDungeonGrid Grid;
	FIntVector GridSize;
	float CellWorldSize = 400.0f;
	FVector WorldOffset = FVector::ZeroVector;
	int32 VoxelsPerCell = 4;
	float VoxelSize = 100.0f;
	int32 WallThickness = 1;
	bool bInitialized = false;

	/** Copied room data for material lookup. */
	TArray<FDungeonRoom> Rooms;

	/** Material IDs copied from config. */
	uint8 WallMaterialID = 2;
	uint8 FloorMaterialID = 2;
	uint8 CeilingMaterialID = 2;
	uint8 StaircaseMaterialID = 2;
	uint8 DoorFrameMaterialID = 2;
	uint8 DungeonBiomeID = 0;
	TMap<EDungeonRoomType, uint8> RoomTypeMaterialOverrides;

	/** Convert world position to grid coordinate. Returns false if outside grid. */
	bool WorldToGridCoord(const FVector& WorldPos, FIntVector& OutGridCoord) const;

	/** Check if a cell type is open/traversable. */
	static bool IsOpenCell(EDungeonCellType CellType);

	/** Get material for a cell at given position. */
	uint8 GetMaterialForPosition(const FVector& WorldPos, const FIntVector& GridCoord) const;

	/** Vertical limits in chunk coordinates. */
	int32 MinZChunks = -4;
	int32 MaxZChunks = 4;
};
