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

	UPROPERTY(BlueprintReadOnly, Category = "Dungeon|Voxel")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "Dungeon|Voxel")
	int32 VoxelsModified = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Dungeon|Voxel")
	int32 ChunksAffected = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Dungeon|Voxel")
	float StampTimeMs = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Dungeon|Voxel")
	FString ErrorMessage;
};

/** The voxel world's layout as a stamp needs it (UDungeonVoxelStamper::GetWorldLayout). */
struct FDungeonStampWorldLayout
{
	/** The voxel world's WorldOrigin (its lattice's anchor). */
	FVector WorldOrigin = FVector::ZeroVector;

	/** The voxel world's VoxelSize. */
	float VoxelSize = 100.0f;

	/** The voxel world's ChunkSize (voxels per chunk edge). */
	int32 ChunkSize = 32;
};

/** One voxel a planned stamp writes: its index on the world's lattice and the voxel (FVoxelData packed). */
struct FDungeonStampWrite
{
	FIntVector Index = FIntVector::ZeroValue;
	uint32 PackedVoxel = 0;
};

/** A planned stamp's voxels in one chunk, each once, in the order the passes first reached them. */
struct FDungeonStampChunkWrites
{
	FIntVector ChunkCoord = FIntVector::ZeroValue;
	TArray<FDungeonStampWrite> Writes;
};

/**
 * A stamp worked out and not yet written (UDungeonVoxelStamper::PlanStampInFrame, G280): every voxel the stamp writes, once,
 * with the value of the stamp's last write to it, by chunk. Planned on a worker and applied a few chunks a tick on the game
 * thread, it leaves the edit layer the one-frame stamp leaves: every write of a stamp is a Set, which replaces what the
 * voxel held, so a voxel's last write is its value, and a chunk's voxels are written in one operation.
 */
struct FDungeonPlannedStamp
{
	/** The world's lattice in the dungeon's frame (the writes' edit positions). */
	FDungeonVoxelLattice Lattice;

	/** The voxel world's layout the plan was made for. */
	FDungeonStampWorldLayout Layout;

	/** The chunks, in coordinate order (Z, then Y, then X). */
	TArray<FDungeonStampChunkWrites> Chunks;

	/** The chunks of the cells' centres, which the one-frame stamp marks dirty after its operation. */
	TArray<FIntVector> DirtyChunks;

	/** The writes the passes made, a voxel written twice counted twice (the one-frame stamp's VoxelsModified). */
	int32 VoxelWrites = 0;

	/** The distinct voxels written. */
	int32 Voxels = 0;

	/** Whether the plan was made. */
	bool bValid = false;

	/** Why not, when it was not. */
	FString ErrorMessage;

	/** How long the planning took. */
	float PlanTimeMs = 0.0f;
};

/**
 * Stamps dungeon geometry into a voxel world.
 *
 * Uses a 2-pass algorithm: first carve all open cells to air, then place
 * boundary voxels (walls/floors/ceilings) on faces adjacent to solid/OOB: the
 * inner lining inside the cell and the rest of the WallThickness jacket outside
 * it (UDungeonVoxelConfig::WallThickness; CarveOnly writes only the outside).
 */
UCLASS(BlueprintType)
class DUNGEONVOXELINTEGRATION_API UDungeonVoxelStamper : public UObject
{
	GENERATED_BODY()

public:

