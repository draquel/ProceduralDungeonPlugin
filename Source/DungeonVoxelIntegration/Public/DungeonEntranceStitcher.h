#pragma once

#include "CoreMinimal.h"
#include "DungeonVoxelTypes.h"
#include "DungeonEntranceStitcher.generated.h"

struct FDungeonResult;
class UVoxelChunkManager;
class UDungeonVoxelConfig;

/**
 * Connects the dungeon entrance to the terrain surface.
 *
 * Carves a traversable passage from the voxel terrain surface down to the
 * dungeon entrance cell, using one of four visual styles. Works with the
 * VoxelEditManager to create undo-able edits marked as System source.
 */
UCLASS(BlueprintType)
class DUNGEONVOXELINTEGRATION_API UDungeonEntranceStitcher : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Stitch a passage from the terrain surface to the dungeon entrance.
	 * @param Result The generated dungeon data (uses EntranceCell and CellWorldSize).
	 * @param ChunkManager The voxel chunk manager to edit.
	 * @param WorldOffset World-space offset applied to dungeon grid coordinates.
	 * @param Style Visual style for the entrance passage.
	 * @param Config Material and scale configuration.
	 * @return Number of voxels modified, or -1 on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "DungeonVoxelIntegration|Entrance")
	int32 StitchEntrance(
		const FDungeonResult& Result,
		UVoxelChunkManager* ChunkManager,
		const FVector& WorldOffset,
		EDungeonEntranceStyle Style,
		UDungeonVoxelConfig* Config);

private:
	/** Detect terrain surface height at a world XY position using the world mode or vertical sweep. */
	float DetectSurfaceHeight(UVoxelChunkManager* ChunkManager, float WorldX, float WorldY) const;

	/** Carve a column of air from top Z to bottom Z, with optional wall shell. */
	int32 CarveColumn(
		class UVoxelEditManager* EditManager,
		UVoxelChunkManager* ChunkManager,
		const FVector& Center,
		float HalfExtentXY,
		float TopZ,
		float BottomZ,
		float VoxelSize,
		bool bPlaceWalls,
		uint8 WallMaterialID,
		uint8 BiomeID);

	int32 StitchVerticalShaft(const FDungeonResult& Result, UVoxelChunkManager* ChunkManager,
		const FVector& WorldOffset, UDungeonVoxelConfig* Config, float VoxelSize);

	int32 StitchSlopedTunnel(const FDungeonResult& Result, UVoxelChunkManager* ChunkManager,
		const FVector& WorldOffset, UDungeonVoxelConfig* Config, float VoxelSize);

	int32 StitchCaveOpening(const FDungeonResult& Result, UVoxelChunkManager* ChunkManager,
		const FVector& WorldOffset, UDungeonVoxelConfig* Config, float VoxelSize);

	int32 StitchTrapdoor(const FDungeonResult& Result, UVoxelChunkManager* ChunkManager,
		const FVector& WorldOffset, UDungeonVoxelConfig* Config, float VoxelSize);
};
