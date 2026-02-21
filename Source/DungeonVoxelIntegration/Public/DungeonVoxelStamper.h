#pragma once

#include "CoreMinimal.h"
#include "DungeonVoxelTypes.h"
#include "DungeonVoxelStamper.generated.h"

struct FDungeonResult;
struct FDungeonGrid;
struct FDungeonCell;
class UVoxelChunkManager;
class UDungeonVoxelConfig;
enum class EDungeonCellType : uint8;
enum class EDungeonRoomType : uint8;

/** Result data from a dungeon stamp operation. */
USTRUCT(BlueprintType)
struct DUNGEONVOXELINTEGRATION_API FDungeonStampResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "DungeonVoxelIntegration")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "DungeonVoxelIntegration")
	int32 VoxelsModified = 0;

	UPROPERTY(BlueprintReadOnly, Category = "DungeonVoxelIntegration")
	int32 ChunksAffected = 0;

	UPROPERTY(BlueprintReadOnly, Category = "DungeonVoxelIntegration")
	float StampTimeMs = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "DungeonVoxelIntegration")
	FString ErrorMessage;
};

/**
 * Stamps dungeon geometry into a voxel world.
 *
 * Supports three modes:
 * - CarveUnderground: carves air pockets for rooms/hallways below terrain
 * - ReplaceRegion: clears a volume and builds dungeon walls from scratch
 * - MergeAsStructure: only carves where terrain is currently solid
 *
 * Uses a 2-pass algorithm: first carve all open cells to air, then place
 * boundary voxels (walls/floors/ceilings) on faces adjacent to solid/OOB.
 */
UCLASS(BlueprintType)
class DUNGEONVOXELINTEGRATION_API UDungeonVoxelStamper : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Stamp a dungeon result into a voxel chunk manager.
	 * @param Result The generated dungeon data.
	 * @param ChunkManager The voxel chunk manager to edit.
	 * @param WorldOffset World-space offset applied to all grid coordinates.
	 * @param StampMode How to apply the dungeon to existing terrain.
	 * @param Config Material and scale configuration.
	 * @return Stamp result with success flag and statistics.
	 */
	UFUNCTION(BlueprintCallable, Category = "DungeonVoxelIntegration|Stamper")
	FDungeonStampResult StampDungeon(
		const FDungeonResult& Result,
		UVoxelChunkManager* ChunkManager,
		const FVector& WorldOffset,
		EDungeonStampMode StampMode,
		UDungeonVoxelConfig* Config);

private:
	/** Returns true if the cell type represents open/traversable space. */
	static bool IsOpenCell(EDungeonCellType CellType);

	/** Replicates DungeonTileMapper boundary detection for horizontal faces. */
	static bool NeedsWall(const FDungeonGrid& Grid, const FDungeonCell& Current, int32 NX, int32 NY, int32 NZ);

	/** Replicates DungeonTileMapper boundary detection for vertical faces. */
	static bool NeedsVerticalBoundary(const FDungeonGrid& Grid, const FDungeonCell& Current, int32 NX, int32 NY, int32 NZ);

	/** Returns the EDungeonRoomType for a cell based on its RoomIndex, or Generic for non-room cells. */
	static EDungeonRoomType GetRoomTypeForCell(const FDungeonCell& Cell, const FDungeonResult& Result);

	/** Carve a single cell's voxel volume to air. Returns number of voxels modified. */
	int32 CarveCell(
		class UVoxelEditManager* EditManager,
		const FVector& CellWorldMin,
		int32 VoxelsPerCell,
		float VoxelSize,
		bool bOnlyIfSolid,
		UVoxelChunkManager* ChunkManager);

	/** Place boundary voxels on a cell face. Returns number of voxels placed. */
	int32 PlaceBoundary(
		class UVoxelEditManager* EditManager,
		const FVector& CellWorldMin,
		int32 VoxelsPerCell,
		float VoxelSize,
		int32 Face,
		int32 Thickness,
		uint8 MaterialID,
		uint8 BiomeID);
};