	/**
	 * Stamp a dungeon result into a voxel chunk manager, the dungeon laid out in a frame (spherical Phase 5, SP-G6):
	 * WorldOffset and every cell, slab and stair are in frame space, mapped to the world by Frame (scale ignored). The
	 * identity frame is the world (a heightmap world's dungeons); on a planet a POI's pad space (the tangent frame at its
	 * centre), so the dungeon's floors lie across local up. The voxel lattice stays the world's: each frame-space box is
	 * enumerated by testing the samples of its world bounds in frame space, and a rotated tile-dressed (CarveOnly) dungeon
	 * carves half a voxel more margin and seals one voxel thicker (an oblique plane aliases by up to half a voxel's diagonal
	 * on the lattice). The one entry point (code review CR2, DNG-11).
	 * @param Result The generated dungeon data.
	 * @param ChunkManager The voxel chunk manager to edit.
	 * @param WorldOffset Offset applied to all grid coordinates (frame space).
	 * @param StampMode How to apply the dungeon to existing terrain.
	 * @param Config Material and scale configuration.
	 * @param Frame Frame space to world (FTransform::Identity: the world).
	 * @return Stamp result with success flag and statistics.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dungeon|Voxel|Stamper")
	FDungeonStampResult StampDungeonInFrame(
		const FDungeonResult& Result,
		UVoxelChunkManager* ChunkManager,
		const FVector& WorldOffset,
		EDungeonStampMode StampMode,
		UDungeonVoxelConfig* Config,
		const FTransform& Frame);

	/**
	 * The voxel world's layout as a stamp needs it, read on the game thread so a stamp can be planned on any thread.
	 * @param ChunkManager The voxel world
	 * @param OutLayout    Receives the world's origin, voxel size and chunk size
	 * @return False when the chunk manager has no configuration
	 */
	static bool GetWorldLayout(const UVoxelChunkManager* ChunkManager, FDungeonStampWorldLayout& OutLayout);

	/**
	 * Work out a stamp without writing it (G280): the same passes as StampDungeonInFrame, each voxel kept once with the
	 * value of its last write, grouped by the chunk the edit manager puts it in. Touches no voxel world and no UObject but
	 * the configuration (read only), so it runs on a worker. Applying every chunk of the plan (ApplyPlannedChunks) leaves
	 * the edit layer StampDungeonInFrame leaves, voxel for voxel.
	 * @param Result      The generated dungeon data
	 * @param Layout      The voxel world's layout (GetWorldLayout)
	 * @param WorldOffset Offset applied to all grid coordinates (frame space)
	 * @param StampMode   How to apply the dungeon to existing terrain
	 * @param Config      Material and scale configuration
	 * @param Frame       Frame space to world (FTransform::Identity: the world)
	 * @param OutPlan     Receives the plan
	 * @return Whether the plan is valid (else OutPlan.ErrorMessage says why)
	 */
	static bool PlanStampInFrame(
		const FDungeonResult& Result,
		const FDungeonStampWorldLayout& Layout,
		const FVector& WorldOffset,
		EDungeonStampMode StampMode,
		const UDungeonVoxelConfig* Config,
		const FTransform& Frame,
		FDungeonPlannedStamp& OutPlan);

	/**
	 * Write some of a planned stamp's chunks into the edit manager, game thread: one edit operation outside the undo
	 * history with the System source (left set, as StampDungeonInFrame leaves it), whole chunks only, so its listeners
	 * (the re-mesh, the collision cook, scatter) see each chunk's stamp complete.
	 * @param EditManager The voxel world's edit manager
	 * @param Plan        The plan (PlanStampInFrame)
	 * @param FirstChunk  The first chunk to write (an index into Plan.Chunks)
	 * @param NumChunks   How many chunks to write
	 * @return The voxels written
	 */
	static int32 ApplyPlannedChunks(class UVoxelEditManager* EditManager, const FDungeonPlannedStamp& Plan, int32 FirstChunk, int32 NumChunks);

	/**
	 * Mark the chunks of the cells' centres dirty, as StampDungeonInFrame does after its operation: call once every chunk of
	 * the plan is applied.
	 * @param ChunkManager The voxel world
	 * @param Plan         The applied plan
	 */
	static void MarkPlannedChunksDirty(UVoxelChunkManager* ChunkManager, const FDungeonPlannedStamp& Plan);

	// Open-cell / wall / floor decisions come from FDungeonBoundaryRules (DungeonCore), shared with the tile mapper, and the
	// passes that apply them live once in DungeonVoxelStamper.cpp (DungeonStampPasses), shared by the one-frame and the
	// planned stamp. Do not add local copies: they drift, and a drifted rule means tiles seal space the voxels carved.
};
