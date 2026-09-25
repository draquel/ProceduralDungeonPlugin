#pragma once

#include "CoreMinimal.h"
#include "DungeonVoxelTypes.h"
#include "DungeonVoxelLattice.h"
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
	// Open-cell / wall / floor decisions come from FDungeonBoundaryRules (DungeonCore), shared
	// with the tile mapper. Do not add local copies here: they drift, and a drifted rule means
	// tiles seal space the voxels carved (or vice versa).

	/** Returns the EDungeonRoomType for a cell based on its RoomIndex, or Generic for non-room cells. */
	static EDungeonRoomType GetRoomTypeForCell(const FDungeonCell& Cell, const FDungeonResult& Result);

	/** Carve a single cell's voxel volume to air. Returns number of voxels modified. */
	int32 CarveCell(
		class UVoxelEditManager* EditManager,
		const FDungeonVoxelLattice& Lattice,
		const FVector& CellWorldMin,
		float CellWorldSize,
		bool bOnlyIfSolid,
		UVoxelChunkManager* ChunkManager);

	/**
	 * Place boundary voxels on a cell face, INSIDE the open cell (the stone lining that makes a
	 * VoxelCarved dungeon read as rock). Returns number of voxels placed.
	 */
	int32 PlaceBoundary(
		class UVoxelEditManager* EditManager,
		const FDungeonVoxelLattice& Lattice,
		const FVector& CellWorldMin,
		float CellWorldSize,
		int32 Face,
		float Thickness,
		uint8 MaterialID,
		uint8 BiomeID);

	/**
	 * Place seal voxels on a cell face, OUTSIDE the open cell.
	 *
	 * CarveOnly dungeons are lined by tile meshes, so the inward boundary pass is skipped — but
	 * that also removed the only thing asserting the surrounding voxel field is solid. Dungeons
	 * are deliberately anchored at the cave layer, so procedural cave voids intersect the volume
	 * and breach it: terrain reads straight through the thin tiles. This writes the shell just
	 * outside each boundary face instead, sealing the volume without occupying the cell where the
	 * tiles stand.
	 *
	 * Which lattice samples the slab writes is decided by FDungeonVoxelStampPlan: every sample
	 * that Pass 1 carved (any open cell) is excluded by index, so a seal can never plug a
	 * neighbouring room or hallway, nor re-solidify a layer of the cell it belongs to.
	 *
	 * @param OpenCellSamples Lattice indices of every sample inside any open cell, as carved.
	 * @param SealedVoxels Lattice indices already sealed by this stamp. The six face slabs of a
	 *        cell overlap at its edges and corners (that overlap is what closes the shell), and
	 *        neighbouring cells re-seal each other's shells, so without this the same voxel is
	 *        written many times over — all redundant, since every write is the same value.
	 * @return Number of voxels placed.
	 */
	int32 PlaceOuterSeal(
		class UVoxelEditManager* EditManager,
		const FDungeonVoxelLattice& Lattice,
		const FVector& CellWorldMin,
		float CellWorldSize,
		int32 Face,
		float Thickness,
		uint8 MaterialID,
		uint8 BiomeID,
		const TSet<FIntVector>& OpenCellSamples,
		TSet<FIntVector>& SealedVoxels);
};
